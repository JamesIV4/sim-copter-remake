// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterGroundAgent.h"

#include "Audio/SimCopterAudioSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "City/SimCity2000CityActor.h"
#include "City/SimCopterEffectExposure.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Formats/MaxisMeshLibrary.h"
#include "Formats/MaxisProceduralMeshBuilder.h"
#include "Formats/SimCopterOriginalGamePaths.h"
#include "Formats/SimCopterPeopleCityRules.h"
#include "Formats/SimCopterPeopleReader.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Flight/SimCopterWaterGameplay.h"
#include "Game/SimCopterLowPowerMode.h"
#include "Game/SimCopterVehicleMaterialSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Ground/SimCopterCriminalCar.h"
#include "Ground/SimCopterAmbientVehicles.h"
#include "Ground/SimCopterInteraction.h"
#include "Ground/SimCopterOnFootPawn.h"
#include "Ground/SimCopterPopulationBody.h"
#include "Ground/SimCopterPopulationSprite.h"
#include "Ground/SimCopterTrafficSystemActor.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Missions/SimCopterMissionSystemActor.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterGroundAgent, Log, All);

namespace
{
constexpr uint32 GroundAgentRuntimeSaveMagic = 0x4147454e; // 'AGEN'
constexpr int32 GroundAgentRuntimeSaveVersion = 1;

void SerializeAgentBool(FArchive& Archive, bool& Value)
{
	uint8 Byte = Value ? 1 : 0;
	Archive << Byte;
	if (Archive.IsLoading()) Value = Byte != 0;
}
// The city (and its cars/helicopters) is rendered at 0.25x of real-cm so a 400cm remake tile
// stands in for the original 1600-unit tile. The population capsules and bodies were authored in
// real cm, which made pedestrians (and the on-foot player) read ~4x too tall next to the shrunk
// world. Everything visual/collision below is multiplied by this to match.
constexpr float PopulationWorldScale = 0.25f;

constexpr float VehicleCapsuleRadiusCm = 135.0f;
constexpr float VehicleCapsuleHalfHeightCm = 82.0f;
constexpr float PedestrianCapsuleRadiusCm = 32.0f;
constexpr float PedestrianCapsuleHalfHeightCm = 88.0f;
constexpr float TargetStopDistanceCm = 75.0f;
constexpr float VehicleFallbackZCm = 52.0f;
constexpr float PedestrianFallbackZCm = 88.0f;
constexpr float PedestrianSpriteHeightCm = 162.0f;
constexpr float PedestrianBodyHeightCm = 176.0f;
// How far above the sea's rest plane a pair of feet may be and still count as standing IN the water
// rather than on something built over it. The ground snap leaves a person 1 cm proud of whatever it
// hit, and the swell is +/- WaterWaveAmplitude on top of that, so the band has to clear the crest -
// but stay well under a bridge deck, which is a whole terrain step up.
constexpr float WaterStandingClearanceCm = 40.0f;
constexpr const TCHAR* SpriteMaterialPath = TEXT("/Game/Materials/M_SimCopterSpriteTexture.M_SimCopterSpriteTexture");
// The privanim head's material. Masked and chroma-keyed exactly like the unlit sprite material, but
// Default Lit - see FigureHeadMaterial in the header for why a head must not be an emissive card.
constexpr const TCHAR* FigureHeadMaterialPath = TEXT("/Game/Materials/M_SimCopterLitSpriteTexture.M_SimCopterLitSpriteTexture");

// FUN_004c1050 mode 1: the spotlight reaction only fires when rng % DAT_0058dc3a == 0.
// FUN_004c3010 writes 65000 to that global and something else writes a small tuning value;
// the write order is unresolved (see heli_tools_models_decode_20260724.md section 10), so the
// remake uses a playable 1-in-4 until it is decoded.
constexpr uint16 SpotlightReactionChance = 4;

uint16 GetPlayerBehaviorSpeedScalar(const APawn& Pawn)
{
	FVector Velocity = Pawn.GetVelocity();
	float ReferenceSpeed = 1.0f;
	if (const ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(&Pawn))
	{
		Velocity = Helicopter->GetVelocityCmPerSec();
		ReferenceSpeed = FMath::Max(1.0f, Helicopter->GetMaxForwardSpeedCmPerSec());
	}
	else if (const ASimCopterOnFootPawn* OnFoot = Cast<ASimCopterOnFootPawn>(&Pawn))
	{
		Velocity = OnFoot->GetCurrentVelocityCmPerSec();
		ReferenceSpeed = FMath::Max(1.0f, OnFoot->GetWalkSpeedCmPerSec());
	}

	return uint16(FMath::Clamp(FMath::RoundToInt((Velocity.Size2D() / ReferenceSpeed) * 10.0f), 0, 10));
}

// Process-wide people.df behavior model cache (one per original-game root).
TSharedPtr<FPeopleBehaviorModel> GetSharedBehaviorModel(const FString& RootPath)
{
	static TMap<FString, TSharedPtr<FPeopleBehaviorModel>> Cache;
	static TSet<FString> FailedRoots;
	const FString Key = FPaths::ConvertRelativePathToFull(RootPath);
	if (const TSharedPtr<FPeopleBehaviorModel>* Found = Cache.Find(Key))
	{
		return *Found;
	}
	if (FailedRoots.Contains(Key))
	{
		return nullptr;
	}
	const FString PeoplePath = FSimCopterPeopleReader::ResolvePeoplePath(RootPath);
	TSharedPtr<FPeopleBehaviorModel> Model = MakeShared<FPeopleBehaviorModel>();
	FString Error;
	if (PeoplePath.IsEmpty() || !FSimCopterPeopleReader::LoadFromFile(PeoplePath, *Model, Error))
	{
		FailedRoots.Add(Key);
		return nullptr;
	}
	Cache.Add(Key, Model);
	return Model;
}

UMaterialInterface* LoadSpriteMaterialNoWarn()
{
	return Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, SpriteMaterialPath, nullptr, LOAD_NoWarn));
}

UMaterialInterface* LoadFigureHeadMaterialNoWarn()
{
	return Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, FigureHeadMaterialPath, nullptr, LOAD_NoWarn));
}
}

ASimCopterGroundAgent::ASimCopterGroundAgent()
{
	PrimaryActorTick.bCanEverTick = true;

	CollisionComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CollisionComponent"));
	CollisionComponent->InitCapsuleSize(PedestrianCapsuleRadiusCm, PedestrianCapsuleHalfHeightCm);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionComponent->SetCollisionObjectType(ECC_Pawn);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	CollisionComponent->SetCanEverAffectNavigation(false);
	SetRootComponent(CollisionComponent);

	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(CollisionComponent);

	OriginalMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("OriginalMesh"));
	OriginalMeshComponent->SetupAttachment(VisualRoot);
	OriginalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OriginalMeshComponent->SetCanEverAffectNavigation(false);
	OriginalMeshComponent->SetVisibility(false);
	// A privanim figure is a ~44cm stack of overlapping primitives, which is smaller than a
	// single cascaded-shadow-map texel at any useful cascade size: the sun's CSM then shadows
	// the figure against itself and the acne lands differently on the coincident faces of
	// neighbouring parts, which is what reads as the fighting bars across a torso. A per-object
	// inset shadow gives the figure its own shadow map fitted to its bounds, so it keeps its
	// ground shadow and its response to the spotlight and street lights, without the acne.
	OriginalMeshComponent->bCastInsetShadow = true;

	ProxyMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProxyMesh"));
	ProxyMeshComponent->SetupAttachment(VisualRoot);
	ProxyMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProxyMeshComponent->SetCanEverAffectNavigation(false);

	// Headlights are cheap dynamic spotlights (no shadows) - there can be many cars on screen.
	HeadlightLeft = CreateDefaultSubobject<USpotLightComponent>(TEXT("HeadlightLeft"));
	HeadlightLeft->SetupAttachment(VisualRoot);
	HeadlightLeft->CastShadows = false;
	HeadlightLeft->SetVisibility(false);

	HeadlightRight = CreateDefaultSubobject<USpotLightComponent>(TEXT("HeadlightRight"));
	HeadlightRight->SetupAttachment(VisualRoot);
	HeadlightRight->CastShadows = false;
	HeadlightRight->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		ProxyMeshComponent->SetStaticMesh(CubeMeshFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ModelMaterialFinder(TEXT("/Game/Materials/M_SimCopterLitVertexColor.M_SimCopterLitVertexColor"));
	if (ModelMaterialFinder.Succeeded())
	{
		VertexColorMaterial = ModelMaterialFinder.Object;
	}

	SpriteMaterial = LoadSpriteMaterialNoWarn();
	FigureHeadMaterial = LoadFigureHeadMaterialNoWarn();

	OriginalGameRoot.Path = TEXT("../Reference/SimCopterOriginalGame");
	AnimationPhase = FMath::FRandRange(0.0f, UE_TWO_PI);

	// Everyday street mix (service figures and easter eggs are opt-in via PedestrianFigureName).
	PedestrianFigurePool = {
		TEXT("fatman"), TEXT("2blonde"), TEXT("Child"), TEXT("5.5man"), TEXT("SUIT"),
		TEXT("5man"), TEXT("SHADES"), TEXT("Blonde"), TEXT("2woman"), TEXT("Woman")};

	ApplyAgentShape();
}

void ASimCopterGroundAgent::BeginPlay()
{
	Super::BeginPlay();

	// Share the fleet's material instance so the metallic slider reaches the cars too.
	if (USimCopterVehicleMaterialSubsystem* VehicleMaterials = USimCopterVehicleMaterialSubsystem::Get(this))
	{
		if (UMaterialInstanceDynamic* Shared = VehicleMaterials->GetVehicleMaterial(VertexColorMaterial))
		{
			VertexColorMaterial = Shared;
		}
	}

	ApplyAgentShape();
	if (AgentKind == ESimCopterGroundAgentKind::Pedestrian)
	{
		BuildPedestrianBody();
		StartOriginalBehavior();
	}
	else if (!MeshTableName.IsEmpty())
	{
		LoadOriginalMeshFromOriginalGameRoot();
	}
}

void ASimCopterGroundAgent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopWalkingVoice();
	// There are fourteen voice slots for the whole city, so one has to go back the moment its
	// speaker leaves - a despawning medevac victim otherwise leaves its EKG looping forever.
	StopPersonVoice();

	// A person actor and its passenger slot are one ownership unit. If an external teardown
	// destroys the actor while it is still aboard, return the slot here so the helicopter cannot
	// be left permanently full with no person behind that seat.
	if (bClaimedPassengerSeat)
	{
		const int32 DroppedEventId = MissionEventId;
		const ESimCopterMissionPassengerKind DroppedKind = GetMissionPassengerKind();
		const bool bReturnPickupCount =
			bMissionPickupCounted && !bMissionResolutionReported && DroppedEventId != INDEX_NONE;
		if (ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(BehaviorCarrier.Get()))
		{
			Helicopter->RemoveMissionPassengersForMission(1, MissionEventId, GetMissionPassengerKind(), this);
		}
		bClaimedPassengerSeat = false;
		if (bReturnPickupCount)
		{
			if (ASimCopterMissionSystemActor* Missions = Cast<ASimCopterMissionSystemActor>(
				UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass())))
			{
				Missions->NotifyPassengerDroppedFromHelicopter(DroppedEventId, DroppedKind, 1);
			}
			bMissionPickupCounted = false;
		}
	}
	AlightAttachmentOnly();
	BehaviorCarrier.Reset();
	bRidingHarness = false;

	if (LowPowerChangedHandle.IsValid())
	{
		SimCopterLowPower::OnChanged().Remove(LowPowerChangedHandle);
		LowPowerChangedHandle.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

void ASimCopterGroundAgent::StartOriginalBehavior()
{
	bBehaviorActive = false;
	if (!bUseOriginalBehaviors || AgentKind != ESimCopterGroundAgentKind::Pedestrian)
	{
		return;
	}
	BehaviorModel = GetSharedBehaviorModel(ResolveOriginalGameRoot());
	if (!BehaviorModel.IsValid())
	{
		return;
	}
	BehaviorContext = FSimCopterPersonContext();
	// Seed the people PRNG per agent so crowds don't move in lockstep.
	BehaviorContext.Lfsr = uint16(GetTypeHash(GetFName()) | 1);
	BehaviorContext.Attributes[EBhavAttr::Facing] = uint16(FMath::RoundToInt(GetActorRotation().Yaw / 45.0f) & 7);
	BehaviorContext.Attributes[EBhavAttr::BehaviorClass] = uint16(FMath::Clamp(InitialBehaviorClass, 0, 21));
	// FUN_004c7090 enables the move core's clockwise retry loop (+0x16a) for every spawned
	// person; the move speed (+0x164) starts 0 and is assigned by the shipped programs
	// ("movespeed := 6/8/12/16/25" expressions in people.df).
	BehaviorContext.Attributes[EBhavAttr::AutoTurn] = 1;
	// person+0x152: a spawned person is on the map and can be seen. The shipped programs treat 1
	// as the live default and clear it only while riding something (BHAV 1052 "cop - ride on
	// copter" sets it to 0, BHAV 1055 sets it back on getting out). It gates every object-class
	// search in FUN_004ca350, so leaving it at 0 makes a person invisible to every other person -
	// which is what stopped cops finding criminals.
	BehaviorContext.Attributes[EBhavAttr::Visible] = 1;
	// FUN_004c71c0's appearance block, applied at spawn: the head this class wears, how its voice
	// is pitched and which looping voice event it owns. These have to be in place before the state
	// is set, because FUN_004c7090 overwrites the head for a medevac victim and must win.
	{
		const int32 SpawnClass = int32(BehaviorContext.Attributes[EBhavAttr::BehaviorClass]);
		BehaviorContext.Attributes[EBhavAttr::HeadImageIndex] =
			uint16(FSimCopterPeopleCityRules::GetHeadImageIndexForBehaviorClass(SpawnClass));
		BehaviorContext.Attributes[EBhavAttr::VoicePitch] =
			uint16(int16(FSimCopterPeopleCityRules::GetVoicePitchDeltaForBehaviorClass(SpawnClass)));
		BehaviorContext.Attributes[EBhavAttr::VoiceSet] =
			uint16(FSimCopterPeopleCityRules::ChooseVoiceSetForBehaviorClass(SpawnClass, BehaviorContext.Lfsr));
	}
	BehaviorContext.ResetToState(InitialPersonState);
	if (InitialBehaviorAgitation != 0)
	{
		BehaviorContext.Attributes[EBhavAttr::Speed] =
			uint16(FMath::Clamp(InitialBehaviorAgitation, 0, 0xffff));
	}
	// person+0x188/+0x18a: where this person started, which opcode 87 compares against.
	{
		int32 HomeX = INDEX_NONE;
		int32 HomeY = INDEX_NONE;
		BehaviorHomeTile = TryGetCurrentTileCoordinate(HomeX, HomeY)
			? FIntPoint(HomeX, HomeY)
			: FIntPoint(INDEX_NONE, INDEX_NONE);
	}
	ResetBehaviorProgramOverride();
	LastAppliedBehaviorFacing = INDEX_NONE;
	BehaviorStepVelocityCmPerSec = FVector::ZeroVector;
	BehaviorStepTimeRemainingSeconds = 0.0f;
	bBehaviorActive = true;
}

// SCHOOK: PersonInteractionReaction 0x004c1050
bool ASimCopterGroundAgent::ApplyInteraction(const FSimCopterInteractionEvent& Event)
{
	// FUN_0049a4f0 routes by object class; only the person class (obj[0xc] & 8) lands here.
	if (AgentKind != ESimCopterGroundAgentKind::Pedestrian || !bBehaviorActive)
	{
		return false;
	}

	// Acceptance tests from FUN_004c1050: never react to the helicopter body itself, and skip
	// people the mission layer has taken over (carried, injured pose, mid-fall) - the original
	// equivalents are the person+0x15e / person[0x52] / person+0x12e guards.
	if (Event.Source == this || bMissionCarried || bMissionStationary || bPassengerFallActive)
	{
		return false;
	}
	// The two exact guards from the same test: someone riding the player's helicopter is out of
	// reach of everything, and a medevac victim (state 6) never reacts to anything at all - they are
	// lying on the ground waiting for a pickup.
	if (BehaviorCarrier.Get() != nullptr && BehaviorCarrier.Get() == ResolvePlayerHelicopter())
	{
		return false;
	}
	if (BehaviorContext.Attributes[EBhavAttr::State] == 6)
	{
		return false;
	}

	// Mode 1 hard-codes BHAV 950 and only fires on a 1-in-N roll; every other mode reads
	// DAT_0058d728[mode]. Mode 13 is the exception: FUN_004c1050 uses param_5 *as the program id*
	// when it is non-zero, which is how a cop or a paramedic (attr 32 := 916 in BHAV 1400/1401/1402
	// and 801) makes the person they shove reach for "don't gawk, maybe run" instead of stopping to
	// chat.
	int32 ProgramId = INDEX_NONE;
	if (Event.Mode == ESimCopterInteractionMode::Spotlight)
	{
		if (BehaviorContext.RandomBounded(SpotlightReactionChance) != 0)
		{
			return false;
		}
		ProgramId = SimCopterInteraction::SpotlightReactionProgram;
	}
	else if (Event.Mode == ESimCopterInteractionMode::PersonNeutral && Event.MessageIndex != 0)
	{
		ProgramId = Event.MessageIndex;
	}
	else
	{
		ProgramId = SimCopterInteraction::GetPersonReactionProgram(Event.Mode);
	}

	if (ProgramId == INDEX_NONE ||
		!BehaviorModel.IsValid() ||
		BehaviorModel->FindProgram(ProgramId) == nullptr)
	{
		return false;
	}

	if (!BehaviorContext.PushReactionProgram(ProgramId))
	{
		return false;
	}

	BehaviorContext.ReactionParameter = Event.MessageIndex;
	// person+0x1a4, written on the same accepted branch: what caused this. Opcodes 32/33 turn away
	// from or toward it, opcode 80 gabs at it, and opcode 15 class 4 selects it.
	BehaviorInteractionSource = Event.Source;
	if (Event.Mode == ESimCopterInteractionMode::Megaphone)
	{
		// person+0x15a: the message index the shipped BHAV 901 branches on.
		BehaviorContext.MegaphoneMessageIndex = Event.MessageIndex;
	}
	return true;
}

void ASimCopterGroundAgent::ResetBehaviorProgramOverride()
{
	if (InitialBehaviorProgramId == INDEX_NONE)
	{
		return;
	}

	BehaviorContext.Stack.Reset();
	FSimCopterPersonContext::FFrame Frame;
	Frame.ProgramId = InitialBehaviorProgramId;
	BehaviorContext.Stack.Add(Frame);
}

void ASimCopterGroundAgent::UpdateOriginalBehavior(float DeltaSeconds)
{
	// The people VM only ever drives pedestrians; vehicles keep the road-route movement.
	if (!bBehaviorActive || !BehaviorModel.IsValid() || AgentKind != ESimCopterGroundAgentKind::Pedestrian)
	{
		return;
	}

	BehaviorTickAccumulator += DeltaSeconds * BehaviorTickRate;
	int32 Steps = FMath::FloorToInt(BehaviorTickAccumulator);
	if (Steps <= 0)
	{
		return;
	}
	BehaviorTickAccumulator -= float(Steps);
	Steps = FMath::Min(Steps, 4); // don't burst after hitches

	for (int32 Step = 0; Step < Steps; ++Step)
	{
		// DAT_00506448, incremented once per behaviour tick by FUN_004c5fb0. Opcode 79 reads it.
		++BehaviorTickCounter;
		const EBhavStepResult Result = FSimCopterBehaviorVM::Tick(BehaviorContext, *BehaviorModel, *this);
		if (Result == EBhavStepResult::Completed && InitialBehaviorProgramId != INDEX_NONE)
		{
			ResetBehaviorProgramOverride();
		}
		if (Result == EBhavStepResult::Stopped || BehaviorContext.bRequestDespawn)
		{
			ASimCopterMissionSystemActor* Missions = Cast<ASimCopterMissionSystemActor>(
				UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass()));
			const bool bDeadMedevacPatientAboard =
				bMissionPatientDead &&
				bClaimedPassengerSeat &&
				int32(BehaviorContext.Attributes[EBhavAttr::State]) == 6 &&
				Cast<ASimCopterHelicopterPawn>(BehaviorCarrier.Get()) != nullptr;
			if (bDeadMedevacPatientAboard)
			{
				// Opcode 66 normally asks the population layer to recycle the dead person. A
				// medevac body already in the cabin is still owned by that real passenger seat:
				// keep both until the hospital paramedic visibly removes them.
				BehaviorContext.bRequestDespawn = false;
				bBehaviorActive = false;
				bBehaviorMoveSuspended = true;
				SetActorHiddenInGame(true);
				return;
			}
			// An emergency worker is NOT a mission dependency, and must never be caught by the
			// clause below. It carries the record it was sent to only so its own opcode 13 can post
			// against it, and it never sets bMissionResolutionReported - that flag belongs to
			// passengers. So an ambulance medic reaching BHAV 269's op 40 while the medevac record
			// was still open had its despawn refused, was restarted into BHAV 801, and - already
			// attached to the ambulance with bBehaviorMoveSuspended set by BoardCarrier - could
			// never walk or finish again. That is the reported ambulance loop. The crew member's
			// own vehicle owns its lifetime: op 61 has already messaged it by the time op 40 runs.
			const bool bUnresolvedMissionPerson =
				MissionEventId != INDEX_NONE &&
				!bMissionResolutionReported &&
				!IsEmergencyCrewMember() &&
				Missions != nullptr &&
				Missions->IsMissionEventActive(MissionEventId);
			const bool bHospitalParamedic = bPersistentHospitalRoofCrew;
			if (bUnresolvedMissionPerson || bHospitalParamedic)
			{
				// A decoded program may time out or reach Disappear, but that cannot be allowed to
				// erase an unresolved mission dependency. State-5 hospital staff are likewise a
				// persistent service point: the original population code can recycle them, while
				// doing so visibly after a handoff makes the worker appear to vanish.
				//
				// Refusing the despawn is not enough on its own. The walker never advances past a
				// stop opcode (FUN_004ce7b0 returns on result 3 without touching the record
				// cursor), so the person stays parked ON it and re-executes it every tick from
				// then on - alive, visible and completely inert. That is the frozen hospital
				// paramedic: BHAV 801 -> 272 reaches 'Medevac disappear' (op 51 then op 40)
				// whenever the player is more than ten tiles away, which is nearly always true
				// moments after the roof is staffed, and 263 -> 269 ends the same way when no
				// patient is aboard. Restart the state program instead, the way FUN_004c7090 does
				// for a freshly spawned worker, so the post keeps probing for the helicopter.
				BehaviorContext.bRequestDespawn = false;
				BehaviorContext.ResetToState(BehaviorContext.GetStateIndex());
				ResetBehaviorProgramOverride();
				if (bHospitalParamedic)
				{
					// BHAV 801's entry runs 'Walk-10' once, with autoturn cleared, and the original
					// never gets there twice - the worker retires. Restarting the program replays
					// that walk on every refusal, and with the facing left alone every replay went
					// the same way, so the medic marched half a metre at a time to the edge of its
					// roof and stayed there. Re-rolling the facing turns the repeat back into what
					// its single execution reads as: a step somewhere, not a heading.
					BehaviorContext.Attributes[EBhavAttr::Facing] =
						uint16(BehaviorContext.RandomBounded(8));
				}
				if (!bClaimedPassengerSeat && !BehaviorCarrier.IsValid())
				{
					SetActorHiddenInGame(false);
					BehaviorContext.Attributes[EBhavAttr::Visible] = 1;
				}
				return;
			}

			// Original 'Disappear'/deactivate path: hide and let the spawner recycle us. Give any
			// seat back first, or a delivered passenger would leave the cabin counting them.
			if (bClaimedPassengerSeat || BehaviorCarrier.IsValid())
			{
				AlightFromCarrier();
			}
			bBehaviorActive = false;
			SetActorHiddenInGame(true);
			SetLifeSpan(1.0f);
			return;
		}
		if (Result == EBhavStepResult::Failed)
		{
			bBehaviorActive = false;
			return;
		}
	}

	// Anim binds map straight onto the figure clips (same 4-char mnemonics as ARLU).
	if (!BehaviorContext.PendingAnimMnemonic.IsEmpty())
	{
		// A victim still waiting on a pickup waves whenever their program leaves them standing;
		// the moment it walks them somewhere the walk clip takes back over.
		//
		// "WvNo", not "Wave". Both clips ship in every figure and they are not the same gesture -
		// the shipped bind sites say which is which. "Wave" belongs to panic: 287 'Rioter flee
		// tree', 289 'Rioter run', 902 'Rxn: Ouch', 1062 'Riot Follower', 805 'Fireman'. "WvNo" is
		// the one people play while standing about acknowledging somebody - 1020 the mechanic,
		// 1051/1053/1055 cops at the station, 1201 the rooftop worker, 1203 the park - and, above
		// all, **291 rec[4]**, which is the transport passenger waving at the player. That is this
		// exact situation, so it is this exact clip.
		if (bMissionWavesWhenIdle && BehaviorContext.PendingAnimMnemonic == TEXT("NoMo"))
		{
			BehaviorContext.PendingAnimMnemonic = TEXT("WvNo");
		}
		if (bUsingPedestrianFigure && BehaviorContext.PendingAnimMnemonic != FigureMnemonic)
		{
			RebuildFigureClip(BehaviorContext.PendingAnimMnemonic);
		}
		BehaviorContext.PendingAnimMnemonic.Reset();
	}

	UpdatePersonVoice();

	// Attribute 39 can have moved under us: FUN_004c7090 writes head 10 whenever a person becomes
	// a state-6 casualty, which a swoon (opcode 35) does mid-life. FUN_004c7f10 re-reads it every
	// time it draws the figure, so it costs one comparison here and only re-skins on a change.
	RefreshHeadImageIndex();

	// The original driver (FUN_004c6450) advances the bound clip one frame per behavior tick,
	// wrapping at the clip's ARPP row count - playback rate is the tick rate, not wall time.
	AdvanceBehaviorFigureFrames(Steps);

	// The figure renders at the person's stored octant facing; op29/scatter turns must show
	// even when standing, so the yaw comes from the attribute, not from velocity.
	ApplyBehaviorFacingRotation();
}

void ASimCopterGroundAgent::ApplyBehaviorFacingRotation()
{
	const int32 Facing = int32(BehaviorContext.Attributes[EBhavAttr::Facing]) & 7;
	if (Facing == LastAppliedBehaviorFacing)
	{
		return;
	}
	if (const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner()))
	{
		const FVector WorldDirection = TrafficSystem->GetPeopleFacingWorldDirection(Facing);
		if (!WorldDirection.IsNearlyZero())
		{
			// The original snaps to the 45-degree heading instantly - no turn interpolation.
			SetActorRotation(FRotator(0.0f, WorldDirection.Rotation().Yaw, 0.0f));
			LastAppliedBehaviorFacing = Facing;
		}
	}
}

void ASimCopterGroundAgent::AdvanceBehaviorFigureFrames(int32 TickCount)
{
	if (!bUsingPedestrianFigure || FigureFrameCount <= 1 || TickCount <= 0)
	{
		return;
	}
	FigureCurrentFrame = (FigureCurrentFrame + TickCount) % FigureFrameCount;
	FSimCopterPopulationFigure::ShowFrame(OriginalMeshComponent, FigureFrameCount, FigureCurrentFrame, bFigureHasHeadSection);
}

int32 ASimCopterGroundAgent::GetCurrentTileClass() const
{
	if (const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner()))
	{
		const int32 TileClass = TrafficSystem->GetPeopleTileClassAtWorldLocation(GetActorLocation());
		if (TileClass != INDEX_NONE)
		{
			return TileClass;
		}
	}

	// Standalone/unit-test fallback when no traffic system owns the agent.
	return RouteTargetNodeIndex != INDEX_NONE ? 7 : 10;
}

bool ASimCopterGroundAgent::TryGetCurrentTileCoordinate(int32& OutFileX, int32& OutFileY) const
{
	if (const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner()))
	{
		return TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(GetActorLocation(), OutFileX, OutFileY);
	}

	OutFileX = INDEX_NONE;
	OutFileY = INDEX_NONE;
	return false;
}

bool ASimCopterGroundAgent::IsTileClassAllowedForState(int32 StateIndex, int32 TileClass) const
{
	return FSimCopterBehaviorVM::GetAllowedTileClasses(StateIndex).Contains(TileClass);
}

void ASimCopterGroundAgent::SetInitialBehaviorClass(int32 NewInitialBehaviorClass)
{
	InitialBehaviorClass = FMath::Clamp(NewInitialBehaviorClass, 0, 21);
	// The original binds the figure from the behavior class at spawn (FUN_004c71c0): dogs,
	// cows, Elvis and the everyday street mix all come from this one table.
	PedestrianFigureName = FSimCopterPeopleCityRules::GetFigureNameForBehaviorClass(InitialBehaviorClass);
	if (bBehaviorActive)
	{
		BehaviorContext.Attributes[EBhavAttr::BehaviorClass] = uint16(InitialBehaviorClass);
	}
}

void ASimCopterGroundAgent::SetInitialBehaviorProgramId(int32 NewInitialBehaviorProgramId)
{
	InitialBehaviorProgramId = NewInitialBehaviorProgramId > 0 ? NewInitialBehaviorProgramId : INDEX_NONE;
	if (bBehaviorActive)
	{
		ResetBehaviorProgramOverride();
	}
}

void ASimCopterGroundAgent::SetPedestrianFigureClothesOffset(int32 NewClothesOffset)
{
	ForcedFigureClothesOffset = FMath::Clamp(NewClothesOffset, 0, 13);
	FigureClothesOffset = ForcedFigureClothesOffset;
	if (bUsingPedestrianFigure)
	{
		RebuildFigureClip(FigureMnemonic.IsEmpty() ? TEXT("NoMo") : FigureMnemonic);
	}
}

bool ASimCopterGroundAgent::MoveStep(FSimCopterPersonContext& Context)
{
	// FUN_004c6970: every move tick rebinds the clip from the move result and speed -
	// 0 = NoMo, 1..6 = 1Wal, 7+ = 1Run (dogs/cows remap to DgRn/DgSt in the clip bind).
	// The magnitude comes from the MoveSpeed attribute (+0x164), which the shipped programs
	// assign directly; one tick displaces octantDir * MoveSpeed / 12 original units.
	const int32 MoveSpeed = FMath::Max(0, int32(int16(Context.Attributes[EBhavAttr::MoveSpeed])));
	const TCHAR* SpeedMnemonic = MoveSpeed <= 0 ? TEXT("NoMo") : (MoveSpeed < 7 ? TEXT("1Wal") : TEXT("1Run"));

	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	if (TrafficSystem == nullptr)
	{
		// Standalone/unit-test agents have no city to validate against: accept the move.
		Context.PendingAnimMnemonic = SpeedMnemonic;
		return true;
	}

	if (MoveSpeed <= 0)
	{
		// A zero-speed move succeeds without displacement (result 0, speed 0 -> NoMo).
		Context.PendingAnimMnemonic = SpeedMnemonic;
		BehaviorStepVelocityCmPerSec = FVector::ZeroVector;
		BehaviorStepTimeRemainingSeconds = 0.0f;
		return true;
	}

	const float UnitCm = TrafficSystem->GetPeopleWorldCmPerOriginalUnit();
	const float StepDistanceCm = float(MoveSpeed) / 12.0f * UnitCm;
	const float HalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	const float CurrentFeetZ = GetActorLocation().Z - HalfHeight;
	const float MaxClimbCm = MaxStepClimbOriginalUnits * UnitCm;
	const float MaxDropCm = (MaxStepClimbOriginalUnits + 0.5f) * UnitCm; // -(0x8000 + max) in 16.16

	const int32 StartFacing = int32(Context.Attributes[EBhavAttr::Facing]) & 7;
	// FUN_004c9300 retries clockwise up to 8 extra facings only while +0x16a is set.
	const int32 MaxAttempts = Context.Attributes[EBhavAttr::AutoTurn] != 0 ? 8 : 1;
	const bool bMoveThroughWalls = Context.Attributes[EBhavAttr::MoveThroughWalls] != 0;
	const bool bAmbient = Context.Attributes[EBhavAttr::AmbientFlag] != 0;
	const int32 BehaviorClass = Context.Attributes[EBhavAttr::BehaviorClass];

	int32 LastBlockResult = 0;
	ASimCopterGroundAgent* BumpedPerson = nullptr;
	for (int32 Turn = 0; Turn < MaxAttempts; ++Turn)
	{
		const int32 Facing = (StartFacing + Turn) & 7;
		FVector TargetLocation = FVector::ZeroVector;
		int32 TargetTileClass = INDEX_NONE;
		if (!TrafficSystem->TryGetPeopleFacingStepTarget(GetActorLocation(), Facing, StepDistanceCm, TargetLocation, TargetTileClass))
		{
			LastBlockResult = 3;
			continue;
		}

		// A posted hospital worker turns at the edge of its own roof instead of walking to it and
		// leaning on the containment clamp. Result 3 is the same code the original uses for a tile
		// the walker may not enter, so the retry loop turns them exactly as it would there.
		//
		// An aimless walk is held to a smaller square around the post than one that is on its way to
		// something: BHAV 801's entry walk is meant to happen once and the remake replays it every
		// time it refuses the medic's despawn, which used to march them out to the parapet. See
		// HospitalRoofPostIdleWanderFraction.
		if (!IsWithinHospitalRoofPost(
				TargetLocation,
				bBehaviorStepSeekingSelection ? 1.0f : HospitalRoofPostIdleWanderFraction))
		{
			LastBlockResult = 3;
			continue;
		}

		// FUN_004c9470 asks FUN_004c9000 about the frame's SELECTED object here - before the tile
		// class, before the climb gate, before the bump - and an overlap with it is move result 10,
		// returned without writing the new position. That is the arrival StepTowardSelectedObject is
		// waiting for: walking into what you walked toward stops you against it. It is never the bump
		// below, which is why a paramedic can stand at the casualty (or at the helicopter) instead of
		// circling it forever refusing to occupy the same space, and it is ahead of the tile rules
		// because the thing you were sent to is reachable whatever it happens to be standing on.
		//
		// Deliberately narrower than the original in one way: the original applies this to every move
		// because each walk frame owns its own selection slot, while the remake keeps one slot per
		// person (see ops 67/68), so a stale selection could otherwise stop an unrelated walk dead.
		// And the roof-post containment above stays ahead of it: that is the remake's own guard, and
		// an aircraft parked off the building must not pull a posted medic over the edge.
		if (bBehaviorStepSeekingSelection && IsTouchingSelection(Context, TargetLocation))
		{
			bBehaviorStepTouchedSelection = true;
			Context.Attributes[EBhavAttr::Facing] = uint16(Facing);
			Context.PendingAnimMnemonic = SpeedMnemonic;
			BehaviorStepVelocityCmPerSec = FVector::ZeroVector;
			BehaviorStepTimeRemainingSeconds = 0.0f;
			return true;
		}

		// FUN_004c9470: ambient people (+0x168) may only enter tile classes from their
		// behavior-class row (DAT_0058ec00); result 3 otherwise. Non-ambient movement keeps
		// the pre-VM rows as a safety net (missions steer via goto-object opcodes instead).
		//
		// The exemption is remake-only and is documented on SetIgnoresTileClassRules: the airport
		// is class 1, which no row accepts, and the level-complete band has to be able to walk
		// about on it. The climb gate below still keeps them out of buildings.
		if (bIgnoresTileClassRules)
		{
			// fall through to the climb/surface gates
		}
		else if (bAmbient)
		{
			if (!FSimCopterPeopleCityRules::GetAmbientStateTileClasses(BehaviorClass).Contains(TargetTileClass))
			{
				LastBlockResult = 3;
				continue;
			}
		}
		else if (!IsTileClassAllowedForState(BehaviorClass, TargetTileClass) &&
			!FSimCopterPeopleCityRules::GetAmbientStateTileClasses(BehaviorClass).Contains(TargetTileClass))
		{
			LastBlockResult = 3;
			continue;
		}

		// The max-climb/drop gate against the walked surface (highest geometry at the target
		// column). This - not the tile class - is what stops people at building walls: the
		// surface there is the roof, far above the 5-unit climb allowance.
		//
		// Only the CLIMB arm has BHAV 308's "move through walls" escape. FUN_004c9470 reads
		//     if (maxClimb < rise)      { if (person+0x190 == 0) result = 1; else keep the old Z; }
		//     else if (rise < -0x8000 - maxClimb) { result = 2; }
		// so the drop arm is unconditional: no flag has ever let a person step down off a ledge.
		// The remake wrapped both arms in the flag, and that is how a paramedic walking over to
		// the helicopter - with the flag left set by BHAV 269, or by 308 after four failed moves -
		// walked straight off the edge of the hospital roof.
		//
		// The original's climb escape keeps the walker's existing Z rather than lifting them onto
		// whatever they walked into. The remake has no separate walker Z (the ground snap owns it),
		// so allowing the horizontal step is the whole of that arm here.
		float SurfaceZ = 0.0f;
		if (!TryGetWalkSurfaceZAt(TargetLocation, SurfaceZ))
		{
			// FUN_004c82c0 always answers - it is the max of the cell's object tops and the
			// terrain, and one of those always exists. "I could not find a surface" is a remake
			// state with no original counterpart, so taking the step anyway was an invented hole
			// in the gate. Refuse it and let the retry loop turn, exactly as for a tile the
			// walker may not enter.
			LastBlockResult = 3;
			continue;
		}

		{
			const float Rise = SurfaceZ - CurrentFeetZ;
			if (Rise > MaxClimbCm && !bMoveThroughWalls)
			{
				LastBlockResult = 1; // FaCl recoil
				continue;
			}
			if (Rise < -MaxDropCm)
			{
				LastBlockResult = 2; // Whoa
				continue;
			}
		}

		// FUN_004c9470 step 3: something in the target cell is in the way. Walking into another
		// person is move result 5, which FUN_004c9300's retry loop treats as blocked (it only
		// accepts 0/7/8/10) and which broadcasts reaction 0xd at them from inside the step check.
		if (ASimCopterGroundAgent* Bumped = FindBumpedPedestrian(TargetLocation))
		{
			LastBlockResult = 5;
			BumpedPerson = Bumped;
			continue;
		}

		// Success: renew the constant step velocity until the next behavior tick (the ground
		// snap performs the original's per-step warp onto the walked surface).
		Context.Attributes[EBhavAttr::Facing] = uint16(Facing);
		Context.PendingAnimMnemonic = SpeedMnemonic;
		const FVector FlatDelta(TargetLocation.X - GetActorLocation().X, TargetLocation.Y - GetActorLocation().Y, 0.0f);
		BehaviorStepVelocityCmPerSec = FlatDelta * BehaviorTickRate;
		BehaviorStepTimeRemainingSeconds = 1.25f / FMath::Max(BehaviorTickRate, 1.0f);
		return true;
	}

	// Blocked on every allowed facing: bind the recoil clip for the final attempt's result,
	// exactly like the single post-move call after FUN_004c9300's retry loop.
	BehaviorStepVelocityCmPerSec = FVector::ZeroVector;
	BehaviorStepTimeRemainingSeconds = 0.0f;
	if (LastBlockResult == 5 && BumpedPerson != nullptr)
	{
		// Result 5: the street conversation. Both halves of it - we turn to them and gab, and they
		// get reaction 914, which reaches opcode 80 and gabs back.
		RunBumpedPersonSelector(*BumpedPerson);
		return false;
	}
	Context.PendingAnimMnemonic = LastBlockResult == 1 ? TEXT("FaCl") : (LastBlockResult == 2 ? TEXT("Whoa") : TEXT("NoMo"));
	return false;
}

// SCHOOK: PersonPostMove 0x004c6970, normal move results 0/8.
void ASimCopterGroundAgent::UpdateWalkingVoice(const int32 MoveSpeed)
{
	const int32 VoiceSet = int32(BehaviorContext.Attributes[EBhavAttr::VoiceSet]);
	if (MoveSpeed <= 0)
	{
		StopWalkingVoice();
		return;
	}

	USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this);
	if (Audio == nullptr || FVector::DistSquared(GetActorLocation(), Audio->GetListenerLocation()) >=
		FMath::Square(SimCopterSound::PedestrianFootstepMaxRangeCm))
	{
		StopWalkingVoice();
		return;
	}

	UAudioComponent* WalkingSound = WalkingSoundComponent.Get();
	if (WalkingSound == nullptr || !WalkingSound->IsPlaying())
	{
		WalkingSound = Audio->PlayAttachedVoiceLoop(
			VoiceSet,
			GetRootComponent(),
			SimCopterSound::GetWalkPacedFrequencyHz(MoveSpeed),
			SimCopterSound::PedestrianFootstepMaxRangeCm,
			SimCopterSound::PedestrianFootstepVolumeMultiplier);
		WalkingSoundComponent = WalkingSound;
		return;
	}
	Audio->SetAttachedVoiceLoopFrequencyHz(
		WalkingSound,
		VoiceSet,
		SimCopterSound::GetWalkPacedFrequencyHz(MoveSpeed));
}

void ASimCopterGroundAgent::StopWalkingVoice()
{
	if (UAudioComponent* WalkingSound = WalkingSoundComponent.Get())
	{
		if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
		{
			Audio->StopAttachedVoiceLoop(WalkingSound);
		}
	}
	WalkingSoundComponent.Reset();
}

ASimCopterGroundAgent* ASimCopterGroundAgent::FindBumpedPedestrian(const FVector& StepTargetWorldLocation) const
{
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	if (TrafficSystem == nullptr)
	{
		return nullptr;
	}
	// FUN_004c9000 collides against the other object's own radius (person+0x1c4, the field opcode 27
	// writes). The remake measures with the same physical quantity it already has - the two capsule
	// radii - scaled by opcode 27's ratio, so an agitated rioter still packs to half the spacing.
	const float RadiusScale = BehaviorBodyRadiusUnits / 3.0f;
	const float BumpRadiusCm = FMath::Max(1.0f, GetCollisionRadiusCm() * RadiusScale * 2.0f);
	return TrafficSystem->FindPersonOverlapping(*this, StepTargetWorldLocation, BumpRadiusCm);
}

void ASimCopterGroundAgent::RunBumpedPersonSelector(ASimCopterGroundAgent& Other)
{
	// FUN_004c9470's result-5 side effect, verbatim:
	//   FUN_004c1050(0xd, me, them, -1, person+0x180)
	// - mode 13 is DAT_0058d728[13] = BHAV 914 "Rxn: Person--civil, neutral", and person+0x180
	// (attribute 32) overrides it when set. Cops and paramedics set it to 916.
	FSimCopterInteractionEvent Event;
	Event.Mode = ESimCopterInteractionMode::PersonNeutral;
	Event.Source = this;
	Event.TargetWorldLocation = Other.GetActorLocation();
	Event.MessageIndex = int32(BehaviorContext.Attributes[EBhavAttr::BumpReaction]);
	Other.ApplyInteraction(Event);

	// And FUN_004c6970 case 5 on our own side: face them, then 2Gab or HipH.
	RunMeetSelector(BehaviorContext, &Other);
}

void ASimCopterGroundAgent::SetPersistentHospitalRoofCrew(const bool bPersistent)
{
	bPersistentHospitalRoofCrew = bPersistent;
	if (!bPersistent)
	{
		bHasHospitalRoofPost = false;
	}
}

void ASimCopterGroundAgent::SetHospitalRoofPost(const FVector& RoofCenterWorldLocation, const float HalfExtentCm)
{
	bPersistentHospitalRoofCrew = true;
	HospitalRoofPostWorldLocation = RoofCenterWorldLocation;
	HospitalRoofPostHalfExtentCm = FMath::Max(0.0f, HalfExtentCm);
	bHasHospitalRoofPost = HospitalRoofPostHalfExtentCm > 0.0f;
}

bool ASimCopterGroundAgent::IsWithinRoofPostSquare(
	const FVector& TargetLocation,
	const FVector& CurrentLocation,
	const FVector& PostCenterWorldLocation,
	const float PostHalfExtentCm,
	const float BodyRadiusCm,
	const float ExtentFraction)
{
	if (PostHalfExtentCm <= 0.0f)
	{
		return true;
	}

	auto OffsetFromPost = [&PostCenterWorldLocation](const FVector& Location)
	{
		return FMath::Max(
			FMath::Abs(Location.X - PostCenterWorldLocation.X),
			FMath::Abs(Location.Y - PostCenterWorldLocation.Y));
	};

	// Inset by the body radius so the capsule stays over the roof rather than half off it.
	const float Limit = FMath::Max(
		1.0f,
		PostHalfExtentCm * FMath::Clamp(ExtentFraction, 0.0f, 1.0f) - BodyRadiusCm);
	const float TargetOffset = OffsetFromPost(TargetLocation);
	if (TargetOffset <= Limit)
	{
		return true;
	}

	// Already outside it - which the whole-roof arm allows, and a shove or a reload can produce
	// anyway. Refusing every direction from out here would pin the worker against the parapet, so
	// let anything that heads back toward the middle through.
	return TargetOffset < OffsetFromPost(CurrentLocation);
}

bool ASimCopterGroundAgent::IsWithinHospitalRoofPost(const FVector& WorldLocation, const float ExtentFraction) const
{
	if (!bHasHospitalRoofPost)
	{
		return true;
	}
	return IsWithinRoofPostSquare(
		WorldLocation,
		GetActorLocation(),
		HospitalRoofPostWorldLocation,
		HospitalRoofPostHalfExtentCm,
		CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleRadius() : 0.0f,
		ExtentFraction);
}

ASimCopterGroundAgent::ERoofPostContainment ASimCopterGroundAgent::ClampToHospitalRoofPost(
	const FVector& WorldLocation,
	const FVector& PostCenterWorldLocation,
	const float PostHalfExtentCm,
	const float BodyRadiusCm,
	const float CapsuleHalfHeightCm,
	const float FallToleranceCm,
	FVector& OutContainedLocation)
{
	OutContainedLocation = WorldLocation;
	if (PostHalfExtentCm <= 0.0f)
	{
		return ERoofPostContainment::AtPost;
	}

	// Somebody who has genuinely left - flown off in the cabin and set down across the city - is not
	// "over the edge of their roof", and hauling them back would teleport them across the map. Past
	// roughly a building's width from the post, treat the post as abandoned and let the mission tick
	// staff the roof again instead.
	const float AbandonedDistanceCm = PostHalfExtentCm * 2.0f;
	if (FVector::DistSquared2D(WorldLocation, PostCenterWorldLocation) > FMath::Square(AbandonedDistanceCm))
	{
		return ERoofPostContainment::Abandoned;
	}

	const float Limit = FMath::Max(1.0f, PostHalfExtentCm - BodyRadiusCm);
	FVector Contained(
		FMath::Clamp(WorldLocation.X, PostCenterWorldLocation.X - Limit, PostCenterWorldLocation.X + Limit),
		FMath::Clamp(WorldLocation.Y, PostCenterWorldLocation.Y - Limit, PostCenterWorldLocation.Y + Limit),
		WorldLocation.Z);

	// Putting them back over the roof is not enough once they are already beside the building: the
	// pedestrian ground probe starts at their own feet, so from street level it would find the
	// ground *inside* the building and leave them standing in the lobby. Restore the roof height
	// they were posted at, which is the surface the spawn placed them on.
	if ((Contained.Z - CapsuleHalfHeightCm) < PostCenterWorldLocation.Z - FallToleranceCm)
	{
		Contained.Z = PostCenterWorldLocation.Z + CapsuleHalfHeightCm + 1.0f;
	}

	if (Contained.Equals(WorldLocation, 0.5f))
	{
		return ERoofPostContainment::AtPost;
	}

	OutContainedLocation = Contained;
	return ERoofPostContainment::Contained;
}

bool ASimCopterGroundAgent::ContainToHospitalRoofPost()
{
	// Only while the worker owns its own transform: riding the helicopter, being carried or lying
	// in a mission pose all mean somebody else is placing them.
	if (!bHasHospitalRoofPost || bMissionCarried || bBehaviorMoveSuspended || BehaviorCarrier.IsValid())
	{
		return false;
	}

	FVector Contained = FVector::ZeroVector;
	const ERoofPostContainment Result = ClampToHospitalRoofPost(
		GetActorLocation(),
		HospitalRoofPostWorldLocation,
		HospitalRoofPostHalfExtentCm,
		CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleRadius() : 0.0f,
		CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f,
		HospitalRoofPostFallToleranceCm,
		Contained);

	if (Result == ERoofPostContainment::Abandoned)
	{
		bHasHospitalRoofPost = false;
		return false;
	}
	if (Result == ERoofPostContainment::AtPost)
	{
		return false;
	}

	VerticalVelocityCmPerSec = 0.0f;
	BehaviorStepVelocityCmPerSec = FVector::ZeroVector;
	BehaviorStepTimeRemainingSeconds = 0.0f;
	CurrentVelocityCmPerSec = FVector::ZeroVector;
	ExternalVelocityCmPerSec = FVector::ZeroVector;
	SetActorLocation(Contained, false);
	return true;
}

bool ASimCopterGroundAgent::TryGetWalkSurfaceZAt(const FVector& WorldLocation, float& OutSurfaceZ) const
{
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	if (AgentKind == ESimCopterGroundAgentKind::Vehicle)
	{
		bool bAllowsElevatedMesh = false;
		if (TrafficSystem != nullptr && TrafficSystem->TryGetVehicleRoadSurfaceZ(
			*this, WorldLocation, OutSurfaceZ, bAllowsElevatedMesh))
		{
			if (bAllowsElevatedMesh && GetWorld() != nullptr)
			{
				// Only ramp/elevated-road tile IDs reach this trace. Sampling the actual ramp mesh
				// makes both ground snap and the forward/back pitch probes follow its exact plane.
				// Bridge tiles deliberately stay graph-driven: their composite meshes also contain
				// supports/towers that must never become a vehicle surface.
				const FVector Start(
					WorldLocation.X, WorldLocation.Y, GetActorLocation().Z + GroundProbeUpCm);
				const FVector End(
					WorldLocation.X, WorldLocation.Y, OutSurfaceZ - GroundProbeDistanceCm);
				FHitResult Hit;
				FCollisionQueryParams QueryParams(
					SCENE_QUERY_STAT(SimCopterVehicleRoadDeck), false, this);
				if (GetWorld()->LineTraceSingleByChannel(
						Hit, Start, End, ECC_Camera, QueryParams) && Hit.bBlockingHit)
				{
					const ASimCity2000CityActor* City = TrafficSystem->GetCityActor();
					const bool bBuildingHit = City != nullptr &&
						City->IsBuildingCollisionHit(Hit.GetComponent(), Hit.ImpactPoint);
					const float MeshOffset = Hit.ImpactPoint.Z - OutSurfaceZ;
					if (!bBuildingHit && MeshOffset >= -10.0f &&
						MeshOffset <= VehicleElevatedRoadMeshMaxOffsetCm)
					{
						OutSurfaceZ = Hit.ImpactPoint.Z;
					}
				}
			}
			return true;
		}
		return false;
	}
	if (GetWorld() == nullptr)
	{
		return false;
	}

	// FUN_004c82c0 returns the highest of object tops and terrain at the point, so the probe
	// starts far above any roof and takes the first blocking hit downward. The Camera channel
	// is blocked by city geometry but ignored by agent/player capsules.
	const float StartZ = GetActorLocation().Z + 12000.0f;
	const FVector Start(WorldLocation.X, WorldLocation.Y, StartZ);
	const FVector End(WorldLocation.X, WorldLocation.Y, GetActorLocation().Z - GroundProbeDistanceCm);
	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterWalkSurface), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Camera, QueryParams) && Hit.bBlockingHit)
	{
		OutSurfaceZ = Hit.ImpactPoint.Z;
		return true;
	}

	float TerrainWorldZ = 0.0f;
	if (TrafficSystem != nullptr && TrafficSystem->TryGetTerrainWorldZAtWorldLocation(WorldLocation, TerrainWorldZ))
	{
		OutSurfaceZ = TerrainWorldZ;
		return true;
	}

	return false;
}

bool ASimCopterGroundAgent::IsThreatNearby(const FSimCopterPersonContext& Context) const
{
	// FUN_004c9bc0: the player's helicopter close and low. Original radius DAT_0058dc32 is a
	// few tiles; use ~4 remake tiles horizontally and a low-altitude gate.
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (PlayerPawn == nullptr)
	{
		return false;
	}
	const FVector Delta = PlayerPawn->GetActorLocation() - GetActorLocation();
	return FMath::Abs(Delta.Z) < 1200.0f && Delta.SizeSquared2D() < FMath::Square(1600.0f);
}

bool ASimCopterGroundAgent::TryGetPlayerTileProbe(
	const FSimCopterPersonContext& Context,
	FSimCopterBehaviorPlayerTileProbe& OutProbe) const
{
	(void)Context;
	OutProbe = FSimCopterBehaviorPlayerTileProbe();
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (TrafficSystem == nullptr || PlayerPawn == nullptr)
	{
		return false;
	}

	if (!TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(
		PlayerPawn->GetActorLocation(),
		OutProbe.FileX,
		OutProbe.FileY))
	{
		return false;
	}

	OutProbe.Speed = GetPlayerBehaviorSpeedScalar(*PlayerPawn);
	OutProbe.Facing = uint16(TrafficSystem->GetPeopleStoredFacingFromWorldLocations(GetActorLocation(), PlayerPawn->GetActorLocation()));
	return true;
}

ASimCopterHelicopterPawn* ASimCopterGroundAgent::ResolvePlayerHelicopter() const
{
	// DAT_005040d0+0xa4: the player's airframe, whether or not they are currently in it.
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}
	if (ASimCopterHelicopterPawn* Piloted = Cast<ASimCopterHelicopterPawn>(UGameplayStatics::GetPlayerPawn(World, 0)))
	{
		return Piloted;
	}
	// On foot: the machine they stepped out of is the one parked nearest to them.
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	const FVector From = PlayerPawn != nullptr ? PlayerPawn->GetActorLocation() : GetActorLocation();
	TArray<AActor*> Helicopters;
	UGameplayStatics::GetAllActorsOfClass(World, ASimCopterHelicopterPawn::StaticClass(), Helicopters);
	ASimCopterHelicopterPawn* Best = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();
	for (AActor* Actor : Helicopters)
	{
		ASimCopterHelicopterPawn* Candidate = Cast<ASimCopterHelicopterPawn>(Actor);
		if (Candidate == nullptr)
		{
			continue;
		}
		const float DistanceSq = FVector::DistSquared(From, Candidate->GetActorLocation());
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			Best = Candidate;
		}
	}
	return Best;
}

bool ASimCopterGroundAgent::SelectObjectOfClass(
	FSimCopterPersonContext& Context,
	const int32 ObjectClass,
	int32& OutTileDistance)
{
	// FUN_004cac70's jump table at 0x004cb130. Classes backed by systems the remake actually owns
	// are answered here; the rest fall through to "nothing found", which makes their probe take
	// the false edge exactly as an empty city would.
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	AActor* Found = nullptr;
	// Classes that are a place rather than a thing fill this instead.
	FVector FoundLocation = FVector::ZeroVector;
	bool bFoundLocation = false;
	bool bFoundHarness = false;

	switch (ObjectClass)
	{
	case EBhavObjectClass::MyMissionCoords:
	{
		// FUN_004a88e0 resolves person+0x10a to a live mission record and returns the pointer at
		// record +0x30 when it is not -1: SecondaryX/Y, the destination. BHAV 292 is the only
		// shipped class-0 site; it probes this before performing the passenger's real alight.
		const ASimCopterMissionSystemActor* Missions = Cast<ASimCopterMissionSystemActor>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass()));
		int32 DestinationX = INDEX_NONE;
		int32 DestinationY = INDEX_NONE;
		if (Missions != nullptr &&
			TrafficSystem != nullptr &&
			Missions->TryGetMissionDestinationTile(MissionEventId, DestinationX, DestinationY) &&
			TrafficSystem->TryGetTileCenterWorldLocation(
				DestinationX,
				DestinationY,
				FoundLocation))
		{
			bFoundLocation = true;
		}
		break;
	}
	case EBhavObjectClass::AlreadySelected:
		Found = Context.SelectedObject.Get();
		bFoundLocation = Context.bHasSelection;
		FoundLocation = Context.SelectedLocation;
		break;
	case EBhavObjectClass::PlayerHelicopter:
	{
		// DAT_005040d0+0xa4 is the player's helicopter, and it exists whether or not they are
		// sitting in it - so this must find the airframe, not "whatever body the player is in".
		// BHAV 291 probes it at one tile to decide a transport passenger may climb aboard.
		ASimCopterHelicopterPawn* PlayerHelicopter = ResolvePlayerHelicopter();
		// DIVERGENCE, deliberate: a worker posted on a hospital roof does not notice the aircraft
		// until it is actually over their building. BHAV 263 rec[2] probes ten tiles, which in the
		// remake means the roof crew broke into a run at a helicopter three or four tiles away and
		// then walked into the containment clamp at the edge of their own roof, because that is as
		// far as they are allowed to go. Failing the probe instead leaves them patrolling
		// (801 -> Walk-10, Idle-10) until there is something they can genuinely reach.
		if (PlayerHelicopter != nullptr && IsHelicopterWithinRoofPostAggro(*PlayerHelicopter))
		{
			Found = PlayerHelicopter;
		}
		break;
	}
	case EBhavObjectClass::PlayerSpotlight:
		if (TrafficSystem != nullptr && TrafficSystem->TryGetSpotlightGroundLocation(FoundLocation))
		{
			bFoundLocation = true;
		}
		break;
	case EBhavObjectClass::Harness:
	{
		// The rope end, and only while the harness is the thing on it - a bucket is not something
		// you climb onto. The actor is the helicopter, because that is what a rider ends up
		// attached to; bSelectionIsHarness records which of the two was asked for.
		ASimCopterHelicopterPawn* Helicopter = ResolvePlayerHelicopter();
		FVector RopeEnd = FVector::ZeroVector;
		if (Helicopter != nullptr &&
			Helicopter->IsHarnessRopeEndSelected() &&
			Helicopter->TryGetRopeEndWorldLocation(RopeEnd))
		{
			Found = Helicopter;
			FoundLocation = RopeEnd;
			bFoundLocation = true;
			bFoundHarness = true;
		}
		break;
	}
	case EBhavObjectClass::PlayerAvatar:
		// DAT_00506444 is the player's own person record, which follows them into the cockpit -
		// it is "wherever the player is", not "the on-foot avatar". BHAV 291 probes it at four
		// tiles, which is what makes a waiting transport party walk over to a helicopter that is
		// still in the air and wave at it.
		Found = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
		break;
	case EBhavObjectClass::InteractionSource:
		// person+0x1a4, now that the reaction path records it: whatever last interacted with me,
		// which for a bump is the person who walked into me.
		Found = BehaviorInteractionSource.Get();
		break;
	case EBhavObjectClass::MedevacVictim:
		Found = TrafficSystem != nullptr ? TrafficSystem->FindNearestBehaviorPerson(*this, -2, 6) : nullptr;
		break;
	case EBhavObjectClass::UncaughtCriminal:
		Found = TrafficSystem != nullptr ? TrafficSystem->FindNearestBehaviorPerson(*this, 0, -2) : nullptr;
		break;
	case EBhavObjectClass::PoliceOfficer:
		Found = TrafficSystem != nullptr ? TrafficSystem->FindNearestBehaviorPerson(*this, 1, -2) : nullptr;
		break;
	case EBhavObjectClass::Civilian:
		Found = TrafficSystem != nullptr ? TrafficSystem->FindNearestBehaviorPerson(*this, -2, 0) : nullptr;
		break;
	case EBhavObjectClass::FireTruck:
	case EBhavObjectClass::PoliceCar:
	case EBhavObjectClass::Ambulance:
	case EBhavObjectClass::SpeederCar:
	{
		int32 ServiceIndex = 3; // FUN_0049b060 kinds 3/4 share the speeder pool.
		switch (ObjectClass)
		{
		case EBhavObjectClass::Ambulance:
			ServiceIndex = static_cast<int32>(SimCopterDispatch::EService::Ambulance);
			break;
		case EBhavObjectClass::PoliceCar:
			ServiceIndex = static_cast<int32>(SimCopterDispatch::EService::Police);
			break;
		case EBhavObjectClass::FireTruck:
			ServiceIndex = static_cast<int32>(SimCopterDispatch::EService::FireTruck);
			break;
		default:
			break;
		}
		Found = TrafficSystem != nullptr
			? TrafficSystem->FindNearestServiceVehicleAgent(
				GetActorLocation(), ServiceIndex)
			: nullptr;
		break;
	}
	default:
		break;
	}

	if (Found != nullptr && !bFoundHarness)
	{
		FoundLocation = Found->GetActorLocation();
		bFoundLocation = true;
	}
	if (!bFoundLocation)
	{
		Context.ClearSelection();
		return false;
	}

	// The range the opcode compares is a tile count: the original takes the larger of the two
	// axis deltas between the two stored tile coordinates (0x004cb094).
	int32 MyX = INDEX_NONE;
	int32 MyY = INDEX_NONE;
	int32 TheirX = INDEX_NONE;
	int32 TheirY = INDEX_NONE;
	if (TrafficSystem == nullptr ||
		!TryGetCurrentTileCoordinate(MyX, MyY) ||
		!TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(FoundLocation, TheirX, TheirY))
	{
		Context.ClearSelection();
		return false;
	}

	Context.SelectedObject = Found;
	Context.SelectedLocation = FoundLocation;
	Context.bSelectionIsHarness = bFoundHarness;
	Context.bHasSelection = true;
	OutTileDistance = FMath::Max(FMath::Abs(TheirX - MyX), FMath::Abs(TheirY - MyY));
	return true;
}

bool ASimCopterGroundAgent::EvaluateProximityTest(const FSimCopterPersonContext& Context, const int32 TestIndex) const
{
	// FUN_004caaf0. Case 1 is the only one the shipped criminal and cop programs reach in the
	// remake: |helicopter altitude - the ground height under it| <= 4 original units. Cases 2 and
	// 3 test a carrier the remake does not model for these programs, and case 0 reads a player
	// field that has not been identified.
	if (TestIndex != 1)
	{
		return false;
	}

	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	const ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(
		UGameplayStatics::GetPlayerPawn(GetWorld(), 0));
	if (TrafficSystem == nullptr || Helicopter == nullptr)
	{
		// On foot there is no helicopter to hover, so nothing is being set down on anyone.
		return false;
	}

	const FVector HelicopterLocation = Helicopter->GetActorLocation();
	float TerrainZ = 0.0f;
	if (!TrafficSystem->TryGetTerrainWorldZAtWorldLocation(HelicopterLocation, TerrainZ))
	{
		return false;
	}

	const float GateCm = TrafficSystem->GetPeopleWorldCmPerOriginalUnit() * 4.0f;
	return FMath::Abs(HelicopterLocation.Z - TerrainZ) <= GateCm;
}

bool ASimCopterGroundAgent::FaceSelectedObject(FSimCopterPersonContext& Context)
{
	// FUN_004cb270: facing = (octant toward the object - 2) & 7. The remake's helper already
	// returns the stored octant for a pair of world points, so the -2 rotation is baked in.
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	if (!Context.bHasSelection || TrafficSystem == nullptr)
	{
		return false;
	}

	Context.Attributes[EBhavAttr::Facing] = uint16(
		TrafficSystem->GetPeopleStoredFacingFromWorldLocations(GetActorLocation(), Context.SelectedLocation) & 7);
	return true;
}

bool ASimCopterGroundAgent::TryGetBehaviorFacingOctantToward(
	const FVector& TargetWorldLocation,
	int32& OutOctant) const
{
	OutOctant = 0;
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	if (TrafficSystem == nullptr)
	{
		return false;
	}
	// FUN_004c8430 answers -1 when the two points share a position, which is the arm ops 31/32/33
	// treat as failure. A few centimetres is the same thing at this scale.
	if (FVector::DistSquared2D(GetActorLocation(), TargetWorldLocation) < FMath::Square(1.0f))
	{
		return false;
	}
	OutOctant = TrafficSystem->GetPeopleStoredFacingFromWorldLocations(GetActorLocation(), TargetWorldLocation) & 7;
	return true;
}

bool ASimCopterGroundAgent::FaceAwayFromSelectedObject(FSimCopterPersonContext& Context)
{
	// FUN_004cc240: facing = (bearing + 2) & 7, which is the op-18 facing turned 180 degrees. No
	// selection at all is a successful no-op; a selection we have no bearing to fails.
	if (!Context.bHasSelection)
	{
		return true;
	}
	int32 Octant = 0;
	if (!TryGetBehaviorFacingOctantToward(Context.SelectedLocation, Octant))
	{
		return false;
	}
	Context.Attributes[EBhavAttr::Facing] = uint16((Octant + 4) & 7);
	return true;
}

bool ASimCopterGroundAgent::FaceInteractionSource(FSimCopterPersonContext& Context, const bool bFaceToward)
{
	// FUN_004cc2b0 against person+0x1a4. Token 0x21 faces toward it, 0x20 away; no source is a
	// successful no-op, and a source with no bearing takes a random facing and fails.
	const AActor* Source = BehaviorInteractionSource.Get();
	if (Source == nullptr)
	{
		return true;
	}
	int32 Octant = 0;
	if (!TryGetBehaviorFacingOctantToward(Source->GetActorLocation(), Octant))
	{
		Context.Attributes[EBhavAttr::Facing] = Context.RandomBounded(8);
		return false;
	}
	Context.Attributes[EBhavAttr::Facing] = uint16(bFaceToward ? Octant : ((Octant + 4) & 7));
	return true;
}

void ASimCopterGroundAgent::ReactToInteractionSource(FSimCopterPersonContext& Context)
{
	// FUN_004cb790 -> FUN_004c6970(movespeed, source is a person ? 5 : 4, source).
	RunMeetSelector(Context, BehaviorInteractionSource.Get());
}

void ASimCopterGroundAgent::RunMeetSelector(FSimCopterPersonContext& Context, AActor* Source)
{
	// FUN_004c6970's two person-contact arms: result 5 is the street conversation, result 4 the
	// "Whoa" a person gives an object that shoved them. The object comes in as a parameter because
	// the bumper's own person+0x1a4 is not written by a bump - only the bumped person's is.
	if (Source == nullptr)
	{
		return;
	}

	ASimCopterGroundAgent* OtherPerson = Cast<ASimCopterGroundAgent>(Source);
	if (OtherPerson != nullptr && OtherPerson->AgentKind == ESimCopterGroundAgentKind::Pedestrian)
	{
		int32 Octant = 0;
		if (TryGetBehaviorFacingOctantToward(Source->GetActorLocation(), Octant))
		{
			Context.Attributes[EBhavAttr::Facing] = uint16(Octant);
		}
		// FUN_004c6970 case 5: 50/50 between the two conversation clips, then one of nine voice
		// lines. The line is rolled with FUN_004cea00(9) and mapped through the switch at
		// 0x004c6b3a - which is NOT the identity, and not sorted: cases 0..8 pick voice events
		// 10, 1, 4, 11, 2, 18, 6, 7, 3. Both halves matter; the animation on its own is a mime.
		Context.PendingAnimMnemonic = Context.RandomBounded(2) == 0 ? TEXT("2Gab") : TEXT("HipH");
		static constexpr int32 ChatVoiceEvents[9] = { 10, 1, 4, 11, 2, 18, 6, 7, 3 };
		const int32 Roll = FMath::Clamp(int32(Context.RandomBounded(9)), 0, 8);
		PlayPersonVoiceEvent(ChatVoiceEvents[Roll], /*bAllocateSlot=*/true, /*bNonPositional=*/false, /*bForce=*/false);
		return;
	}

	// Case 4: shoved by an object rather than met by a person - "Whoa" plus voice event 0x2a.
	Context.PendingAnimMnemonic = TEXT("Whoa");
	PlayPersonVoiceEvent(0x2a, /*bAllocateSlot=*/true, /*bNonPositional=*/false, /*bForce=*/false);
}

ESimCopterBehaviorStepResult ASimCopterGroundAgent::StepTowardSelectedObject(FSimCopterPersonContext& Context)
{
	// FUN_004ca940: turn to face the object, take one ordinary move step, and report arrival on
	// move result 10 with under 5 original units of vertical separation.
	//
	// Result 10 is CONTACT, not tile co-occupancy. FUN_004c9470 asks FUN_004c9000 what the walker's
	// body would overlap at the step target; FUN_004c9000 tests the frame's *selected* object first
	// and FUN_004c8f70 answers with a box overlap of the two bodies' own extents (the walker's
	// person+0x1c4 radius against the object's +0x10 radius). Only that returns 10 - and it returns
	// before the position is written, so the walker stops against what it walked up to.
	//
	// The remake used to accept "we are on the same tile". A tile is 400 cm: a paramedic entering
	// the far corner of the helicopter's tile was declared to have arrived and reached into the
	// cabin from nearly three metres away, which is what "the medics never walk up to the
	// helicopter" was. Contact is now measured against the aircraft's own airframe box.
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	if (!Context.bHasSelection || TrafficSystem == nullptr)
	{
		return ESimCopterBehaviorStepResult::NoTarget;
	}

	// A moving target (and the spotlight is the most mobile of the lot) has to be re-read every
	// step or the chase walks to where it used to be.
	if (const AActor* Target = Context.SelectedObject.Get())
	{
		if (Context.bSelectionIsHarness)
		{
			// Walk to the rope end, not to the airframe hanging above it.
			const ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(Target);
			FVector RopeEnd = FVector::ZeroVector;
			if (Helicopter == nullptr || !Helicopter->TryGetRopeEndWorldLocation(RopeEnd))
			{
				Context.ClearSelection();
				return ESimCopterBehaviorStepResult::NoTarget;
			}
			Context.SelectedLocation = RopeEnd;
		}
		else
		{
			Context.SelectedLocation = Target->GetActorLocation();
		}
	}
	else if (!TrafficSystem->TryGetSpotlightGroundLocation(Context.SelectedLocation))
	{
		// The only location-only class is the spotlight; once it is off there is nothing to walk to.
		Context.ClearSelection();
		return ESimCopterBehaviorStepResult::NoTarget;
	}

	// Both ends still have to be somewhere the city knows about; the original's walker cannot leave
	// the tile grid at all, and neither end having a tile is a remake-only state.
	int32 MyX = INDEX_NONE;
	int32 MyY = INDEX_NONE;
	int32 TheirX = INDEX_NONE;
	int32 TheirY = INDEX_NONE;
	if (!TryGetCurrentTileCoordinate(MyX, MyY) ||
		!TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(Context.SelectedLocation, TheirX, TheirY))
	{
		return ESimCopterBehaviorStepResult::NoTarget;
	}

	// The original measures its vertical gate between two ground-referenced positions; the remake
	// keeps an actor origin mid-body, so the comparison has to be feet-to-doorsill or a landed
	// helicopter is permanently "too high" to climb into - 5 units is only about 31 cm.
	const float HeightGateCm = TrafficSystem->GetPeopleWorldCmPerOriginalUnit() * 5.0f;
	float TargetReferenceZ = Context.SelectedLocation.Z;
	if (!Context.bSelectionIsHarness)
	{
		if (const AActor* TargetActor = Context.SelectedObject.Get())
		{
			TargetReferenceZ -= TargetActor->GetSimpleCollisionHalfHeight();
		}
	}
	const float MyFeetZ = GetActorLocation().Z - GetCapsuleHalfHeightCm();
	const bool bHeightGatePassed = FMath::Abs(TargetReferenceZ - MyFeetZ) < HeightGateCm;

	// Already touching it - the step that made contact refused to displace us, exactly as
	// FUN_004c9470 does when it returns 10.
	if (bHeightGatePassed && IsTouchingSelection(Context, GetActorLocation()))
	{
		return ESimCopterBehaviorStepResult::Arrived;
	}

	FaceSelectedObject(Context);
	bBehaviorStepTouchedSelection = false;
	bBehaviorStepSeekingSelection = true;
	MoveStep(Context);
	bBehaviorStepSeekingSelection = false;
	if (bHeightGatePassed && bBehaviorStepTouchedSelection)
	{
		return ESimCopterBehaviorStepResult::Arrived;
	}
	return ESimCopterBehaviorStepResult::Moving;
}

float ASimCopterGroundAgent::ComputeContactGapCm(
	const FVector& FromWorldLocation,
	const FVector& TargetWorldLocation,
	const float MyRadiusCm,
	const float TargetRadiusCm)
{
	return static_cast<float>(FVector::Dist2D(FromWorldLocation, TargetWorldLocation)) -
		FMath::Max(0.0f, MyRadiusCm) -
		FMath::Max(0.0f, TargetRadiusCm);
}

float ASimCopterGroundAgent::GetSelectionContactGapCm(
	const FSimCopterPersonContext& Context,
	const FVector& FromWorldLocation) const
{
	// FUN_004c8f70's box overlap in remake terms: the gap between this walker's body and the
	// selection's own extent, measured across the deck (StepTowardSelectedObject owns the vertical
	// gate). Negative or zero means the two bodies are in contact.
	const float MyRadiusCm = FMath::Max(1.0f, GetCollisionRadiusCm());
	const AActor* Target = Context.SelectedObject.Get();

	// The helicopter's collision capsule is a 95 cm sphere sized for the flight impact sweep, so
	// GetSimpleCollisionRadius would put contact a metre out from a fuselage a fraction of that
	// across. Measure against the airframe the player can actually see. (Not for a harness
	// selection: there the selected actor is still the helicopter but the target is its rope end.)
	const ASimCopterHelicopterPawn* Helicopter =
		Context.bSelectionIsHarness ? nullptr : Cast<ASimCopterHelicopterPawn>(Target);
	if (Helicopter != nullptr)
	{
		return Helicopter->GetDistanceToAirframeCm(FromWorldLocation, /*bHorizontalOnly=*/true) -
			MyRadiusCm;
	}

	// A vehicle gets the same treatment for the same reason. FUN_004c8f70 overlaps the object's own
	// +0x10 extent, and a car's extent is the car - but the remake's vehicle capsule is a 33.75 cm
	// radius chosen for traffic separation, which is *narrower than the car is long*. A medic
	// walking up to the nose or tail of its ambulance could be standing in the bodywork and still
	// measure a positive gap, so BHAV 262's return walk never reported arrival and the handoff at
	// BHAV 275 was never reached. Measure the rendered body instead.
	if (const ASimCopterGroundAgent* Vehicle = Cast<ASimCopterGroundAgent>(Target))
	{
		if (Vehicle->GetAgentKind() == ESimCopterGroundAgentKind::Vehicle)
		{
			return Vehicle->GetDistanceToBodyCm(FromWorldLocation) - MyRadiusCm;
		}
	}

	// Everyone else keeps the original's cube-vs-cube with the extent the remake already has for
	// them: their own capsule radius.
	const float TargetRadiusCm = Target != nullptr ? Target->GetSimpleCollisionRadius() : 0.0f;
	return ComputeContactGapCm(FromWorldLocation, Context.SelectedLocation, MyRadiusCm, TargetRadiusCm);
}

float ASimCopterGroundAgent::ComputeBodyGapCm(
	const FBox& LocalBoundsCm,
	const FTransform& BodyFrame,
	const FVector& WorldLocation)
{
	if (LocalBoundsCm.IsValid == 0)
	{
		return 0.0f;
	}
	FVector LocalPoint = BodyFrame.InverseTransformPosition(WorldLocation);
	// Across the deck only: the caller owns the vertical gate, exactly as for the airframe.
	LocalPoint.Z = FMath::Clamp(LocalPoint.Z, LocalBoundsCm.Min.Z, LocalBoundsCm.Max.Z);
	return static_cast<float>(FMath::Sqrt(LocalBoundsCm.ComputeSquaredDistanceToPoint(LocalPoint)));
}

float ASimCopterGroundAgent::GetDistanceToBodyCm(const FVector& WorldLocation) const
{
	const USceneComponent* VisibleBody =
		bUsingOriginalMesh
			? static_cast<const USceneComponent*>(OriginalMeshComponent.Get())
			: static_cast<const USceneComponent*>(ProxyMeshComponent.Get());

	if (VisibleBody != nullptr)
	{
		// Measured through the component's own relative transform, so the box is already in the
		// actor's frame and turns with it.
		const FBoxSphereBounds BodyBounds = VisibleBody->CalcBounds(VisibleBody->GetRelativeTransform());
		if (BodyBounds.SphereRadius > UE_SMALL_NUMBER && !BodyBounds.BoxExtent.ContainsNaN())
		{
			const FBox LocalBounds = BodyBounds.GetBox();
			if (LocalBounds.IsValid != 0)
			{
				return ComputeBodyGapCm(LocalBounds, GetActorTransform(), WorldLocation);
			}
		}
	}

	// No mesh built yet (a headless test, or the frame before the GEO packs load). Fall back to
	// the collision capsule so the answer is still a gap to a body rather than to a point.
	const float Distance = static_cast<float>(FVector::Dist2D(WorldLocation, GetActorLocation()));
	return FMath::Max(0.0f, Distance - GetCollisionRadiusCm());
}

bool ASimCopterGroundAgent::ApplyHelicopterRunOver(ASimCopterHelicopterPawn& Helicopter)
{
	// The interaction half is the original's, unaltered: FUN_0049a4f0(0xc, ...) routes an airframe
	// contact to the person reaction table, whose entry 12 is BHAV 912 "Rxn: Large fast vehicle hit"
	// -> 903 "Rxn: Die" -> 309 "Fall off master". 903 plays the death sounds, posts outcome 10
	// (EVT_PersonDied), sets attr15 "written off" and despawns them, so nothing about dying needs to
	// be invented here.
	if (bRunOverByHelicopter)
	{
		return false; // one airframe, one death; the contact test is true for several frames
	}

	FSimCopterInteractionEvent Event;
	Event.Mode = 12; // DAT_0058d728[12] - the large-fast-vehicle arm
	Event.Source = &Helicopter;
	Event.TargetWorldLocation = GetActorLocation();
	Event.MissionEventId = MissionEventId;
	if (!ApplyInteraction(Event))
	{
		return false;
	}
	bRunOverByHelicopter = true;

	// DIVERGENCE: outcome 9 (EVT_CriminalCaught) as well. The executable has no way to end a crime
	// mission with the helicopter, so running the criminal down would otherwise kill the target and
	// leave the job open forever with nobody left to catch. Squashing the one you were sent for is
	// the job done.
	PostMissionOutcome(BehaviorContext, 9);
	return true;
}

bool ASimCopterGroundAgent::IsWithinRoofPostAggro(
	const FVector& HelicopterWorldLocation,
	const FVector& PostCenterWorldLocation,
	const float PostHalfExtentCm,
	const float MarginCm)
{
	// "Over the building" in the only terms the post has: its own square, widened by a margin so a
	// pilot who parks with the skids just past the parapet is still served. Height is deliberately
	// not tested - the crew should be walking to the pad while the aircraft is still coming down.
	const float LimitCm = FMath::Max(0.0f, PostHalfExtentCm) + FMath::Max(0.0f, MarginCm);
	const FVector Delta = HelicopterWorldLocation - PostCenterWorldLocation;
	return FMath::Abs(Delta.X) <= LimitCm && FMath::Abs(Delta.Y) <= LimitCm;
}

bool ASimCopterGroundAgent::IsWithinHandoffReach(
	const float AirframeGapCm,
	const float MyRadiusCm,
	const float ReachCm,
	const float DoorsillWorldZ,
	const float FeetWorldZ,
	const float MaxVerticalCm)
{
	const float AllowedCm = FMath::Max(1.0f, MyRadiusCm) + FMath::Max(0.0f, ReachCm);
	return AirframeGapCm <= AllowedCm &&
		FMath::Abs(DoorsillWorldZ - FeetWorldZ) <= FMath::Max(0.0f, MaxVerticalCm);
}

bool ASimCopterGroundAgent::IsHelicopterWithinRoofPostAggro(const ASimCopterHelicopterPawn& Helicopter) const
{
	// Only a posted worker is restricted; everybody else keeps the shipped ten-tile probe.
	if (!bHasHospitalRoofPost)
	{
		return true;
	}
	return IsWithinRoofPostAggro(
		Helicopter.GetActorLocation(),
		HospitalRoofPostWorldLocation,
		HospitalRoofPostHalfExtentCm,
		HospitalRoofPostAggroMarginCm);
}

bool ASimCopterGroundAgent::IsAtHelicopterForHandoff(const ASimCopterHelicopterPawn& Helicopter) const
{
	// Riding it counts, and has to: a medic who climbed aboard unloads a patient in flight, which is
	// BHAV 263 rec[1] -> 29 -> 31 -> 3 and is meant to work.
	if (BehaviorCarrier.Get() == &Helicopter)
	{
		return true;
	}

	// Across the deck this is skin contact plus a hand's reach. Vertically it is a window rather
	// than contact, because taking a casualty out of a helicopter that is still hovering low over
	// the pad is something the game should allow - what it must not allow is doing it from the far
	// side of the roof, or from the ground under an aircraft in the air.
	//
	// The doorsill is measured the same way StepTowardSelectedObject's vertical gate does it.
	return IsWithinHandoffReach(
		Helicopter.GetDistanceToAirframeCm(GetActorLocation(), /*bHorizontalOnly=*/true),
		GetCollisionRadiusCm(),
		HelicopterHandoffReachCm,
		Helicopter.GetActorLocation().Z - Helicopter.GetSimpleCollisionHalfHeight(),
		GetActorLocation().Z - GetCapsuleHalfHeightCm(),
		HelicopterHandoffMaxVerticalCm);
}

bool ASimCopterGroundAgent::IsTouchingSelection(
	const FSimCopterPersonContext& Context,
	const FVector& FromWorldLocation) const
{
	if (!Context.bHasSelection)
	{
		return false;
	}

	// Two selections have no body to make contact with. The rope end is a point hanging in the air,
	// and the spotlight's ground spot is a location with no object at all - which the original never
	// walks to in the first place, since FUN_004ca940 refuses a frame whose selection slot is not an
	// object. Both keep the older whole-tile acceptance rather than being given an invented radius.
	if (Context.bSelectionIsHarness || Context.SelectedObject.Get() == nullptr)
	{
		const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
		int32 MyX = INDEX_NONE;
		int32 MyY = INDEX_NONE;
		int32 TheirX = INDEX_NONE;
		int32 TheirY = INDEX_NONE;
		return TrafficSystem != nullptr &&
			TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(FromWorldLocation, MyX, MyY) &&
			TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(Context.SelectedLocation, TheirX, TheirY) &&
			MyX == TheirX && MyY == TheirY;
	}

	return GetSelectionContactGapCm(Context, FromWorldLocation) <= 0.0f;
}

bool ASimCopterGroundAgent::PushReactionOnSelectedObject(FSimCopterPersonContext& Context, const int32 ProgramId)
{
	ASimCopterGroundAgent* Target = Cast<ASimCopterGroundAgent>(Context.SelectedObject.Get());
	return Target != nullptr && Target->PushBehaviorReaction(ProgramId);
}

bool ASimCopterGroundAgent::PushBehaviorReaction(const int32 ProgramId)
{
	if (!bBehaviorActive || !BehaviorModel.IsValid() || BehaviorModel->FindProgram(ProgramId) == nullptr)
	{
		return false;
	}
	return BehaviorContext.PushReactionProgram(ProgramId);
}

void ASimCopterGroundAgent::PostMissionOutcome(FSimCopterPersonContext& Context, const int32 OutcomeCode)
{
	// FUN_004ccf50: the person's program reports an outcome, and this maps it onto one of the
	// mission event codes and posts it against the record the person belongs to (person+0x10a).
	if (MissionEventId == INDEX_NONE)
	{
		return;
	}

	int32 EventCode = INDEX_NONE;
	bool bCarriesCoordinates = false;
	switch (OutcomeCode)
	{
	case 0: EventCode = SimCopterMissions::EVT_VictimPickedUp; break;
	case 1:
		// Which "delivered" event depends on what kind of person this is (person+0x148).
		switch (int32(Context.Attributes[EBhavAttr::State]))
		{
		case 1: case 2: case 0x13: EventCode = SimCopterMissions::EVT_RescueDelivered; break;
		case 3:                    EventCode = SimCopterMissions::EVT_RioterCalmed; break;
		case 4:                    EventCode = SimCopterMissions::EVT_TransportDelivered; break;
		case 6:                    EventCode = SimCopterMissions::EVT_MedevacDelivered; break;
		default: break;
		}
		break;
	case 2: EventCode = SimCopterMissions::EVT_SetTertiaryCoords; bCarriesCoordinates = true; break;
	case 4: EventCode = SimCopterMissions::EVT_RioterDispersed; break;
	case 5: EventCode = SimCopterMissions::EVT_RioterCalmed; break;
	// The one that makes a criminal's marker follow them: every loop of every criminal program
	// re-posts the person's own tile as the mission's primary coordinates.
	case 6: EventCode = SimCopterMissions::EVT_SetPrimaryCoords; bCarriesCoordinates = true; break;
	case 7: EventCode = SimCopterMissions::EVT_MedevacDelivered; break;
	case 8: EventCode = SimCopterMissions::EVT_VictimPickedUp; break;
	case 9: EventCode = SimCopterMissions::EVT_CriminalCaught; break;
	case 10: EventCode = SimCopterMissions::EVT_PersonDied; break;
	case 11: EventCode = SimCopterMissions::EVT_PassengerLost; break;
	default: break;
	}

	if (EventCode == INDEX_NONE)
	{
		return;
	}

	ASimCopterMissionSystemActor* Missions = Cast<ASimCopterMissionSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass()));
	if (Missions == nullptr)
	{
		return;
	}

	// Passenger actions are engine-owned, idempotent services. The decoded program decides *when*
	// to request one, but it does not write counters directly: the same real person may also pass
	// through an on-foot pickup, the seat window, or the mission recovery loop in the same frame.
	const int32 PersonState = int32(Context.Attributes[EBhavAttr::State]);
	const bool bPassengerState =
		PersonState == 1 || PersonState == 2 || PersonState == 4 ||
		PersonState == 6 || PersonState == 0x13;
	if ((OutcomeCode == 0 || OutcomeCode == 8) && bPassengerState)
	{
		Missions->NotifyMissionPersonBoarded(this);
		return;
	}
	if ((OutcomeCode == 1 && bPassengerState) || OutcomeCode == 7)
	{
		Missions->NotifyMissionPersonDelivered(this);
		return;
	}
	if (OutcomeCode == 10 && bPassengerState)
	{
		Missions->NotifyMissionPersonDied(this);
		return;
	}

	if (bCarriesCoordinates)
	{
		int32 TileX = INDEX_NONE;
		int32 TileY = INDEX_NONE;
		if (!TryGetCurrentTileCoordinate(TileX, TileY))
		{
			return;
		}
		Missions->PostMissionEventAt(EventCode, MissionEventId, TileX, TileY, 0, true);
		return;
	}

	Missions->PostMissionEvent(EventCode, MissionEventId, 1, false);
}

int32 ASimCopterGroundAgent::GetCurrentTileBuildingId() const
{
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	int32 FileX = INDEX_NONE;
	int32 FileY = INDEX_NONE;
	if (TrafficSystem == nullptr || !TryGetCurrentTileCoordinate(FileX, FileY))
	{
		return INDEX_NONE;
	}
	return TrafficSystem->GetXbldTileId(FileX, FileY);
}

bool ASimCopterGroundAgent::IsCurrentTileServiceable() const
{
	// FUN_004ccc40 = FUN_004c9cc0 (is anything on this tile) && FUN_004c9dc0(tile class). The
	// remake answers the second half only: the walkable classes an on-foot crew member can stand
	// and work on, which is the part the paramedic program branches on.
	const int32 TileClass = GetCurrentTileClass();
	return TileClass == 7 || TileClass == 10 || TileClass == 11 || TileClass == 12 || TileClass == 13;
}

bool ASimCopterGroundAgent::IsRidingCarrier(const FSimCopterPersonContext& Context) const
{
	// person+0x1a0. Either the VM put them on something or the mission layer did.
	return BehaviorCarrier.IsValid() || bMissionCarried;
}

bool ASimCopterGroundAgent::CanAlightHere() const
{
	// Cabin entry/exit already has a proven helicopter-side landing gate. Use that service as the
	// definitive answer instead of reconstructing landing state from the hidden passenger actor's
	// attachment transform. The tile check below still prevents rescue delivery onto open water.
	const ASimCopterHelicopterPawn* CabinHelicopter =
		bClaimedPassengerSeat && !bRidingHarness
			? Cast<ASimCopterHelicopterPawn>(BehaviorCarrier.Get())
			: nullptr;
	if (CabinHelicopter != nullptr && !CabinHelicopter->CanTransferMissionPassengers())
	{
		return false;
	}
	// FUN_004c9bc0: the tile has to be one people may occupy, and the person has to be within
	// 6 original units of the ground under them. That second half is what stops a passenger
	// stepping out of a helicopter at altitude.
	//
	// The tile half is deliberately broad: anywhere that is not open water. Restricting it to the
	// walkable pedestrian classes meant a helicopter set down on a helipad, a roof or a road
	// shoulder failed the test and nobody could ever get out - which is what stranded the train
	// survivors aboard.
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	int32 TileX = INDEX_NONE;
	int32 TileY = INDEX_NONE;
	if (TrafficSystem != nullptr &&
		TryGetCurrentTileCoordinate(TileX, TileY) &&
		TrafficSystem->IsWaterTile(TileX, TileY))
	{
		return false;
	}

	if (TrafficSystem == nullptr)
	{
		return true;
	}

	if (CabinHelicopter != nullptr)
	{
		return true;
	}

	float SurfaceZ = 0.0f;
	const FVector Location = GetActorLocation();
	if (!TryGetWalkSurfaceZAt(Location, SurfaceZ) &&
		!TrafficSystem->TryGetTerrainWorldZAtWorldLocation(Location, SurfaceZ))
	{
		return false;
	}

	const float HeightCm = Location.Z - GetCapsuleHalfHeightCm() - SurfaceZ;
	return HeightCm < TrafficSystem->GetPeopleWorldCmPerOriginalUnit() * 6.0f;
}

bool ASimCopterGroundAgent::TryAlightHere()
{
	if (!CanAlightHere())
	{
		return false;
	}
	if (BehaviorCarrier.IsValid() || bBehaviorMoveSuspended)
	{
		AlightFromCarrier();
	}
	return true;
}

ESimCopterMissionPassengerKind ASimCopterGroundAgent::GetMissionPassengerKind() const
{
	// The same split FUN_004ccf50 case 1 makes on person+0x148 when it decides which "delivered"
	// event a person is worth: states 1/2/0x13 are rescues, 4 is a transport fare, 6 a medevac.
	switch (int32(BehaviorContext.Attributes[EBhavAttr::State]))
	{
	case 4:  return ESimCopterMissionPassengerKind::Transport;
	case 6:  return ESimCopterMissionPassengerKind::Medevac;
	default: return ESimCopterMissionPassengerKind::Rescue;
	}
}

bool ASimCopterGroundAgent::IsMedevacVictim() const
{
	return AgentKind == ESimCopterGroundAgentKind::Pedestrian &&
		int32(BehaviorContext.Attributes[EBhavAttr::State]) == 6;
}

bool ASimCopterGroundAgent::PrepareForPlayerCausedMedevac()
{
	if (AgentKind != ESimCopterGroundAgentKind::Pedestrian ||
		IsMedevacVictim() ||
		bMissionCarried ||
		bClaimedPassengerSeat ||
		BehaviorCarrier.IsValid())
	{
		return false;
	}

	// SCHOOK: PersonCollapsesIntoCasualty 0x004c9b50. The original reports death against the
	// person's current record before replacing person+0x10a with the fresh medevac record. The
	// Apache missile makes the same conversion synchronously, so it must retain that first half
	// or a struck rioter/rescue victim leaves their former mission waiting forever.
	if (MissionEventId != INDEX_NONE && !bMissionResolutionReported)
	{
		PostMissionOutcome(BehaviorContext, 10);
	}
	BehaviorContext.Attributes[EBhavAttr::Speed] = 0;
	return true;
}

bool ASimCopterGroundAgent::IsEmergencyCrewPersonState(const int32 PersonState)
{
	// 5 is the ambulance's Medik (FUN_004bd980(0x0c, 5)); 7/8 are the loop-flag-1 police states
	// FUN_004ca350 searches for, and 0xe is BHAV 1402's speeder cop. Every passenger state a
	// mission scores - 1/2/4/6/0x13 - is deliberately absent.
	switch (PersonState)
	{
	case 5: case 7: case 8: case 0xe: return true;
	default:                          return false;
	}
}

bool ASimCopterGroundAgent::IsEmergencyCrewMember() const
{
	return IsEmergencyCrewPersonState(int32(BehaviorContext.Attributes[EBhavAttr::State]));
}

bool ASimCopterGroundAgent::BoardCarrier(
	AActor* NewCarrier,
	const bool bAsHarnessRider,
	const bool bAllowAirborneCabinTransfer,
	const bool bAsCarriedBody)
{
	if (NewCarrier == nullptr || NewCarrier == this)
	{
		return false;
	}
	if (BehaviorCarrier.Get() == NewCarrier && bRidingHarness == bAsHarnessRider)
	{
		return true;
	}

	ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(NewCarrier);
	if (Helicopter != nullptr && bAsHarnessRider)
	{
		// A harness pickup is only useful when winding the rider in can actually claim a cabin
		// seat. Refuse the action up front instead of stranding or dropping somebody at the top
		// of the rope when op 58 attempts the transfer.
		if (Helicopter->GetAvailablePassengerSeats() <= 0)
		{
			return false;
		}

		// One person on the hook. The harness is a single sling in the original and a queue of
		// survivors all riding the same rope end is nonsense; whoever grabs it first has it until
		// they are lifted into the cabin or let go.
		if (const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner()))
		{
			if (TrafficSystem->FindHarnessRider(Helicopter, this) != nullptr)
			{
				return false;
			}
		}
	}
	if (Helicopter != nullptr && !bAsHarnessRider)
	{
		if (int32(BehaviorContext.Attributes[EBhavAttr::State]) == 5 &&
			MissionEventId == INDEX_NONE)
		{
			// Shipped BHAV 263 first looks for a medevac patient aboard. Only its no-patient arm
			// calls BHAV 269, which may select the player's helicopter as the medic's ride. Once
			// BHAV 263 has removed the last patient, that same test is
			// also true, so the stable action boundary must distinguish "go help retrieve one"
			// from "the delivery just finished."
			const ASimCopterMissionSystemActor* Missions = Cast<ASimCopterMissionSystemActor>(
				UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass()));
			if (Missions == nullptr ||
				!Missions->CanHospitalParamedicBoardPlayerHelicopter(Helicopter))
			{
				return false;
			}
		}

		// Normal cabin boarding goes through opcode 12's own height band, which is the looser of the
		// original's two - climbing in tolerates a lower hover than stepping out does. The one
		// exception is the decoded op-58 harness-to-cabin transition: winding the rope in is supposed
		// to bring its rider aboard while airborne.
		if ((!bAllowAirborneCabinTransfer && !Helicopter->CanBoardMissionPassengers()) ||
			Helicopter->GetAvailablePassengerSeats() <= 0)
		{
			return false;
		}
	}

	// Relinquish the old carrier before taking the new one. This is deliberately attachment-only:
	// snapping a harness rider to the ground for the instant it transfers into the cabin produces
	// a visible teleport and can select a roof far below it.
	if (AActor* PreviousCarrier = BehaviorCarrier.Get())
	{
		if (bClaimedPassengerSeat)
		{
			if (ASimCopterHelicopterPawn* PreviousHelicopter = Cast<ASimCopterHelicopterPawn>(PreviousCarrier))
			{
				PreviousHelicopter->RemoveMissionPassengersForMission(
					1, MissionEventId, GetMissionPassengerKind(), this);
			}
			bClaimedPassengerSeat = false;
		}
		AlightAttachmentOnly();
		BehaviorCarrier.Reset();
		bRidingHarness = false;
	}
	else
	{
		// The older on-foot carry path attaches directly to a scene component and records no
		// BehaviorCarrier. Detach that real person; never destroy it and replace it with a slot.
		AlightAttachmentOnly();
	}

	if (Helicopter != nullptr && !bAsHarnessRider)
	{
		// FUN_004c6250 copies person+0x18e into the record and seats the face at 1, so the seat
		// window shows this passenger's own head from the moment they climb in.
		SeatPortraitMood = 1;
		if (Helicopter->AddMissionPassengersForMission(
				1, MissionEventId, GetMissionPassengerKind(), this) <= 0)
		{
			return false;
		}
		bClaimedPassengerSeat = true;
		SetActorHiddenInGame(true);
	}
	else
	{
		// Getting into a vehicle puts you inside it. Only a toted body stays on show, because the
		// carrier is another person and the whole point is that you can see them carrying it.
		const bool bInsideVehicle =
			!bAsCarriedBody &&
			!bAsHarnessRider &&
			Cast<ASimCopterGroundAgent>(NewCarrier) != nullptr;
		SetActorHiddenInGame(bInsideVehicle);
	}

	BehaviorCarrier = NewCarrier;
	bRidingHarness = bAsHarnessRider;
	if (Helicopter != nullptr && Helicopter == ResolvePlayerHelicopter() && !bAsHarnessRider)
	{
		// SCHOOK: PersonSetCarrier 0x004c6360. Assigning the player's helicopter invokes
		// FUN_004c5210(0x3c, 1, 0, 1): people voice event 60, whose sole clip is doropn.
		// Force bypasses the ordinary camera/cabin audibility gate exactly as the original does.
		PlayPersonVoiceEvent(
			SimCopterSound::VOX_DOOR_OPEN,
			/*bAllocateSlot=*/true,
			/*bNonPositional=*/false,
			/*bForce=*/true);
	}

	// Climbing aboard something is a hospital worker leaving its post under its own program (BHAV
	// 269 boards the vehicle it came from). The carrier owns the transform from here, and holding
	// the roof confinement would fight it - or drag them back the moment they were set down.
	bHasHospitalRoofPost = false;

	// A legacy on-foot carry paused the VM. Preserve its live context and let it resume once the
	// helicopter owns the transform, so medevac health/delivery behavior is not discarded.
	bMissionCarried = false;
	if (!bBehaviorActive && BehaviorModel.IsValid() && bUseOriginalBehaviors)
	{
		bBehaviorActive = true;
	}
	bBehaviorMoveSuspended = true;
	bSnapToGround = false;
	CurrentVelocityCmPerSec = FVector::ZeroVector;
	ExternalVelocityCmPerSec = FVector::ZeroVector;
	BehaviorStepVelocityCmPerSec = FVector::ZeroVector;
	BehaviorStepTimeRemainingSeconds = 0.0f;
	ClearMoveTarget();
	SetActorEnableCollision(false);
	if (CollisionComponent != nullptr)
	{
		CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (USceneComponent* CarrierRoot = NewCarrier->GetRootComponent())
	{
		AttachToComponent(CarrierRoot, FAttachmentTransformRules::KeepWorldTransform);
		if (bAsCarriedBody && Cast<ASimCopterGroundAgent>(NewCarrier) != nullptr)
		{
			// Keep a patient visibly slung across the carrier instead of occupying the same
			// transform and disappearing inside their body. Held against the chest: far enough
			// out not to intersect, close enough to read as carried rather than floating along
			// in front of them.
			//
			// Gated on bAsCarriedBody since 2026-08-05. This used to fire for anything that rode a
			// ground agent, so BHAV 269's medic - which walks back to its ambulance and runs op 12
			// "walk to selection AND board it" - was laid across the ambulance's flank in the
			// corpse pose and left there. That is the reported "paramedic stuck at the side of the
			// ambulance": it had already got in.
			SetActorRelativeLocation(CarriedPersonRelativeOffsetCm);
			SetActorRelativeRotation(FRotator(0.0f, 90.0f, 88.0f));
			SetForcedPedestrianFigureClip(TEXT("Dead"));
		}
	}

	// Riding removes the person from every other person's object search, but not from rendering
	// unless they are physically inside the cabin.
	BehaviorContext.Attributes[EBhavAttr::Visible] = 0;

	if (Helicopter != nullptr && MissionEventId != INDEX_NONE)
	{
		if (ASimCopterMissionSystemActor* Missions = Cast<ASimCopterMissionSystemActor>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass())))
		{
			Missions->NotifyMissionPersonBoarded(this);
		}
	}
	return true;
}

void ASimCopterGroundAgent::UpdateCarriedTransform()
{
	AActor* Carrier = BehaviorCarrier.Get();
	if (Carrier == nullptr)
	{
		return;
	}
	if (!bRidingHarness)
	{
		// Attached to the carrier's root: the attachment already moves us.
		return;
	}
	const ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(Carrier);
	FVector RopeEnd = FVector::ZeroVector;
	if (Helicopter == nullptr || !Helicopter->TryGetRopeEndWorldLocation(RopeEnd))
	{
		// The rope has been wound all the way in with someone on it. They come inside - that is
		// the point of the winch - not onto the ground under the helicopter, which is what
		// letting go used to do. Opcode 58 normally gets here first; this is the backstop for
		// when the rope stows between behaviour ticks.
		if (!TransferFromHarnessToCabin())
		{
			AlightFromCarrier();
		}
		return;
	}
	SetActorLocation(RopeEnd - FVector(0.0f, 0.0f, GetCapsuleHalfHeightCm()), false);
}

void ASimCopterGroundAgent::AlightAttachmentOnly()
{
	if (GetAttachParentActor() != nullptr)
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}
}

bool ASimCopterGroundAgent::AlightFromCarrier()
{
	AActor* Carrier = BehaviorCarrier.Get();
	const bool bLeavingHelicopterCabin =
		bClaimedPassengerSeat && Carrier != nullptr && Carrier == ResolvePlayerHelicopter();
	if (bLeavingHelicopterCabin)
	{
		// FUN_004c6360 uses the same event-60 doropn cue when a person leaves a door-bearing
		// carrier. There is no dorcls entry in the original people voice-event table.
		PlayPersonVoiceEvent(
			SimCopterSound::VOX_DOOR_OPEN,
			/*bAllocateSlot=*/true,
			/*bNonPositional=*/false,
			/*bForce=*/true);
	}
	if (bClaimedPassengerSeat)
	{
		if (ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(Carrier))
		{
			Helicopter->RemoveMissionPassengersForMission(1, MissionEventId, GetMissionPassengerKind(), this);
		}
		bClaimedPassengerSeat = false;
	}

	AlightAttachmentOnly();
	BehaviorCarrier.Reset();
	bRidingHarness = false;
	bBehaviorMoveSuspended = false;
	bMissionCarried = false;
	bSnapToGround = true;
	SetActorHiddenInGame(false);
	ClearForcedPedestrianFigureClip();
	const FRotator CurrentRotation = GetActorRotation();
	SetActorRotation(FRotator(0.0f, CurrentRotation.Yaw, 0.0f));
	SetActorEnableCollision(true);
	if (CollisionComponent != nullptr)
	{
		CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	BehaviorContext.Attributes[EBhavAttr::Visible] = 1;
	// Deliberately NOT SnapToGroundImmediate: somebody let go of this person, so they fall.
	// bSnapToGround (set above) hands them to UpdateGroundSnap, which accelerates them down onto
	// whatever is beneath and only places them once they land. Teleporting them to the ground here
	// is what made a patient lifted out of the cabin appear on the deck with no drop at all.
	VerticalVelocityCmPerSec = 0.0f;
	return true;
}

bool ASimCopterGroundAgent::TransferFromHarnessToCabin()
{
	AActor* Carrier = BehaviorCarrier.Get();
	if (Carrier == nullptr || !bRidingHarness)
	{
		return false;
	}
	return BoardCarrier(
		Carrier,
		/*bAsHarnessRider*/ false,
		/*bAllowAirborneCabinTransfer*/ true);
}

bool ASimCopterGroundAgent::BoardSelection(FSimCopterPersonContext& Context)
{
	// FUN_004cc900. The harness is a place on the rope rather than an actor of its own, so a
	// harness selection carries the helicopter as the actor and the rope end as the location.
	AActor* Target = Context.SelectedObject.Get();
	if (Target == nullptr)
	{
		return false;
	}
	const bool bHarness = Context.bSelectionIsHarness;
	return BoardCarrier(Target, bHarness);
}

bool ASimCopterGroundAgent::PutSelectedPersonOnMe(FSimCopterPersonContext& Context)
{
	// FUN_004cc6a0: the selected person is teleported onto me and I become their carrier.
	ASimCopterGroundAgent* Person = Cast<ASimCopterGroundAgent>(Context.SelectedObject.Get());
	if (Person == nullptr || Person == this)
	{
		return false;
	}

	// Toting somebody out of the player's cabin has to happen at the aircraft. The teleport below is
	// the whole of the original's op 44 and it has no distance test, because it cannot need one - in
	// the shipped graph a walk that ended on contact is what got the walker here. See
	// DropSelectedPerson for why the remake cannot rely on that alone.
	if (const ASimCopterHelicopterPawn* Carrier =
			Cast<ASimCopterHelicopterPawn>(Person->GetBehaviorCarrier()))
	{
		if (!IsAtHelicopterForHandoff(*Carrier))
		{
			return false;
		}
	}

	Person->SetActorLocation(GetActorLocation(), false);
	return Person->BoardCarrier(
		this,
		/*bAsHarnessRider*/ false,
		/*bAllowAirborneCabinTransfer*/ false,
		/*bAsCarriedBody*/ true);
}

bool ASimCopterGroundAgent::DropSelectedPerson(FSimCopterPersonContext& Context)
{
	ASimCopterGroundAgent* Person = Cast<ASimCopterGroundAgent>(Context.SelectedObject.Get());
	if (Person == nullptr)
	{
		return false;
	}

	// BHAV 263 rec[3] is the moment an emergency worker takes a casualty out of the player's cabin.
	// The shipped graph then leaves the delivery to the patient's own BHAV 282, which posts it only
	// on XBLD 209 - so a medic who is not stood on the hospital when it happens completes the
	// interaction, revives the patient, and never credits the mission. That soft-locks the medevac:
	// the seat is empty, so nothing can hand the patient over a second time.
	//
	// Handing a casualty to a paramedic IS the delivery, wherever the two of them are standing.
	// NotifyMissionPersonDelivered is the idempotent service, so the ordinary hospital route still
	// runs its full animation and BHAV 282's later request is simply refused as already reported.
	const bool bIsEmergencyWorker = int32(Context.Attributes[EBhavAttr::State]) == 5;
	const ASimCopterHelicopterPawn* PlayerCarrier =
		Cast<ASimCopterHelicopterPawn>(Person->GetBehaviorCarrier());
	const bool bTakenFromPlayer =
		PlayerCarrier != nullptr &&
		Person->MissionEventId != INDEX_NONE &&
		!Person->HasMissionResolutionReported();

	// You cannot take somebody out of an aircraft you are not standing at. FUN_004cc8d0 carries no
	// such test and does not need one: in the shipped graph the only way to reach BHAV 263 rec[3] is
	// through rec[5]'s walk, which ends on physical contact (FUN_004c9470's move result 10), or with
	// the medic already aboard. The remake can arrive here with neither true - a program restarted
	// mid-stack, a helicopter that lifted off after the walk, rec[32]'s within-25-units fallback -
	// and then a casualty visibly leaves the cabin with nobody near it.
	//
	// Refusing makes op 47 take its false edge, which in BHAV 263 unwinds to 801's idle and probes
	// again next loop. That is a retry, not a failure: the medic keeps trying while the pilot is
	// there to be reached.
	if (PlayerCarrier != nullptr && !IsAtHelicopterForHandoff(*PlayerCarrier))
	{
		return false;
	}

	if (!Person->AlightFromCarrier())
	{
		return false;
	}

	if (bIsEmergencyWorker && bTakenFromPlayer)
	{
		if (ASimCopterMissionSystemActor* Missions = Cast<ASimCopterMissionSystemActor>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass())))
		{
			// The service resolves the passenger kind from the record itself and returns 0 for a
			// record that does not carry one, so this cannot invent a delivery the mission never
			// asked for.
			Missions->NotifyMissionPersonDelivered(Person);
		}
	}
	return true;
}

bool ASimCopterGroundAgent::SelectCarriedPerson(FSimCopterPersonContext& Context, const bool bAlsoDropThem)
{
	// FUN_004ca650 scans the person array for whoever's carrier is me.
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	ASimCopterGroundAgent* Carried = TrafficSystem != nullptr ? TrafficSystem->FindPersonCarriedBy(*this) : nullptr;
	if (Carried == nullptr)
	{
		Context.ClearSelection();
		return false;
	}
	const AActor* PreviousSelection = Context.SelectedObject.Get();
	const AActor* StartingVehicle = BehaviorStartingVehicle.Get();
	const bool bAtStartingAmbulance =
		bAlsoDropThem &&
		StartingVehicle != nullptr &&
		PreviousSelection == StartingVehicle &&
		int32(BehaviorContext.Attributes[EBhavAttr::State]) == 5 &&
		int32(Carried->BehaviorContext.Attributes[EBhavAttr::State]) == 6;

	if (bAlsoDropThem)
	{
		Carried->AlightFromCarrier();
		if (bAtStartingAmbulance)
		{
			// BHAV 262 -> 272 -> 275 reaches opcode 51 only after the medic has selected and
			// walked back to object class 10, the ambulance pool. BHAV 285 now owns the two
			// mission outcomes that follow; record that this is a real ground-service handoff,
			// not an arbitrary opcode-13 request from a person standing elsewhere.
			Carried->SetAmbulanceHandoffPending(true);
		}
		if (Carried->IsMissionPatientDead())
		{
			// The original removes a dead person at opcode 66, before any later handoff. The
			// remake deliberately retains a cabin body so the medic can visibly carry the same
			// actor. Once opcode 51 completes that interaction, the extra lifetime is over.
			Carried->SetActorHiddenInGame(true);
			Carried->SetLifeSpan(0.25f);
		}
	}
	Context.SelectedObject = Carried;
	Context.SelectedLocation = Carried->GetActorLocation();
	Context.bSelectionIsHarness = false;
	Context.bHasSelection = true;
	return true;
}

bool ASimCopterGroundAgent::IsCarryingPerson() const
{
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	return TrafficSystem != nullptr && TrafficSystem->FindPersonCarriedBy(*this) != nullptr;
}

bool ASimCopterGroundAgent::GetOnHelicopterIfHarnessRaised(FSimCopterPersonContext& Context)
{
	// FUN_004cccd0. Not on the harness at all -> nothing to do, answer true (the original's two
	// "can ignore" arms). On the harness with the bucket still down -> also true, keep riding.
	// On the harness once it is raised -> climb into the cabin, and the answer is whether that
	// worked.
	const ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(BehaviorCarrier.Get());
	if (Helicopter == nullptr || !bRidingHarness)
	{
		return true;
	}
	// "bucket not raised - can ignore": while the rope is still out the rider stays on it.
	if (Helicopter->IsRopeDeployed())
	{
		return true;
	}
	return TransferFromHarnessToCabin();
}

bool ASimCopterGroundAgent::IsCarrierPlayerHelicopter() const
{
	const AActor* Carrier = BehaviorCarrier.Get();
	return Carrier != nullptr && !bRidingHarness && Carrier->IsA<ASimCopterHelicopterPawn>();
}

bool ASimCopterGroundAgent::IsCarrierHarness() const
{
	return BehaviorCarrier.IsValid() && bRidingHarness;
}

bool ASimCopterGroundAgent::IsOnHomeTile() const
{
	int32 FileX = INDEX_NONE;
	int32 FileY = INDEX_NONE;
	return BehaviorHomeTile.X != INDEX_NONE &&
		TryGetCurrentTileCoordinate(FileX, FileY) &&
		FIntPoint(FileX, FileY) == BehaviorHomeTile;
}

bool ASimCopterGroundAgent::SelectMedevacVictimAboardPlayer(FSimCopterPersonContext& Context)
{
	// FUN_004cc830 walks the player's passenger list for a person in state 6. The remake keeps
	// the same people attached to the helicopter, so the search is over who it is carrying.
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	const ASimCopterHelicopterPawn* Helicopter = ResolvePlayerHelicopter();
	if (TrafficSystem == nullptr || Helicopter == nullptr)
	{
		Context.ClearSelection();
		return false;
	}

	ASimCopterGroundAgent* Victim = TrafficSystem->FindMedevacPassengerAboard(Helicopter);
	if (Victim == nullptr)
	{
		Context.ClearSelection();
		return false;
	}
	Context.SelectedObject = Victim;
	Context.SelectedLocation = Victim->GetActorLocation();
	Context.bSelectionIsHarness = false;
	Context.bHasSelection = true;
	return true;
}

void ASimCopterGroundAgent::MessageOwningVehicle(const int32 MessageId)
{
	// FUN_0049aed0(person+0x170, msg). The remake stamps the deploying vehicle on the crew member
	// so a boarded officer or paramedic can release it; anything else is a no-op, as it is in the
	// original for a person with no vehicle.
	if (ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner()))
	{
		TrafficSystem->NotifyCrewMemberMessagedVehicle(*this, MessageId);
	}
}

void ASimCopterGroundAgent::ThrowProjectileAtSelection(
	FSimCopterPersonContext& Context,
	const bool bAtSelection,
	const bool bIncendiary)
{
	// FUN_004cbfd0 / FUN_004cc130 both bind "Thro" and hand a projectile to FUN_0048e0b0, but they
	// ask for different types, and the difference is the whole point:
	//
	//   op 60 (FUN_004cced0 -> FUN_004cbfd0 with record[0] == 0x3c) -> type 4, class flag 0x10:
	//        the arsonist's firebomb, which grounds and can start a building fire.
	//   ops 30/83                                                   -> type 10, class flag 0x400:
	//        the rioter's thrown rock, which bounces, hurts whatever it lands on and expires.
	//
	// Only the first is modelled as a world object; the rock keeps the animation half, which is
	// what stops a rioter standing inert where the original throws something.
	if (bAtSelection)
	{
		FaceSelectedObject(Context);
	}
	Context.PendingAnimMnemonic = TEXT("Thro");

	if (bIncendiary)
	{
		if (ASimCopterMissionSystemActor* Missions = Cast<ASimCopterMissionSystemActor>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass())))
		{
			Missions->ThrowArsonistFirebomb(GetActorLocation());
		}
	}
}

bool ASimCopterGroundAgent::BeginFallAndDie(FSimCopterPersonContext& Context)
{
	// FUN_004cbbc0's terminal arm: come off whatever is holding you, land, post EVT_PersonDied and
	// hold the "Dead" pose. The literal detach is not allowed to discard a medevac body already
	// inside the cabin: its real actor and seat stay paired until the hospital handoff takes it.
	const bool bDeadMedevacPatientAboard =
		MissionEventId != INDEX_NONE &&
		int32(Context.Attributes[EBhavAttr::State]) == 6 &&
		bClaimedPassengerSeat &&
		Cast<ASimCopterHelicopterPawn>(BehaviorCarrier.Get()) != nullptr;
	if (!bDeadMedevacPatientAboard)
	{
		AlightFromCarrier();
	}
	PostMissionOutcome(Context, 10);
	SetMissionDeadPose();
	return true;
}

bool ASimCopterGroundAgent::SelectOwningVehicle(FSimCopterPersonContext& Context)
{
	// FUN_004ca700: person+0x170 names the emergency vehicle this person rode in on; with none,
	// the original falls back to the player's helicopter.
	if (AActor* StartingVehicle = BehaviorStartingVehicle.Get())
	{
		Context.SelectedObject = StartingVehicle;
		Context.SelectedLocation = StartingVehicle->GetActorLocation();
		Context.bSelectionIsHarness = false;
		Context.bHasSelection = true;
		return true;
	}

	if (int32(BehaviorContext.Attributes[EBhavAttr::State]) == 5)
	{
		const ASimCopterHelicopterPawn* Helicopter = ResolvePlayerHelicopter();
		const ASimCopterMissionSystemActor* Missions = Cast<ASimCopterMissionSystemActor>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass()));
		if (Helicopter == nullptr ||
			Missions == nullptr ||
			!Missions->CanHospitalParamedicBoardPlayerHelicopter(Helicopter))
		{
			// Do not even select/walk toward the fallback helicopter after a completed handoff.
			// BoardCarrier repeats this check as the atomic action backstop.
			Context.ClearSelection();
			return false;
		}
	}

	int32 Distance = 0;
	return SelectObjectOfClass(Context, EBhavObjectClass::PlayerHelicopter, Distance);
}

bool ASimCopterGroundAgent::IsSelectionPlayerHelicopter(const FSimCopterPersonContext& Context) const
{
	const AActor* Selected = Context.SelectedObject.Get();
	return Selected != nullptr && Selected == ResolvePlayerHelicopter();
}

bool ASimCopterGroundAgent::IsSelectionWithinUnits(const FSimCopterPersonContext& Context, const int32 Units) const
{
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	if (!Context.bHasSelection || TrafficSystem == nullptr)
	{
		return false;
	}
	// FUN_004ccad0 / FUN_004cca60 both sum the three absolute axis deltas in original units.
	const FVector Delta = Context.SelectedLocation - GetActorLocation();
	const float UnitCm = FMath::Max(1.0f, TrafficSystem->GetPeopleWorldCmPerOriginalUnit());
	const float ManhattanUnits = (FMath::Abs(Delta.X) + FMath::Abs(Delta.Y) + FMath::Abs(Delta.Z)) / UnitCm;
	return ManhattanUnits < float(Units);
}

// SCHOOK: PeopleOpSetSeatFace 0x004ccb40
void ASimCopterGroundAgent::SetSeatPortraitMood(int32 Mood)
{
	// people1.bmp has three rows and FUN_00453f70 multiplies the record straight into a source
	// rect, so anything else would sample off the sheet.
	SeatPortraitMood = FMath::Clamp(Mood, 0, FSimCopterPopulationSprite::People1Rows - 1);

	// FUN_004ccb40 writes the seat manifest and marks the window dirty. Nothing else stores this,
	// so a person with no seat simply keeps the value for whenever they take one.
	if (bClaimedPassengerSeat)
	{
		if (ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(BehaviorCarrier.Get()))
		{
			Helicopter->SetMissionPassengerPortraitState(this, SeatPortraitMood);
		}
	}
}

// SCHOOK: PersonPlayVoice 0x004c5210 (via opcodes 57 and 85, FUN_004ccca0 / FUN_004cc110)
void ASimCopterGroundAgent::PlayPersonVoiceEvent(
	const int32 VoiceEvent,
	const bool bAllocateSlot,
	const bool bNonPositional,
	const bool bForce)
{
	USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this);
	if (Audio == nullptr)
	{
		return;
	}
	if (VoiceEvent < 0)
	{
		StopPersonVoice(); // the param_2 == -1 arm
		return;
	}
	// The audibility gate. The original plays when the sound is 2D, when the caller forces it,
	// when this person is riding the player's cabin - which is what carries an injured
	// passenger's EKG and moans into the cockpit - or when DAT_00503aa0 == 3, the mode the game
	// enters as you step out of the helicopter and walk the streets yourself.
	const bool bPlayerOnFoot = GetWorld() != nullptr &&
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterOnFootPawn::StaticClass()) != nullptr;
	if (!bNonPositional && !bForce && !bPlayerOnFoot && !IsCarrierPlayerHelicopter())
	{
		return;
	}

	if (VoiceSlotId == INDEX_NONE)
	{
		// param_3 == 0 means "only speak if I already have a slot"; the original returns without
		// taking one. FUN_004c5120 recycles the oldest speaker when all of them are busy, which
		// the mixer's own allocator does not do - a dropped line is the honest fallback.
		if (!bAllocateSlot)
		{
			return;
		}
		VoiceSlotId = Audio->AcquireVoiceSlot();
		if (VoiceSlotId == INDEX_NONE)
		{
			return;
		}
		VoiceCurrentEvent = INDEX_NONE;
	}

	const int32 VoiceSet = int32(BehaviorContext.Attributes[EBhavAttr::VoiceSet]);
	if (VoiceCurrentEvent == VoiceEvent && Audio->IsPlaying(VoiceSlotId))
	{
		// Already saying this. A looping sound that is also this person's own voice event gets
		// re-tuned instead of restarted - and BHAV 800 makes 58 a medevac victim's voice event
		// precisely so the EKG's rate tracks their health: (health*4 + 0x78) * 0x19 is 13 kHz at
		// full health and 3 kHz at zero, i.e. the beep slows and deepens as they fade. Everything
		// else re-tunes from the walk speed, which is how footsteps keep up with a runner.
		if (VoiceSet != VoiceEvent)
		{
			return;
		}
		const int32 Health = FMath::Clamp(int32(BehaviorContext.Attributes[EBhavAttr::MedevacHealth]), 0, 100);
		BehaviorContext.Attributes[EBhavAttr::MedevacHealth] = uint16(Health);
		const int32 Rate = VoiceEvent == SimCopterSound::VOX_EKG
			? SimCopterSound::GetEkgFrequencyHz(Health)
			: SimCopterSound::GetWalkPacedFrequencyHz(int32(BehaviorContext.Attributes[EBhavAttr::MoveSpeed]));
		Audio->SetFrequencyHz(VoiceSlotId, FMath::Max(0, Rate));
		return;
	}

	// FUN_004c5210's per-event pitch: person+0x178 unless the event overrides it. The three
	// footstep clips and the Elvis noises scale with the walk speed, and the EKG starts from the
	// victim's health so it is already at the right rate before the first re-tune.
	int32 PitchDeltaHz = int32(int16(BehaviorContext.Attributes[EBhavAttr::VoicePitch]));
	if (SimCopterSound::IsWalkPacedVoiceEvent(VoiceEvent))
	{
		PitchDeltaHz = SimCopterSound::GetWalkPacedPitchDeltaHz(
			int32(BehaviorContext.Attributes[EBhavAttr::MoveSpeed]));
	}
	else if (VoiceEvent == SimCopterSound::VOX_EKG)
	{
		PitchDeltaHz = SimCopterSound::GetEkgStartPitchDeltaHz(
			int32(BehaviorContext.Attributes[EBhavAttr::MedevacHealth]));
	}
	const bool bLoop = SimCopterSound::IsLoopingVoiceEvent(VoiceEvent);

	VoiceCurrentEvent = VoiceEvent;
	bVoiceIsNonPositional = bNonPositional;
	// uStack_108: the loop bit is set for a looping clip, and also whenever the event is this
	// person's own voice event.
	const int32 Flags = (bLoop || VoiceSet == VoiceEvent) ? SimCopterSoundFlags::Loop : 0;
	if (!Audio->PlayVoiceEvent(VoiceSlotId, VoiceEvent, GetActorLocation(), PitchDeltaHz, bNonPositional, Flags))
	{
		VoiceCurrentEvent = INDEX_NONE;
	}
}

void ASimCopterGroundAgent::UpdatePersonVoice()
{
	if (VoiceSlotId == INDEX_NONE)
	{
		return;
	}
	USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this);
	if (Audio == nullptr)
	{
		return;
	}
	if (!Audio->IsPlaying(VoiceSlotId))
	{
		// The bank is fourteen slots for a whole city. The original recycles the oldest speaker
		// when it runs dry (FUN_004c5120); handing a finished slot straight back is the same
		// result without having to rank speakers, and it means a one-shot line cannot pin a slot.
		Audio->ReleaseVoiceSlot(VoiceSlotId);
		VoiceSlotId = INDEX_NONE;
		VoiceCurrentEvent = INDEX_NONE;
		return;
	}
	if (!bVoiceIsNonPositional)
	{
		// One buffer per sound and no per-emitter handle: a 3D voice is kept on its speaker by
		// the owner calling SetPosition (FUN_0042a2f0) while it plays.
		Audio->SetPosition(VoiceSlotId, GetActorLocation());
	}
}

// SCHOOK: PersonPlayVoice 0x004c5210, the param_2 == -1 arm (opcode 85, FUN_004cc110)
void ASimCopterGroundAgent::StopPersonVoice()
{
	if (VoiceSlotId == INDEX_NONE)
	{
		return;
	}
	if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
	{
		Audio->ReleaseVoiceSlot(VoiceSlotId);
	}
	VoiceSlotId = INDEX_NONE;
	VoiceCurrentEvent = INDEX_NONE;
	bVoiceIsNonPositional = false;
}

// SCHOOK: PeopleOpPlayerSpeed 0x004ccb80
int32 ASimCopterGroundAgent::GetPlayerHelicopterSpeed() const
{
	const ASimCopterHelicopterPawn* Helicopter = ResolvePlayerHelicopter();
	if (Helicopter == nullptr)
	{
		return 0;
	}

	// Not the plain airspeed. FUN_004ccb80 is
	//     (heli[0x4e] >> 16) * MaxDamage / max(heli[0x34], 1)
	// where heli[0x34] is the machine's remaining hit points (FUN_0048a550) and MaxDamage the
	// model's full complement (FUN_0048a530 reads registry +0x48). The ratio is 1 in a pristine
	// helicopter and grows as it is beaten up, so BHAV 264's 250/125 thresholds are crossed at
	// lower and lower real speeds: passengers get frightened sooner in a wreck. The original
	// floors the divisor at 1.0 - the compare is on the float's bit pattern, so a negative hit
	// point count lands there too - and clamps the result to 65535 before truncating.
	const FSimCopterFlightModel& Model = Helicopter->GetFlightModel();
	const int32 SpeedUnits = Model.ForwardSpeed >> 16;
	const float HitPoints = float(Model.HitPoints);
	const float Divisor = HitPoints >= 0.5f ? HitPoints : 1.0f;
	const float Scaled = float(Model.Tuning.MaxDamage) / Divisor * float(SpeedUnits);
	return int32(FMath::Min(Scaled, 65535.0f));
}

bool ASimCopterGroundAgent::HasHiddenPersonInState(const int32 State) const
{
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	return TrafficSystem != nullptr && TrafficSystem->HasHiddenBehaviorPersonInState(State);
}

int32 ASimCopterGroundAgent::GetDifficultyTier() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const ASimCopterMissionSystemActor* Missions = Cast<ASimCopterMissionSystemActor>(
			UGameplayStatics::GetActorOfClass(const_cast<UWorld*>(World), ASimCopterMissionSystemActor::StaticClass())))
		{
			return Missions->GetMissionDifficultyTier();
		}
	}
	return 1;
}

int32 ASimCopterGroundAgent::GetActiveMedevacMissionCount() const
{
	// FUN_004abb00(0x20): live records whose type mask carries the MedEvac bit.
	if (const UWorld* World = GetWorld())
	{
		if (const ASimCopterMissionSystemActor* Missions = Cast<ASimCopterMissionSystemActor>(
			UGameplayStatics::GetActorOfClass(const_cast<UWorld*>(World), ASimCopterMissionSystemActor::StaticClass())))
		{
			return Missions->CountActiveMissionsOfType(SimCopterMissions::TYPE_Medevac);
		}
	}
	return 0;
}

bool ASimCopterGroundAgent::CollapseIntoMedevacVictim(FSimCopterPersonContext& Context)
{
	// SCHOOK: PersonCollapsesIntoCasualty 0x004c9b50
	UWorld* World = GetWorld();
	ASimCopterMissionSystemActor* Missions = World != nullptr
		? Cast<ASimCopterMissionSystemActor>(
			UGameplayStatics::GetActorOfClass(World, ASimCopterMissionSystemActor::StaticClass()))
		: nullptr;
	if (Missions == nullptr || !bBehaviorActive)
	{
		return false;
	}
	// Someone the mission layer already owns is not available to be re-purposed as a fresh casualty,
	// and neither is anyone already lying there as one.
	if (bMissionCarried || bMissionStationary || bClaimedPassengerSeat || bPersistentHospitalRoofCrew ||
		BehaviorCarrier.IsValid() ||
		Context.Attributes[EBhavAttr::State] == 6)
	{
		return false;
	}

	// The original tells the record this person belonged to that they died, then hands them to a new
	// MedEvac record. Nearly every route into BHAV 906 "Rxn: Swoon" is one of the player's own tools
	// (tear gas via 907, "Ouch" via 902), so the remake routes it through the established
	// player-caused injury service: same state-6 victim and marker, but no completion reward, which
	// is the existing rule for putting a civilian in hospital yourself.
	if (MissionEventId != INDEX_NONE && !bMissionResolutionReported)
	{
		PostMissionOutcome(Context, 10);
	}
	Context.Attributes[EBhavAttr::Speed] = 0;
	return Missions->CreatePlayerCausedMedevacForVictim(this);
}

bool ASimCopterGroundAgent::MeasureRiotCrowd(
	const FSimCopterPersonContext& Context,
	const int32 RadiusTiles,
	int32& OutFacingOctant,
	int32& OutAverageAgitation,
	int32& OutCount) const
{
	OutFacingOctant = 0;
	OutAverageAgitation = 0;
	OutCount = 0;

	// FUN_004cb480 refuses outright unless a riot record is live (FUN_004a9230(0x1000)) - the crowd
	// scan is only a riot measurement while there is a riot to measure.
	const UWorld* World = GetWorld();
	const ASimCopterMissionSystemActor* Missions = World != nullptr
		? Cast<ASimCopterMissionSystemActor>(
			UGameplayStatics::GetActorOfClass(const_cast<UWorld*>(World), ASimCopterMissionSystemActor::StaticClass()))
		: nullptr;
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	if (Missions == nullptr ||
		TrafficSystem == nullptr ||
		Missions->FindActiveMissionOfType(SimCopterMissions::TYPE_Riot) == INDEX_NONE)
	{
		return false;
	}

	// A crowd that is present but calm still measures - it just has no bearing. FUN_004c9f10
	// writes 0xffff into the octant and zero into the mean in that case and returns anyway, so
	// this only reports a facing when there is one. Failing here instead is what used to make
	// every riot disperse itself on the first tick.
	FVector Centroid = FVector::ZeroVector;
	if (TrafficSystem->MeasureBehaviorCrowd(*this, RadiusTiles, OutCount, OutAverageAgitation, Centroid))
	{
		// The bearing the original reports is already the stored octant (it applies the same -2
		// turn as opcode 18), which is why BHAV 852 can assign it straight to the facing attribute.
		int32 Octant = 0;
		if (TryGetBehaviorFacingOctantToward(Centroid, Octant))
		{
			OutFacingOctant = Octant;
		}
	}
	return true;
}

bool ASimCopterGroundAgent::JoinLiveRiot(FSimCopterPersonContext& Context)
{
	// FUN_004cb680 -> FUN_004c4e60: find the live riot, post EVT_RiotPersonAdded, and change state
	// to 3 so the walker restarts on BHAV 850 "Riot!".
	UWorld* World = GetWorld();
	ASimCopterMissionSystemActor* Missions = World != nullptr
		? Cast<ASimCopterMissionSystemActor>(
			UGameplayStatics::GetActorOfClass(World, ASimCopterMissionSystemActor::StaticClass()))
		: nullptr;
	if (Missions == nullptr)
	{
		return false;
	}
	const int32 RiotEventId = Missions->FindActiveMissionOfType(SimCopterMissions::TYPE_Riot);
	if (RiotEventId == INDEX_NONE)
	{
		return false;
	}

	// Anyone the mission layer already owns keeps the job they have; the original had no passengers
	// or hospital staff to protect here, but converting one would strand the mission that needs them.
	if (bMissionCarried || bMissionStationary || bClaimedPassengerSeat || bPersistentHospitalRoofCrew ||
		BehaviorCarrier.IsValid() || MissionEventId != INDEX_NONE)
	{
		return false;
	}

	Missions->PostMissionEvent(SimCopterMissions::EVT_RiotPersonAdded, RiotEventId, 1, false);
	MissionEventId = RiotEventId;
	InitialPersonState = 3;
	ResetMissionActionTracking();
	// FUN_004c0df0 sets the state, which rebinds the program - the caller then returns 3 (Stop)
	// because the program the walker was running no longer exists.
	Context.ResetToState(3);
	Context.Attributes[EBhavAttr::CriminalCaught] = 0;
	return true;
}

bool ASimCopterGroundAgent::FaceNearestFireWithin(
	FSimCopterPersonContext& Context,
	const int32 RadiusTiles,
	int32& OutTileDistance)
{
	OutTileDistance = 0;
	UWorld* World = GetWorld();
	ASimCopterMissionSystemActor* Missions = World != nullptr
		? Cast<ASimCopterMissionSystemActor>(
			UGameplayStatics::GetActorOfClass(World, ASimCopterMissionSystemActor::StaticClass()))
		: nullptr;
	int32 MyX = INDEX_NONE;
	int32 MyY = INDEX_NONE;
	if (Missions == nullptr || RadiusTiles <= 0 || !TryGetCurrentTileCoordinate(MyX, MyY))
	{
		return false;
	}

	// FUN_004ca190 takes the Manhattan-nearest cell carrying scene-cell flag 0x20 within the square.
	// The flag's writer is not in the Ghidra export set: "0x20 = this cell is alight" is read off the
	// program's own name (274 "Gawk at (or flee) fire") plus the fact that FUN_004c9cc0 refuses an
	// ambient spawn on such a cell. The fire truck's own target scan is the query that already
	// answers it here.
	ASimCopterMissionSystemActor::FServiceFireTarget Target;
	if (!Missions->TryAcquireServiceFireTarget(FIntPoint(MyX, MyY), RadiusTiles, Target) ||
		Target.Tile.X == INDEX_NONE)
	{
		return false;
	}

	OutTileDistance = FMath::Abs(Target.Tile.X - MyX) + FMath::Abs(Target.Tile.Y - MyY);
	int32 Octant = 0;
	if (TryGetBehaviorFacingOctantToward(Target.World, Octant))
	{
		Context.Attributes[EBhavAttr::Facing] = uint16(Octant);
	}
	return true;
}

int32 ASimCopterGroundAgent::GetSelectionRoomForBoarding(const FSimCopterPersonContext& Context) const
{
	// FUN_004cc980: the player's helicopter answers manifest[+4] - manifest[+8], i.e. free seats.
	const AActor* Selection = Context.SelectedObject.Get();
	if (Selection == nullptr)
	{
		return INDEX_NONE;
	}
	if (const ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(Selection))
	{
		return Helicopter == ResolvePlayerHelicopter() ? Helicopter->GetAvailablePassengerSeats() : INDEX_NONE;
	}
	// obj+0xc & 0x10 is the emergency-vehicle flag (FUN_004c4e10 copies obj+0xe from it into
	// person+0x170), and those get the original's flat "there is room" constant.
	if (const ASimCopterGroundAgent* Vehicle = Cast<ASimCopterGroundAgent>(Selection))
	{
		if (Vehicle->GetAgentKind() == ESimCopterGroundAgentKind::Vehicle)
		{
			return ISimCopterBehaviorWorld::EmergencyVehicleRoomId;
		}
	}
	return INDEX_NONE;
}

bool ASimCopterGroundAgent::BeginBeamAbduction(USceneComponent* Target)
{
	// SCHOOK: BeamPersonUp 0x004c0f40 (eligibility 0x004c0f80)
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	UWorld* World = GetWorld();
	if (Target == nullptr || World == nullptr || TrafficSystem == nullptr)
	{
		return false;
	}
	if (!bBehaviorActive || AgentKind != ESimCopterGroundAgentKind::Pedestrian || bBeamAbductionActive)
	{
		return false;
	}

	// person+0x148 != 0 needs a 1-in-3000 roll of its own: the UFO takes ambient pedestrians freely
	// and a mission person only very rarely.
	if (BehaviorContext.Attributes[EBhavAttr::State] != 0 &&
		BehaviorContext.RandomBounded(3000) != 0)
	{
		return false;
	}

	// person+0x15e, "already written off", plus the remake's own engine-owned people. The original
	// had no equivalent of a mission carrying a real actor, and abducting one would strand it.
	if (bMissionCarried || bMissionStationary || bPassengerFallActive || bMissionPatientDead ||
		bClaimedPassengerSeat || bPersistentHospitalRoofCrew || BehaviorCarrier.IsValid())
	{
		return false;
	}

	// FUN_0049ad30: the person has to be on screen, with person+0x1c4 as the test radius.
	if (!WasRecentlyRendered(0.5f))
	{
		return false;
	}

	// ...and within 9 tiles of the camera tile (DAT_0061a618/0x61a61c).
	int32 MyX = INDEX_NONE;
	int32 MyY = INDEX_NONE;
	int32 CameraX = INDEX_NONE;
	int32 CameraY = INDEX_NONE;
	const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	const APlayerCameraManager* CameraManager = PlayerController != nullptr ? PlayerController->PlayerCameraManager : nullptr;
	if (CameraManager == nullptr ||
		!TryGetCurrentTileCoordinate(MyX, MyY) ||
		!TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(CameraManager->GetCameraLocation(), CameraX, CameraY) ||
		FMath::Abs(CameraX - MyX) >= 9 ||
		FMath::Abs(CameraY - MyY) >= 9)
	{
		return false;
	}

	BehaviorBeamTarget = Target;
	bBeamAbductionActive = true;
	// The flight is a teleport per tick with no move check, so the ground snap has to let go of them
	// the same way it does for a swimmer or a train-roof rider.
	bSnapToGround = false;
	BehaviorStepVelocityCmPerSec = FVector::ZeroVector;
	BehaviorStepTimeRemainingSeconds = 0.0f;
	// FUN_004c0df0(0x10, -1): state 16 is BHAV 666 "Porkchop".
	InitialPersonState = 16;
	ResetBehaviorProgramOverride();
	BehaviorContext.ResetToState(16);
	return true;
}

bool ASimCopterGroundAgent::AdvanceBeamAbduction(FSimCopterPersonContext& Context)
{
	// SCHOOK: BeamFlightStep 0x004cb830
	const USceneComponent* Target = BehaviorBeamTarget.Get();
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	if (Target == nullptr || TrafficSystem == nullptr)
	{
		// No person+0x1a8: the handler returns 1 without moving. If the saucer left while we were
		// on our way up, finish anyway rather than leaving somebody hanging in the air.
		FinishBeamAbduction();
		return false;
	}

	// One step is MoveSpeed WHOLE original units along the normalised 3D delta - the walker's /12
	// does not apply here - and the position is written straight through with no climb gate.
	const float UnitCm = FMath::Max(0.01f, TrafficSystem->GetPeopleWorldCmPerOriginalUnit());
	const float StepCm = FMath::Max(0, int32(int16(Context.Attributes[EBhavAttr::MoveSpeed]))) * UnitCm;
	const FVector Delta = Target->GetComponentLocation() - GetActorLocation();
	const float DistanceCm = Delta.Size();
	if (StepCm > 0.0f && DistanceCm > KINDA_SMALL_NUMBER)
	{
		SetActorLocation(GetActorLocation() + Delta / DistanceCm * FMath::Min(StepCm, DistanceCm));
	}

	// Result 2 (keep flying) only while there was at least one whole step left to travel and the
	// person is still within 0x15 tiles of the camera; anything else finishes the opcode.
	if (StepCm <= 0.0f || DistanceCm < StepCm)
	{
		FinishBeamAbduction();
		return false;
	}

	int32 MyX = INDEX_NONE;
	int32 MyY = INDEX_NONE;
	int32 CameraX = INDEX_NONE;
	int32 CameraY = INDEX_NONE;
	const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	const APlayerCameraManager* CameraManager = PlayerController != nullptr ? PlayerController->PlayerCameraManager : nullptr;
	if (CameraManager == nullptr ||
		!TryGetCurrentTileCoordinate(MyX, MyY) ||
		!TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(CameraManager->GetCameraLocation(), CameraX, CameraY) ||
		FMath::Abs(CameraX - MyX) >= 0x15 ||
		FMath::Abs(CameraY - MyY) >= 0x15)
	{
		FinishBeamAbduction();
		return false;
	}

	return true;
}

void ASimCopterGroundAgent::FinishBeamAbduction()
{
	// BHAV 666 rec[8] sets person+0x152 to 0 the moment the flight ends, and in the original that
	// both hides the figure and stops its behaviour being simulated. Its remaining records (a sound,
	// Idle-10, then Disappear) run out the clock on an already-invisible person, so hide the actor
	// here and let opcode 40 recycle it.
	if (!bBeamAbductionActive)
	{
		return;
	}
	bBeamAbductionActive = false;
	BehaviorBeamTarget.Reset();
	BehaviorContext.Attributes[EBhavAttr::Visible] = 0;
	SetActorHiddenInGame(true);
}

void ASimCopterGroundAgent::OnUnknownOpcode(int32 Opcode)
{
	if (!ReportedUnknownOpcodes.Contains(Opcode))
	{
		ReportedUnknownOpcodes.Add(Opcode);
		UE_LOG(LogSimCopterGroundAgent, Verbose, TEXT("%s: BHAV opcode %d not yet ported (following false edge)."), *GetName(), Opcode);
	}
}

void ASimCopterGroundAgent::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Before any of the early returns below: a carried or suspended agent is still on screen, and a
	// person who kept the sun's noon brightness after dark is exactly the bug this fixes.
	RefreshSpriteExposure();

	if (AvoidanceMoveTimeRemainingSeconds > 0.0f)
	{
		AvoidanceMoveTimeRemainingSeconds = FMath::Max(0.0f, AvoidanceMoveTimeRemainingSeconds - DeltaSeconds);
	}
	if (AvoidancePathOffsetTimeRemainingSeconds > 0.0f)
	{
		AvoidancePathOffsetTimeRemainingSeconds = FMath::Max(0.0f, AvoidancePathOffsetTimeRemainingSeconds - DeltaSeconds);
	}
	if (GuidanceMoveTargetTimeRemainingSeconds > 0.0f)
	{
		GuidanceMoveTargetTimeRemainingSeconds = FMath::Max(0.0f, GuidanceMoveTargetTimeRemainingSeconds - DeltaSeconds);
	}
	if (bMissionCarried)
	{
		CurrentVelocityCmPerSec = FVector::ZeroVector;
		ExternalVelocityCmPerSec = FVector::ZeroVector;
		StopWalkingVoice();
		UpdateWaterSubmersion(DeltaSeconds);
		UpdateJankyAnimation(DeltaSeconds);
		return;
	}
	if (bBehaviorMoveSuspended)
	{
		// Riding something: the carrier owns the transform, but the walker keeps running - that
		// is how a passenger decides to get off again.
		if (!BehaviorCarrier.IsValid())
		{
			AlightFromCarrier();
		}
		else
		{
			CurrentVelocityCmPerSec = FVector::ZeroVector;
			ExternalVelocityCmPerSec = FVector::ZeroVector;
			StopWalkingVoice();
			UpdateCarriedTransform();
			UpdateOriginalBehavior(DeltaSeconds);
			UpdateWaterSubmersion(DeltaSeconds);
			UpdateJankyAnimation(DeltaSeconds);
			return;
		}
	}
	UpdateOriginalBehavior(DeltaSeconds);
	const FVector PreMovementLocation = GetActorLocation();
	UpdateMovement(DeltaSeconds);
	// Drive footsteps from actual horizontal movement, not merely a non-zero BHAV move-speed
	// attribute. Blocked, stationary, carried and expired-step people therefore cannot leave a
	// loop running. Guidance/scripted walkers still get footsteps because they really moved.
	if (AgentKind == ESimCopterGroundAgentKind::Pedestrian)
	{
		const FVector FrameMovement = GetActorLocation() - PreMovementLocation;
		const bool bActuallyMoving = FVector(FrameMovement.X, FrameMovement.Y, 0.0f).SizeSquared() > FMath::Square(0.01f);
		const int32 MoveSpeed = bActuallyMoving
			? FMath::Max(1, int32(int16(BehaviorContext.Attributes[EBhavAttr::MoveSpeed])))
			: 0;
		UpdateWalkingVoice(MoveSpeed);
	}
	// After every mover and before the ground snap: a worker pushed past the edge of its roof must
	// be back over it before the snap reads a surface, or the snap is what drops them to the street.
	ContainToHospitalRoofPost();
	if (bSnapToGround)
	{
		UpdateGroundSnap(DeltaSeconds);
	}
	// After the snap: the submersion is measured from where the capsule ended up this frame.
	UpdateWaterSubmersion(DeltaSeconds);
	UpdateJankyAnimation(DeltaSeconds);
}

void ASimCopterGroundAgent::ConfigureAgent(
	ESimCopterGroundAgentKind NewAgentKind,
	const FString& NewMeshTableName,
	const FString& NewOriginalGameRoot,
	float NewMovementSpeedCmPerSec)
{
	AgentKind = NewAgentKind;
	MeshTableName = NewMeshTableName;
	OriginalGameRoot.Path = NewOriginalGameRoot;
	MovementSpeedCmPerSec = FMath::Max(1.0f, NewMovementSpeedCmPerSec);
	AnimationPhase = FMath::FRandRange(0.0f, UE_TWO_PI);
	// BeginPlay already ran with the default (pedestrian) kind; stop that behavior VM before
	// retyping. The pedestrian branch below restarts it cleanly.
	bBehaviorActive = false;
	BehaviorTickAccumulator = 0.0f;
	ApplyAgentShape();

	if (AgentKind == ESimCopterGroundAgentKind::Pedestrian)
	{
		BuildPedestrianBody();
		StartOriginalBehavior();
	}
	else if (!MeshTableName.IsEmpty())
	{
		LoadOriginalMeshFromOriginalGameRoot();
	}
	else
	{
		DisableVehicleHeadlights();
		ShowOriginalMesh(false);
	}
}

bool ASimCopterGroundAgent::CaptureRuntimeSaveState(TArray<uint8>& OutData)
{
	OutData.Reset();
	FMemoryWriter Writer(OutData, true);
	uint32 Magic = GroundAgentRuntimeSaveMagic;
	int32 Version = GroundAgentRuntimeSaveVersion;
	Writer << Magic << Version;
	uint8 Kind = static_cast<uint8>(AgentKind);
	Writer << Kind << MeshTableName << OriginalGameRoot.Path << MovementSpeedCmPerSec;
	Writer << PedestrianFigureName << InitialPersonState << InitialBehaviorClass;
	Writer << InitialBehaviorAgitation << InitialBehaviorProgramId << MissionEventId;
	FTransform Transform = GetActorTransform();
	Writer << Transform;

	Writer << MoveTargetLocation << CurrentVelocityCmPerSec << ExternalVelocityCmPerSec;
	Writer << VerticalVelocityCmPerSec << AvoidanceMoveTargetLocation << AvoidancePathOffset;
	Writer << GuidanceMoveTargetLocation;
	SerializeAgentBool(Writer, bHasMoveTarget);
	Writer << TrafficSpeedScale << AvoidanceMoveTimeRemainingSeconds;
	Writer << AvoidancePathOffsetTimeRemainingSeconds << GuidanceMoveTargetTimeRemainingSeconds;
	Writer << AvoidanceSpeedMultiplier << AvoidancePathOffsetSpeedMultiplier;
	Writer << AnimationTimeSeconds << AnimationPhase;
	Writer << PedestrianSpriteColumn << PedestrianSpriteRow << PedestrianOutfitIndex;
	Writer << RouteTargetNodeIndex << RoutePrevNodeIndex << RoutePlannedNextNodeIndex;
	SerializeAgentBool(Writer, bSnapToGround);

	SerializeAgentBool(Writer, bCriminalCar);
	SerializeAgentBool(Writer, bFleeing);
	SerializeAgentBool(Writer, bStopOrdered);
	SerializeAgentBool(Writer, bStopped);
	Writer << CriminalEventId << SpotlightMark << CriminalState;
	Writer << ArrestHoldSeconds << CriminalStopScale;

	Writer << FigureMnemonic << FigureCurrentFrame << FigureFrameTime << FigureClothesOffset;
	Writer << FigureHeadIndex << ForcedFigureMnemonic << ForcedFigureClothesOffset;
	Writer << BehaviorHomeTile << SeatPortraitMood;
	Writer << BehaviorBodyRadiusUnits << BehaviorTickCounter << BehaviorTickAccumulator;
	SerializeAgentBool(Writer, bBehaviorActive);
	Writer << BehaviorStepVelocityCmPerSec << BehaviorStepTimeRemainingSeconds;
	Writer << LastAppliedBehaviorFacing;

	int32 StackCount = BehaviorContext.Stack.Num();
	Writer << StackCount;
	for (FSimCopterPersonContext::FFrame& Frame : BehaviorContext.Stack)
	{
		Writer << Frame.ProgramId << Frame.RecordIndex;
		for (uint16& Local : Frame.Locals) Writer << Local;
	}
	for (uint16& Attribute : BehaviorContext.Attributes) Writer << Attribute;
	Writer << BehaviorContext.Lfsr << BehaviorContext.PendingAnimMnemonic;
	SerializeAgentBool(Writer, BehaviorContext.bRequestDespawn);
	Writer << BehaviorContext.SelectedLocation;
	SerializeAgentBool(Writer, BehaviorContext.bHasSelection);
	SerializeAgentBool(Writer, BehaviorContext.bSelectionIsHarness);
	Writer << BehaviorContext.ActiveReactionProgramId << BehaviorContext.ReactionParameter;
	Writer << BehaviorContext.MegaphoneMessageIndex;

	auto SavedActorName = [](AActor* Actor) -> FName
	{
		if (const ASimCopterGroundAgent* Agent = Cast<ASimCopterGroundAgent>(Actor))
		{
			return Agent->GetRuntimeSaveIdentityName();
		}
		return Actor != nullptr ? Actor->GetFName() : NAME_None;
	};
	AActor* SavedCarrier = BehaviorCarrier.Get();
	if (SavedCarrier == nullptr && bMissionCarried) SavedCarrier = GetAttachParentActor();
	FName CarrierName = SavedActorName(SavedCarrier);
	FName StartingName = SavedActorName(BehaviorStartingVehicle.Get());
	FName InteractionName = SavedActorName(BehaviorInteractionSource.Get());
	FName SelectionName = SavedActorName(BehaviorContext.SelectedObject.Get());
	Writer << CarrierName << StartingName << InteractionName << SelectionName;
	SerializeAgentBool(Writer, bRidingHarness);
	SerializeAgentBool(Writer, bClaimedPassengerSeat);
	SerializeAgentBool(Writer, bBehaviorMoveSuspended);
	SerializeAgentBool(Writer, bBeamAbductionActive);

	SerializeAgentBool(Writer, bMissionWavesWhenIdle);
	SerializeAgentBool(Writer, bMissionStationary);
	SerializeAgentBool(Writer, bMissionCarried);
	SerializeAgentBool(Writer, bMissionPickupCreditAwarded);
	SerializeAgentBool(Writer, bMissionPickupCounted);
	SerializeAgentBool(Writer, bMissionResolutionReported);
	SerializeAgentBool(Writer, bMissionPatientDead);
	SerializeAgentBool(Writer, bAmbulanceHandoffPending);
	SerializeAgentBool(Writer, bPersistentHospitalRoofCrew);
	SerializeAgentBool(Writer, bHasHospitalRoofPost);
	Writer << HospitalRoofPostWorldLocation << HospitalRoofPostHalfExtentCm;
	SerializeAgentBool(Writer, bPassengerFallActive);
	SerializeAgentBool(Writer, bPassengerFallStarted);
	Writer << PassengerFallStartZ << PassengerFallInjuryDistanceCm << PassengerFallSourceEventId;
	return !Writer.IsError();
}

bool ASimCopterGroundAgent::RestoreRuntimeSaveState(const TArray<uint8>& Data)
{
	if (Data.IsEmpty()) return false;
	FMemoryReader Reader(Data, true);
	uint32 Magic = 0;
	int32 Version = 0;
	uint8 Kind = 0;
	FString SavedMesh;
	FString SavedRoot;
	float SavedMovementSpeed = 0.0f;
	FString SavedFigure;
	Reader << Magic << Version << Kind << SavedMesh << SavedRoot << SavedMovementSpeed;
	Reader << SavedFigure << InitialPersonState << InitialBehaviorClass;
	Reader << InitialBehaviorAgitation << InitialBehaviorProgramId << MissionEventId;
	FTransform Transform;
	Reader << Transform;
	if (Magic != GroundAgentRuntimeSaveMagic || Version != GroundAgentRuntimeSaveVersion ||
		Kind > static_cast<uint8>(ESimCopterGroundAgentKind::Vehicle) || SavedMovementSpeed <= 0.0f)
	{
		return false;
	}
	PedestrianFigureName = SavedFigure;
	ConfigureAgent(static_cast<ESimCopterGroundAgentKind>(Kind), SavedMesh, SavedRoot, SavedMovementSpeed);
	SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);

	Reader << MoveTargetLocation << CurrentVelocityCmPerSec << ExternalVelocityCmPerSec;
	Reader << VerticalVelocityCmPerSec << AvoidanceMoveTargetLocation << AvoidancePathOffset;
	Reader << GuidanceMoveTargetLocation;
	SerializeAgentBool(Reader, bHasMoveTarget);
	Reader << TrafficSpeedScale << AvoidanceMoveTimeRemainingSeconds;
	Reader << AvoidancePathOffsetTimeRemainingSeconds << GuidanceMoveTargetTimeRemainingSeconds;
	Reader << AvoidanceSpeedMultiplier << AvoidancePathOffsetSpeedMultiplier;
	Reader << AnimationTimeSeconds << AnimationPhase;
	Reader << PedestrianSpriteColumn << PedestrianSpriteRow << PedestrianOutfitIndex;
	Reader << RouteTargetNodeIndex << RoutePrevNodeIndex << RoutePlannedNextNodeIndex;
	SerializeAgentBool(Reader, bSnapToGround);

	SerializeAgentBool(Reader, bCriminalCar);
	SerializeAgentBool(Reader, bFleeing);
	SerializeAgentBool(Reader, bStopOrdered);
	SerializeAgentBool(Reader, bStopped);
	Reader << CriminalEventId << SpotlightMark << CriminalState;
	Reader << ArrestHoldSeconds << CriminalStopScale;

	Reader << FigureMnemonic << FigureCurrentFrame << FigureFrameTime << FigureClothesOffset;
	Reader << FigureHeadIndex << ForcedFigureMnemonic << ForcedFigureClothesOffset;
	Reader << BehaviorHomeTile << SeatPortraitMood;
	Reader << BehaviorBodyRadiusUnits << BehaviorTickCounter << BehaviorTickAccumulator;
	SerializeAgentBool(Reader, bBehaviorActive);
	Reader << BehaviorStepVelocityCmPerSec << BehaviorStepTimeRemainingSeconds;
	Reader << LastAppliedBehaviorFacing;

	int32 StackCount = 0;
	Reader << StackCount;
	if (StackCount < 0 || StackCount > FSimCopterPersonContext::MaxStackDepth) return false;
	BehaviorContext.Stack.SetNum(StackCount);
	for (FSimCopterPersonContext::FFrame& Frame : BehaviorContext.Stack)
	{
		Reader << Frame.ProgramId << Frame.RecordIndex;
		for (uint16& Local : Frame.Locals) Reader << Local;
	}
	for (uint16& Attribute : BehaviorContext.Attributes) Reader << Attribute;
	Reader << BehaviorContext.Lfsr << BehaviorContext.PendingAnimMnemonic;
	SerializeAgentBool(Reader, BehaviorContext.bRequestDespawn);
	Reader << BehaviorContext.SelectedLocation;
	SerializeAgentBool(Reader, BehaviorContext.bHasSelection);
	SerializeAgentBool(Reader, BehaviorContext.bSelectionIsHarness);
	Reader << BehaviorContext.ActiveReactionProgramId << BehaviorContext.ReactionParameter;
	Reader << BehaviorContext.MegaphoneMessageIndex;
	Reader << PendingSavedCarrierName << PendingSavedStartingVehicleName;
	Reader << PendingSavedInteractionSourceName << PendingSavedSelectionName;
	SerializeAgentBool(Reader, bRidingHarness);
	SerializeAgentBool(Reader, bClaimedPassengerSeat);
	SerializeAgentBool(Reader, bBehaviorMoveSuspended);
	SerializeAgentBool(Reader, bBeamAbductionActive);

	SerializeAgentBool(Reader, bMissionWavesWhenIdle);
	SerializeAgentBool(Reader, bMissionStationary);
	SerializeAgentBool(Reader, bMissionCarried);
	SerializeAgentBool(Reader, bMissionPickupCreditAwarded);
	SerializeAgentBool(Reader, bMissionPickupCounted);
	SerializeAgentBool(Reader, bMissionResolutionReported);
	SerializeAgentBool(Reader, bMissionPatientDead);
	SerializeAgentBool(Reader, bAmbulanceHandoffPending);
	SerializeAgentBool(Reader, bPersistentHospitalRoofCrew);
	SerializeAgentBool(Reader, bHasHospitalRoofPost);
	Reader << HospitalRoofPostWorldLocation << HospitalRoofPostHalfExtentCm;
	SerializeAgentBool(Reader, bPassengerFallActive);
	SerializeAgentBool(Reader, bPassengerFallStarted);
	Reader << PassengerFallStartZ << PassengerFallInjuryDistanceCm << PassengerFallSourceEventId;
	if (Reader.IsError() || Reader.Tell() != Reader.TotalSize()) return false;

	BehaviorContext.SelectedObject.Reset();
	BehaviorCarrier.Reset();
	BehaviorStartingVehicle.Reset();
	BehaviorInteractionSource.Reset();
	BehaviorBeamTarget.Reset();
	// The fixed ambient UFO component is relinked in ResolveRuntimeSaveReferences after every
	// traffic actor exists. It is not a behavior selection and therefore has no actor-name field.
	RefreshHeadImageIndex();
	if (!ForcedFigureMnemonic.IsEmpty())
	{
		SetForcedPedestrianFigureClip(ForcedFigureMnemonic);
	}
	else if (!FigureMnemonic.IsEmpty())
	{
		const int32 SavedFrame = FigureCurrentFrame;
		const float SavedFrameTime = FigureFrameTime;
		RebuildFigureClip(FigureMnemonic);
		FigureCurrentFrame = FMath::Clamp(SavedFrame, 0, FMath::Max(0, FigureFrameCount - 1));
		FigureFrameTime = SavedFrameTime;
		FSimCopterPopulationFigure::ShowFrame(OriginalMeshComponent, FigureFrameCount, FigureCurrentFrame, bFigureHasHeadSection);
	}
	return true;
}

void ASimCopterGroundAgent::ResolveRuntimeSaveReferences(
	const TMap<FName, AActor*>& SavedActors,
	ASimCopterHelicopterPawn* Helicopter)
{
	auto ResolveActor = [&SavedActors, Helicopter](const FName Name) -> AActor*
	{
		if (Name.IsNone()) return nullptr;
		if (Helicopter != nullptr && Helicopter->GetFName() == Name) return Helicopter;
		if (AActor* const* Found = SavedActors.Find(Name)) return *Found;
		return nullptr;
	};

	BehaviorStartingVehicle = ResolveActor(PendingSavedStartingVehicleName);
	BehaviorInteractionSource = ResolveActor(PendingSavedInteractionSourceName);
	BehaviorContext.SelectedObject = ResolveActor(PendingSavedSelectionName);
	if (bBeamAbductionActive)
	{
		ASimCopterAmbientVehiclesActor* Ambient = Cast<ASimCopterAmbientVehiclesActor>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterAmbientVehiclesActor::StaticClass()));
		BehaviorBeamTarget = Ambient != nullptr ? Ambient->GetUfoBeamTargetComponent() : nullptr;
		if (!BehaviorBeamTarget.IsValid())
		{
			bBeamAbductionActive = false;
		}
	}
	AActor* Carrier = ResolveActor(PendingSavedCarrierName);
	BehaviorCarrier = Carrier;
	if (Carrier == nullptr)
	{
		return;
	}

	SetActorEnableCollision(false);
	if (CollisionComponent != nullptr) CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	bSnapToGround = false;
	BehaviorContext.Attributes[EBhavAttr::Visible] = 0;
	if (ASimCopterHelicopterPawn* CarrierHelicopter = Cast<ASimCopterHelicopterPawn>(Carrier))
	{
		if (bClaimedPassengerSeat)
		{
			AttachToComponent(CarrierHelicopter->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
			SetActorHiddenInGame(true);
			CarrierHelicopter->RelinkSavedMissionPassenger(this, RuntimeSaveIdentityName);
		}
		else
		{
			SetActorHiddenInGame(false);
			UpdateCarriedTransform();
		}
	}
	else
	{
		AttachToComponent(Carrier->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
		SetActorHiddenInGame(false);
	}
}

bool ASimCopterGroundAgent::LoadOriginalMeshFromOriginalGameRoot()
{
	if (AgentKind == ESimCopterGroundAgentKind::Pedestrian && FSimCopterPopulationSprite::IsPeople1Name(MeshTableName))
	{
		return LoadOriginalPedestrianSpriteFromOriginalGameRoot();
	}

	LastMeshLoadError.Reset();
	OriginalMeshComponent->ClearAllMeshSections();
	bUsingPedestrianSprite = false;
	// BeginPlay may have built a pedestrian figure before ConfigureAgent retyped this agent
	// (SpawnActor runs BeginPlay with the default kind); drop that state or the behavior VM
	// rebuilds person clips over the vehicle mesh.
	bUsingPedestrianBody = false;
	bUsingPedestrianFigure = false;

	const FString RootPath = ResolveOriginalGameRoot();
	if (RootPath.IsEmpty())
	{
		LastMeshLoadError = TEXT("Original game root is empty.");
		ShowOriginalMesh(false);
		return false;
	}

	FMaxisMeshLibrary MeshLibrary;
	FString Error;
	if (!MeshLibrary.LoadFromOriginalGameRoot(RootPath, Error))
	{
		LastMeshLoadError = Error;
		UE_LOG(LogSimCopterGroundAgent, Warning, TEXT("%s"), *LastMeshLoadError);
		ShowOriginalMesh(false);
		return false;
	}

	const TArray<FColor>* ColorMap = nullptr;
	const FMaxisMeshObject* MeshObject = MeshLibrary.FindObjectByTableName(MeshTableName, &ColorMap);
	if (MeshObject == nullptr)
	{
		LastMeshLoadError = FString::Printf(TEXT("Could not find ground-agent mesh '%s' in '%s'."), *MeshTableName, *RootPath);
		UE_LOG(LogSimCopterGroundAgent, Warning, TEXT("%s"), *LastMeshLoadError);
		ShowOriginalMesh(false);
		return false;
	}

	const float ModelScale = AgentKind == ESimCopterGroundAgentKind::Vehicle ? VehicleModelScale : PedestrianModelScale;
	const FLinearColor FallbackColor = AgentKind == ESimCopterGroundAgentKind::Vehicle
		? FLinearColor(0.33f, 0.34f, 0.35f)
		: FLinearColor(0.72f, 0.63f, 0.52f);

	// The original cars carry translucent "headlight beam" cards (Maxis face type 11) projecting
	// off the front of the body. Route those into a throwaway translucent section so they are
	// dropped from the rendered vehicle - real spotlights below take their place. (Without this
	// the single-section build renders them as opaque grey/blue blocks: the "opaque headlights".)
	FMaxisMeshSection MeshSection;
	FMaxisMeshSection DiscardedBeamSection;
	FMaxisProceduralMeshBuilder::BuildPaletteColoredSections(
		*MeshObject,
		ColorMap,
		ModelUnitsPerCentimeter,
		ModelScale,
		bRenderModelBackfaces,
		FallbackColor,
		MeshSection,
		&DiscardedBeamSection);

	if (MeshSection.IsEmpty())
	{
		LastMeshLoadError = FString::Printf(TEXT("Ground-agent mesh '%s' built no triangles."), *MeshTableName);
		UE_LOG(LogSimCopterGroundAgent, Warning, TEXT("%s"), *LastMeshLoadError);
		ShowOriginalMesh(false);
		return false;
	}

	OriginalMeshComponent->CreateMeshSection_LinearColor(
		0,
		MeshSection.Vertices,
		MeshSection.Triangles,
		MeshSection.Normals,
		MeshSection.UVs,
		MeshSection.VertexColors,
		MeshSection.Tangents,
		false);

	if (VertexColorMaterial != nullptr)
	{
		OriginalMeshComponent->SetMaterial(0, VertexColorMaterial);
	}

	const float HalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	OriginalMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -HalfHeight - MeshSection.LocalBounds.Min.Z));
	ShowOriginalMesh(true);

	if (AgentKind == ESimCopterGroundAgentKind::Vehicle)
	{
		ConfigureVehicleHeadlights(MeshSection.LocalBounds);
	}
	else
	{
		DisableVehicleHeadlights();
	}

	return true;
}

bool ASimCopterGroundAgent::LoadOriginalPedestrianSpriteFromOriginalGameRoot()
{
	LastMeshLoadError.Reset();
	bUsingPedestrianSprite = false;
	bUsingPedestrianFigure = false;
	PedestrianSpriteTexture = nullptr;
	SpriteMaterialInstance = nullptr;
	OriginalMeshComponent->ClearAllMeshSections();

	const FString RootPath = ResolveOriginalGameRoot();
	if (RootPath.IsEmpty())
	{
		LastMeshLoadError = TEXT("Original game root is empty.");
		ShowOriginalMesh(false);
		return false;
	}

	if (SpriteMaterial == nullptr)
	{
		SpriteMaterial = LoadSpriteMaterialNoWarn();
	}

	if (SpriteMaterial == nullptr)
	{
		LastMeshLoadError = FString::Printf(TEXT("Missing %s for original pedestrian sprites."), SpriteMaterialPath);
		UE_LOG(LogSimCopterGroundAgent, Warning, TEXT("%s"), *LastMeshLoadError);
		ShowOriginalMesh(false);
		return false;
	}

	UTexture2D* LoadedTexture = nullptr;
	FString Error;
	if (!FSimCopterPopulationSprite::LoadPeople1Texture(this, RootPath, LoadedTexture, Error))
	{
		LastMeshLoadError = Error;
		UE_LOG(LogSimCopterGroundAgent, Warning, TEXT("%s"), *LastMeshLoadError);
		ShowOriginalMesh(false);
		return false;
	}

	PedestrianSpriteTexture = LoadedTexture;
	PedestrianSpriteColumn = FSimCopterPopulationSprite::ResolvePeople1Column(MeshTableName, this);
	PedestrianSpriteRow = 0;
	FSimCopterPopulationSprite::BuildPeople1FrameQuad(OriginalMeshComponent, PedestrianSpriteColumn, PedestrianSpriteRow, PedestrianSpriteHeightCm);

	SpriteMaterialInstance = UMaterialInstanceDynamic::Create(SpriteMaterial, this);
	if (SpriteMaterialInstance != nullptr)
	{
		SpriteMaterialInstance->SetTextureParameterValue(TEXT("Texture"), PedestrianSpriteTexture);
		OriginalMeshComponent->SetMaterial(0, SpriteMaterialInstance);
	}
	RefreshSpriteExposure();

	const float HalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	OriginalMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -HalfHeight));
	bUsingPedestrianSprite = true;
	ShowOriginalMesh(true);
	return true;
}

bool ASimCopterGroundAgent::BuildPedestrianBody()
{
	if (BuildPedestrianFigure())
	{
		return true;
	}

	UE_LOG(LogSimCopterGroundAgent, Error, TEXT("Failed to build pedestrian figure: %s"), *LastMeshLoadError);
	return false;
}

bool ASimCopterGroundAgent::BuildPedestrianFigure()
{
	LastMeshLoadError.Reset();
	bUsingPedestrianSprite = false;
	bUsingPedestrianBody = false;
	bUsingPedestrianFigure = false;
	PedestrianSpriteTexture = nullptr;
	SpriteMaterialInstance = nullptr;
	FigureHeadTexture = nullptr;
	FigureHeadMaterialInstance = nullptr;
	DisableVehicleHeadlights();

	const FString RootPath = ResolveOriginalGameRoot();
	if (RootPath.IsEmpty())
	{
		LastMeshLoadError = TEXT("Original game root is empty.");
		return false;
	}

	FString Error;
	FigureShared = FSimCopterPopulationFigure::GetShared(RootPath, Error);
	if (!FigureShared.IsValid())
	{
		LastMeshLoadError = Error;
		UE_LOG(LogSimCopterGroundAgent, Warning, TEXT("privanim figures unavailable: %s"), *Error);
		return false;
	}

	// Stable per-agent choices so a pedestrian keeps its identity for its whole life.
	const uint32 Hash = GetTypeHash(GetFName());
	FString FigureName = PedestrianFigureName;
	if (FigureName.IsEmpty() && PedestrianFigurePool.Num() > 0)
	{
		FigureName = PedestrianFigurePool[Hash % uint32(PedestrianFigurePool.Num())];
	}
	FigureIndex = FigureShared->Model.FindFigureIndex(FigureName);
	if (FigureIndex == INDEX_NONE)
	{
		LastMeshLoadError = FString::Printf(TEXT("Figure '%s' not found in privanim.df."), *FigureName);
		UE_LOG(LogSimCopterGroundAgent, Warning, TEXT("%s"), *LastMeshLoadError);
		return false;
	}
	FigureClothesOffset = ForcedFigureClothesOffset != INDEX_NONE
		? ForcedFigureClothesOffset
		: int32((Hash / 7u) % 14u);

	// Head sprite. FUN_004c71c0 binds one head per behavior class and FUN_004c7090 swaps in head
	// 10 for a state-6 medevac victim, so the head is decided by who this person is - not rolled.
	// Picking it from a hash is what put SIM3D image 0x43, the bandaged head, on healthy
	// pedestrians while the actual casualties wore whatever came up.
	FigureHeadIndex = ResolveHeadImageIndex();
	const TArray<int32>& HeadTable = FSimCopterPopulationFigure::GetHeadImageTable();
	if (FigureHeadMaterial == nullptr)
	{
		FigureHeadMaterial = LoadFigureHeadMaterialNoWarn();
	}
	if (FigureHeadMaterial != nullptr)
	{
		if (const FMaxisTextureImage* HeadImage = FigureShared->HeadImages.Find(HeadTable[FigureHeadIndex]))
		{
			FigureHeadTexture = FSimCopterPopulationSprite::CreateTextureFromImage(this, *HeadImage, TEXT("SimCopterFigureHead"));
		}
		if (FigureHeadTexture != nullptr)
		{
			FigureHeadMaterialInstance = UMaterialInstanceDynamic::Create(FigureHeadMaterial, this);
			if (FigureHeadMaterialInstance != nullptr)
			{
				FigureHeadMaterialInstance->SetTextureParameterValue(TEXT("Texture"), FigureHeadTexture);
				// The material's default 1.0 flattens a card's normal to world up, which is right for
				// the crossed vertical quads it was written for (a tree) and wrong here: AppendBall
				// gives the head real normals, and shading it off them is what makes it turn with the
				// sun in step with the vertex-coloured body it sits on.
				FigureHeadMaterialInstance->SetScalarParameterValue(TEXT("CardNormalUpBias"), 0.0f);
			}
		}
	}
	RefreshSpriteExposure();

	bUsingPedestrianFigure = true; // set before the clip build so RebuildFigureClip can run
	if (!RebuildFigureClip(TEXT("NoMo")))
	{
		bUsingPedestrianFigure = false;
		return false;
	}

	const float HalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	// Feet sit at the capsule bottom; the figure is built from Z=0 (feet) upward.
	OriginalMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -HalfHeight));
	ShowOriginalMesh(true);
	return true;
}

// SCHOOK: PersonHeadImage 0x004c71c0 (its `local_4`) + 0x004c7090's state-6 override
int32 ASimCopterGroundAgent::ResolveHeadImageIndex() const
{
	const int32 HeadCount = FSimCopterPopulationFigure::GetHeadImageTable().Num();
	// Once the VM owns this person, attribute 39 is the head - BHAV 264 reads it too, so the
	// portrait and the body must agree on one value rather than each deriving its own.
	const int32 Head = bBehaviorActive
		? int32(BehaviorContext.Attributes[EBhavAttr::HeadImageIndex])
		: (InitialPersonState == 6
			? FSimCopterPeopleCityRules::MedevacVictimHeadImageIndex
			: FSimCopterPeopleCityRules::GetHeadImageIndexForBehaviorClass(InitialBehaviorClass));
	return FMath::Clamp(Head, 0, FMath::Max(0, HeadCount - 1));
}

void ASimCopterGroundAgent::RefreshHeadImageIndex()
{
	const int32 Head = ResolveHeadImageIndex();
	if (Head == FigureHeadIndex)
	{
		return;
	}
	FigureHeadIndex = Head;

	// Only the texture in the head section's material changes; the figure, its clip and its
	// vertex data are untouched, so there is nothing to rebuild.
	if (!bUsingPedestrianFigure || !FigureShared.IsValid() || FigureHeadMaterialInstance == nullptr)
	{
		return;
	}
	const TArray<int32>& HeadTable = FSimCopterPopulationFigure::GetHeadImageTable();
	if (const FMaxisTextureImage* HeadImage = FigureShared->HeadImages.Find(HeadTable[FigureHeadIndex]))
	{
		FigureHeadTexture = FSimCopterPopulationSprite::CreateTextureFromImage(this, *HeadImage, TEXT("SimCopterFigureHead"));
		if (FigureHeadTexture != nullptr)
		{
			FigureHeadMaterialInstance->SetTextureParameterValue(TEXT("Texture"), FigureHeadTexture);
		}
	}
}

bool ASimCopterGroundAgent::RebuildFigureClip(const FString& Mnemonic)
{
	if (!bUsingPedestrianFigure || !FigureShared.IsValid() || !FigureShared->Model.Figures.IsValidIndex(FigureIndex))
	{
		return false;
	}
	const FPrivAnimFigure& Figure = FigureShared->Model.Figures[FigureIndex];

	// FUN_004c68f0: figures keyed '2DOG'/'Coww' substitute their quadruped clips -
	// 1Wal/1Run/Tote play DgRn, everything else DgSt.
	FString EffectiveMnemonic = Mnemonic;
	if (Figure.Name.StartsWith(TEXT("2DOG")) || Figure.Name.StartsWith(TEXT("Coww")))
	{
		EffectiveMnemonic = (Mnemonic == TEXT("1Wal") || Mnemonic == TEXT("1Run") || Mnemonic == TEXT("Tote"))
			? TEXT("DgRn")
			: TEXT("DgSt");
	}

	const FPrivAnimClip* Clip = FigureShared->Model.FindClip(Figure, EffectiveMnemonic);
	if (Clip == nullptr)
	{
		Clip = FigureShared->Model.FindClip(Figure, TEXT("NoMo"));
	}
	if (Clip == nullptr)
	{
		LastMeshLoadError = FString::Printf(TEXT("Figure '%s' has no clip for '%s'."), *Figure.Name, *Mnemonic);
		return false;
	}

	const float HeightCm = PedestrianBodyHeightCm * PopulationWorldScale;
	// Calibrate model units from the standing walk clip so poses like "Dead" keep their scale.
	const FPrivAnimClip* StandingClip = FigureShared->Model.FindClip(Figure, TEXT("1Wal"));
	FigureCalibration = FSimCopterPopulationFigure::Calibrate(StandingClip != nullptr ? *StandingClip : *Clip, HeightCm);

	FSimCopterPopulationFigure::FBuildParams Params;
	Params.HeightCm = HeightCm;
	Params.ClothesOffset = FigureClothesOffset;
	Params.bTexturedHead = FigureHeadMaterialInstance != nullptr;

	if (!FSimCopterPopulationFigure::BuildClipSections(
			OriginalMeshComponent, Figure, *Clip, FigureShared->Palette, Params, FigureCalibration, bFigureHasHeadSection))
	{
		LastMeshLoadError = FString::Printf(TEXT("Failed to build figure '%s' clip '%s'."), *Figure.Name, *Clip->Name);
		return false;
	}

	for (int32 Frame = 0; Frame < Clip->FrameCount; ++Frame)
	{
		if (VertexColorMaterial != nullptr)
		{
			OriginalMeshComponent->SetMaterial(Frame * 2, VertexColorMaterial);
		}
		OriginalMeshComponent->SetMaterial(
			Frame * 2 + 1,
			FigureHeadMaterialInstance != nullptr ? static_cast<UMaterialInterface*>(FigureHeadMaterialInstance) : VertexColorMaterial.Get());
	}

	FigureMnemonic = Mnemonic;
	FigureFrameCount = Clip->FrameCount;
	FigureCurrentFrame = 0;
	FigureFrameTime = 0.0f;
	FSimCopterPopulationFigure::ShowFrame(OriginalMeshComponent, FigureFrameCount, 0, bFigureHasHeadSection);
	return true;
}

void ASimCopterGroundAgent::UpdateFigureAnimation(float DeltaSeconds, float SpeedAlpha)
{
	// The original figures animate through whole-pose frames - no lean/bob overlay.
	VisualRoot->SetRelativeRotation(FRotator::ZeroRotator);
	SetVisualRootRelativeLocation(FVector::ZeroVector);

	if (!ForcedFigureMnemonic.IsEmpty())
	{
		if (FigureMnemonic != ForcedFigureMnemonic)
		{
			RebuildFigureClip(ForcedFigureMnemonic);
		}
		return;
	}

	// With the behavior VM active, clips are bound by the programs/post-move selector and
	// frames advance one per behavior tick (FUN_004c6450) in UpdateOriginalBehavior.
	if (bBehaviorActive)
	{
		return;
	}

	const bool bWalking = SpeedAlpha > 0.12f;
	// Off-program victims (treading water beside the capsized boat, riding the runaway train's
	// roof) have no behavior VM to bind clips for them, so the wave is chosen here instead. Same
	// clip as the VM path above, for the same reason: "WvNo" is the greeting, "Wave" is the panic.
	const bool bWaving = !bWalking && bMissionWavesWhenIdle;
	{
		const FString Desired = bWalking ? TEXT("1Wal") : (bWaving ? TEXT("WvNo") : TEXT("NoMo"));
		if (Desired != FigureMnemonic)
		{
			RebuildFigureClip(Desired);
		}
	}
	if (FigureFrameCount <= 1)
	{
		return;
	}

	// Idle clips tick at half rate, matching the original's lazier off-screen cadence; a wave is
	// a deliberate signal, so it plays at full rate.
	FigureFrameTime += DeltaSeconds * ((bWalking || bWaving) ? FigureFrameRate : FigureFrameRate * 0.5f);
	const int32 DesiredFrame = FMath::FloorToInt(FigureFrameTime) % FigureFrameCount;
	if (DesiredFrame != FigureCurrentFrame)
	{
		FigureCurrentFrame = DesiredFrame;
		FSimCopterPopulationFigure::ShowFrame(OriginalMeshComponent, FigureFrameCount, FigureCurrentFrame, bFigureHasHeadSection);
	}
}

void ASimCopterGroundAgent::ConfigureVehicleHeadlights(const FBox& VehicleLocalBounds)
{
	if (HeadlightLeft == nullptr || HeadlightRight == nullptr)
	{
		return;
	}

	const float HalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	// Positions are in VisualRoot space. The car's nose is the mesh's max-X; headlights sit just
	// above the ground at the front corners and aim forward (+X), slightly downward.
	const float FrontX = VehicleLocalBounds.Max.X;
	const float SideY = FMath::Max(VehicleLocalBounds.Max.Y, 10.0f) * 0.62f;
	const float BodyHeight = VehicleLocalBounds.Max.Z - VehicleLocalBounds.Min.Z;
	const float HeadlightZ = -HalfHeight + FMath::Clamp(BodyHeight * 0.25f, 8.0f, 30.0f);
	const FRotator BeamRotation(-9.0f, 0.0f, 0.0f);

	USpotLightComponent* Lights[2] = {HeadlightLeft, HeadlightRight};
	const float SideSigns[2] = {-1.0f, 1.0f};
	for (int32 Index = 0; Index < 2; ++Index)
	{
		USpotLightComponent* Light = Lights[Index];
		Light->SetRelativeLocation(FVector(FrontX, SideSigns[Index] * SideY, HeadlightZ));
		Light->SetRelativeRotation(BeamRotation);
		Light->SetIntensity(HeadlightIntensity);
		// Exposure independent, like every other gameplay light here: 9,000 unitless is ~14 candelas
		// once the engine converts it, and the level's day sequence runs the sun at 120,000 lux, so
		// the raw beam contributes nothing a tonemapper can show. See SearchLightExposureCompensation.
		Light->SetInverseExposureBlend(1.0f);
		// ...and because that is a direct-lighting trick Lumen never sees, the beam needs its own
		// indirect scale or it bounces nothing. See HeadlightIndirectLightingIntensity.
		Light->SetIndirectLightingIntensity(HeadlightIndirectLightingIntensity);
		Light->SetAttenuationRadius(HeadlightAttenuationRadiusCm);
		Light->SetInnerConeAngle(11.0f);
		Light->SetOuterConeAngle(26.0f);
		Light->SetLightColor(FLinearColor(HeadlightColor));
	}

	bVehicleHeadlightsConfigured = true;
	RefreshHeadlightVisibility();

	// Subscribed here rather than in BeginPlay so only vehicles are on the list: the city's agents
	// are mostly pedestrians, and they have no headlights to refresh.
	if (!LowPowerChangedHandle.IsValid())
	{
		LowPowerChangedHandle = SimCopterLowPower::OnChanged().AddWeakLambda(
			this, [this](bool) { RefreshHeadlightVisibility(); });
	}
}

void ASimCopterGroundAgent::RefreshHeadlightVisibility()
{
	const bool bVisible =
		bVehicleHeadlightsConfigured && bEnableVehicleHeadlights && !SimCopterLowPower::IsEnabled();

	if (HeadlightLeft != nullptr)
	{
		HeadlightLeft->SetVisibility(bVisible);
	}
	if (HeadlightRight != nullptr)
	{
		HeadlightRight->SetVisibility(bVisible);
	}
}

void ASimCopterGroundAgent::RefreshSpriteExposure()
{
	// Only the PEOPLE1 billboard is still unlit and still needs its brightness computed. The figure
	// head moved to the lit card material and is shaded by the scene like everything else, so it has
	// no EmissiveNits to write - which is the point, not an omission.
	USimCopterEffectExposureSubsystem::ApplyEmissiveNits(
		SpriteMaterialInstance, GetWorld(), /*bIsLightSource=*/false);
}

void ASimCopterGroundAgent::DisableVehicleHeadlights()
{
	bVehicleHeadlightsConfigured = false;
	if (HeadlightLeft != nullptr)
	{
		HeadlightLeft->SetVisibility(false);
	}
	if (HeadlightRight != nullptr)
	{
		HeadlightRight->SetVisibility(false);
	}
}

void ASimCopterGroundAgent::SnapToGroundImmediate()
{
	FVector GroundedLocation;
	if (TraceGround(GroundedLocation))
	{
		SetActorLocation(GroundedLocation, false);
	}
}

void ASimCopterGroundAgent::SetMoveTarget(const FVector& NewTargetLocation)
{
	MoveTargetLocation = NewTargetLocation;
	GuidanceMoveTargetTimeRemainingSeconds = 0.0f;
	GuidanceMoveSpeedCmPerSec = 0.0f;
	bHasMoveTarget = true;
}

void ASimCopterGroundAgent::ClearMoveTarget()
{
	bHasMoveTarget = false;
	CurrentVelocityCmPerSec = FVector::ZeroVector;
	AvoidancePathOffsetTimeRemainingSeconds = 0.0f;
	AvoidancePathOffsetSpeedMultiplier = 1.0f;
	GuidanceMoveTargetTimeRemainingSeconds = 0.0f;
	GuidanceMoveSpeedCmPerSec = 0.0f;
}

bool ASimCopterGroundAgent::IsNearMoveTarget(float DistanceCm) const
{
	if (IsAvoidanceMoveActive())
	{
		return false;
	}

	if (!bHasMoveTarget)
	{
		return true;
	}

	const FVector EffectiveTarget = IsAvoidancePathOffsetActive() ? MoveTargetLocation + AvoidancePathOffset : MoveTargetLocation;
	const FVector Delta = EffectiveTarget - GetActorLocation();
	return FVector(Delta.X, Delta.Y, 0.0f).SizeSquared() <= FMath::Square(DistanceCm);
}

float ASimCopterGroundAgent::GetCollisionRadiusCm() const
{
	return CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleRadius() : 0.0f;
}

void ASimCopterGroundAgent::SetTrafficSpeedScale(float NewSpeedScale)
{
	TrafficSpeedScale = FMath::Clamp(NewSpeedScale, 0.0f, 1.75f);
}

void ASimCopterGroundAgent::LimitTrafficSpeedScale(float MaxSpeedScale)
{
	TrafficSpeedScale = FMath::Min(TrafficSpeedScale, FMath::Clamp(MaxSpeedScale, 0.0f, 1.75f));
}

void ASimCopterGroundAgent::MakeCriminalCar(const int32 InEventId)
{
	// FUN_004b8540: the pool slot is claimed, the mission event id recorded at +0x113, and the
	// state machine starts at 0. The fleeing flag is what makes FUN_0049dab0 accept it, and a
	// criminal car flies it from the moment it is placed.
	bCriminalCar = true;
	bFleeing = true;
	bStopOrdered = false;
	bStopped = false;
	CriminalEventId = InEventId;
	SpotlightMark = 0;
	CriminalState = static_cast<uint8>(SimCopterCriminalCar::EState::Cruising);
	ArrestHoldSeconds = 0.0f;
}

bool ASimCopterGroundAgent::TryOrderStop(const int32 CallerMessageId)
{
	if (!bCriminalCar)
	{
		return false;
	}

	const bool bAccepted = SimCopterCriminalCar::AcceptsStopOrder(
		static_cast<SimCopterCriminalCar::EState>(CriminalState),
		SpotlightMark,
		CallerMessageId,
		bStopOrdered || bStopped);
	if (!bAccepted)
	{
		return false;
	}

	// FUN_0049e0c0: raise the stopping flag and drop the moving ones. The car keeps its route -
	// it decelerates along it rather than stopping dead, which is what the stop *distance* at
	// +0xd3 buys. CriminalStopScale starts from whatever it was running at and coasts down.
	bStopOrdered = true;
	CriminalStopScale = TrafficSpeedScale;
	CriminalState = static_cast<uint8>(SimCopterCriminalCar::EState::Stopping);
	return true;
}

void ASimCopterGroundAgent::ApplyTrafficBrake(float MaxSpeedScale, float DeltaSeconds, float BrakeRate)
{
	const float ClampedSpeedScale = FMath::Clamp(MaxSpeedScale, 0.0f, 1.75f);
	LimitTrafficSpeedScale(ClampedSpeedScale);

	const float BrakeAlpha = FMath::Clamp(1.0f - ClampedSpeedScale, 0.0f, 1.0f);
	const float EffectiveBrakeRate = FMath::Max(0.0f, BrakeRate) * BrakeAlpha;
	if (EffectiveBrakeRate <= 0.0f || DeltaSeconds <= 0.0f)
	{
		return;
	}

	CurrentVelocityCmPerSec = FMath::VInterpTo(CurrentVelocityCmPerSec, FVector::ZeroVector, DeltaSeconds, EffectiveBrakeRate);
	ExternalVelocityCmPerSec = FMath::VInterpTo(ExternalVelocityCmPerSec, FVector::ZeroVector, DeltaSeconds, EffectiveBrakeRate);
}

void ASimCopterGroundAgent::AddTrafficVelocityImpulse(const FVector& ImpulseCmPerSec)
{
	ExternalVelocityCmPerSec += FVector(ImpulseCmPerSec.X, ImpulseCmPerSec.Y, 0.0f);
}

void ASimCopterGroundAgent::MoveByTrafficSeparation(const FVector& WorldDelta)
{
	if (!WorldDelta.IsNearlyZero())
	{
		AddActorWorldOffset(WorldDelta, false);
		if (bSnapToGround)
		{
			// Instant re-settle after a horizontal separation nudge (mostly vehicles); the per-tick
			// UpdateGroundSnap handles pedestrian gravity.
			UpdateGroundSnap(0.0f);
		}
	}
}

void ASimCopterGroundAgent::SetAvoidanceMoveTarget(const FVector& NewTargetLocation, float DurationSeconds, float SpeedMultiplier)
{
	AvoidanceMoveTargetLocation = NewTargetLocation;
	AvoidanceMoveTimeRemainingSeconds = FMath::Max(0.0f, DurationSeconds);
	AvoidanceSpeedMultiplier = FMath::Clamp(SpeedMultiplier, 0.25f, 2.5f);
}

void ASimCopterGroundAgent::SetAvoidancePathOffset(const FVector& NewWorldOffset, float DurationSeconds, float SpeedMultiplier)
{
	AvoidancePathOffset = FVector(NewWorldOffset.X, NewWorldOffset.Y, 0.0f);
	AvoidancePathOffsetTimeRemainingSeconds = FMath::Max(0.0f, DurationSeconds);
	AvoidancePathOffsetSpeedMultiplier = FMath::Clamp(SpeedMultiplier, 0.25f, 2.5f);
}

void ASimCopterGroundAgent::SetGuidanceMoveTarget(
	const FVector& NewTargetLocation,
	const float DurationSeconds,
	const float SpeedCmPerSec)
{
	GuidanceMoveTargetLocation = NewTargetLocation;
	GuidanceMoveTargetTimeRemainingSeconds = FMath::Max(0.0f, DurationSeconds);
	GuidanceMoveSpeedCmPerSec = FMath::Max(0.0f, SpeedCmPerSec);
}

bool ASimCopterGroundAgent::SetForcedPedestrianFigureClip(const FString& Mnemonic)
{
	ForcedFigureMnemonic = Mnemonic;
	if (bUsingPedestrianFigure)
	{
		return RebuildFigureClip(Mnemonic);
	}
	return false;
}

void ASimCopterGroundAgent::ClearForcedPedestrianFigureClip()
{
	ForcedFigureMnemonic.Reset();
}

void ASimCopterGroundAgent::SetMissionInjuredPose()
{
	bMissionPatientDead = false;
	bMissionStationary = true;
	BehaviorStepVelocityCmPerSec = FVector::ZeroVector;
	BehaviorStepTimeRemainingSeconds = 0.0f;
	CurrentVelocityCmPerSec = FVector::ZeroVector;
	ExternalVelocityCmPerSec = FVector::ZeroVector;
	ClearMoveTarget();

	// An injured person is a medevac victim, and a medevac victim is BHAV 800 "Medevac initbhav":
	// it binds "Dead" itself and then runs 280 "Medevac sim", which decays their health (281),
	// kills them when it runs out (312), posts EVT_VictimPickedUp when they notice they are
	// aboard, and posts EVT_MedevacDelivered once they are set down on a hospital tile (282,
	// which is opcode 25 against XBLD 209 plus opcode 56). Freezing the VM here - which is what
	// this used to do - threw all of that away and left a prop lying on the pavement.
	if (bBehaviorActive && BehaviorModel.IsValid())
	{
		ClearForcedPedestrianFigureClip();
		BehaviorContext.ResetToState(6);
		// attr34 is person+0x184: BHAV 281 drains it and BHAV 280 kills the victim below 1.
		// FUN_004c4190's successful non-mode-4 spawn path explicitly writes 100 to +0x184 after
		// configuring the person. The previous 58 was inferred from an unrelated BHAV constant
		// and made patients substantially more fragile than the executable does.
		if (BehaviorContext.Attributes[EBhavAttr::MedevacHealth] == 0)
		{
			BehaviorContext.Attributes[EBhavAttr::MedevacHealth] = 100;
		}
	}
	else
	{
		SetForcedPedestrianFigureClip(TEXT("Dead"));
	}

	if (!bUsingPedestrianFigure && VisualRoot != nullptr)
	{
		VisualRoot->SetRelativeRotation(FRotator(0.0f, 0.0f, 86.0f));
	}
}

void ASimCopterGroundAgent::SetMissionDeadPose()
{
	bMissionPatientDead = true;
	bMissionStationary = true;
	bMissionCarried = false;
	BehaviorStepVelocityCmPerSec = FVector::ZeroVector;
	BehaviorStepTimeRemainingSeconds = 0.0f;
	CurrentVelocityCmPerSec = FVector::ZeroVector;
	ExternalVelocityCmPerSec = FVector::ZeroVector;
	ClearMoveTarget();
	bBehaviorActive = false;

	const bool bAboardCabin =
		bClaimedPassengerSeat &&
		Cast<ASimCopterHelicopterPawn>(BehaviorCarrier.Get()) != nullptr;
	bBehaviorMoveSuspended = bAboardCabin;
	bSnapToGround = !bAboardCabin;
	SetActorEnableCollision(!bAboardCabin);
	if (CollisionComponent != nullptr)
	{
		CollisionComponent->SetCollisionEnabled(
			bAboardCabin ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	}
	SetForcedPedestrianFigureClip(TEXT("Dead"));

	if (!bUsingPedestrianFigure && VisualRoot != nullptr)
	{
		VisualRoot->SetRelativeRotation(FRotator(0.0f, 0.0f, 86.0f));
	}
}

void ASimCopterGroundAgent::ClearMissionPose()
{
	bMissionStationary = false;
	bMissionWavesWhenIdle = false;
	bMissionPatientDead = false;
	// Back on the map, so back in everyone else's object searches.
	BehaviorContext.Attributes[EBhavAttr::Visible] = 1;
	bMissionCarried = false;
	bSnapToGround = true;
	ClearForcedPedestrianFigureClip();
	SetActorEnableCollision(true);
	if (CollisionComponent != nullptr)
	{
		CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	if (VisualRoot != nullptr)
	{
		VisualRoot->SetRelativeRotation(FRotator::ZeroRotator);
		SetVisualRootRelativeLocation(FVector::ZeroVector);
	}
}

void ASimCopterGroundAgent::ResumeNormalPedestrianBehavior()
{
	if (AgentKind != ESimCopterGroundAgentKind::Pedestrian)
	{
		return;
	}

	bMissionStationary = false;
	bMissionCarried = false;
	bMissionWavesWhenIdle = false;
	ClearMoveTarget();
	CurrentVelocityCmPerSec = FVector::ZeroVector;
	ExternalVelocityCmPerSec = FVector::ZeroVector;
	BehaviorStepVelocityCmPerSec = FVector::ZeroVector;
	BehaviorStepTimeRemainingSeconds = 0.0f;
	StartOriginalBehavior();
}

void ASimCopterGroundAgent::ResumeSuspendedPedestrianBehavior()
{
	if (AgentKind != ESimCopterGroundAgentKind::Pedestrian)
	{
		return;
	}

	bMissionStationary = false;
	bMissionCarried = false;
	bMissionWavesWhenIdle = false;
	bBehaviorMoveSuspended = false;
	bSnapToGround = true;
	ClearMoveTarget();
	CurrentVelocityCmPerSec = FVector::ZeroVector;
	ExternalVelocityCmPerSec = FVector::ZeroVector;
	BehaviorStepVelocityCmPerSec = FVector::ZeroVector;
	BehaviorStepTimeRemainingSeconds = 0.0f;
	SetActorHiddenInGame(false);
	BehaviorContext.Attributes[EBhavAttr::Visible] = 1;

	if (BehaviorModel.IsValid() && bUseOriginalBehaviors && BehaviorContext.Stack.Num() > 0)
	{
		bBehaviorActive = true;
	}
	else
	{
		StartOriginalBehavior();
	}
}

void ASimCopterGroundAgent::SetDroppedInjuredOnGround(const FVector& WorldLocation)
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	bMissionCarried = false;
	BehaviorContext.Attributes[EBhavAttr::Visible] = 1;
	SetActorEnableCollision(true);
	if (CollisionComponent != nullptr)
	{
		CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	if (VisualRoot != nullptr)
	{
		VisualRoot->SetRelativeRotation(FRotator::ZeroRotator);
		SetVisualRootRelativeLocation(FVector::ZeroVector);
	}
	SetActorLocation(WorldLocation, false);
	bSnapToGround = true;
	// Leave them lying injured on the ground, ready to be picked up again.
	SetMissionInjuredPose();
	// Dropped, not placed: UpdateGroundSnap accelerates them down from wherever they were let go
	// of. Snapping here removed the drop entirely, however high the carrier was holding them.
	VerticalVelocityCmPerSec = 0.0f;
}

void ASimCopterGroundAgent::BeginPassengerFall(int32 SourceEventId, float InjuryDistanceCm)
{
	bPassengerFallActive = true;
	bPassengerFallStarted = false;
	PassengerFallStartZ = GetActorLocation().Z;
	PassengerFallInjuryDistanceCm = FMath::Max(1.0f, InjuryDistanceCm);
	PassengerFallSourceEventId = SourceEventId;
	bMissionStationary = false;
	bMissionCarried = false;
	bSnapToGround = true;
	bBehaviorActive = false;
	BehaviorStepVelocityCmPerSec = FVector::ZeroVector;
	BehaviorStepTimeRemainingSeconds = 0.0f;
	CurrentVelocityCmPerSec = FVector::ZeroVector;
	ExternalVelocityCmPerSec = FVector::ZeroVector;
	VerticalVelocityCmPerSec = 0.0f;
	SetActorEnableCollision(true);
	if (CollisionComponent != nullptr)
	{
		CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	if (VisualRoot != nullptr)
	{
		VisualRoot->SetRelativeRotation(FRotator::ZeroRotator);
		SetVisualRootRelativeLocation(FVector::ZeroVector);
	}
	SetForcedPedestrianFigureClip(TEXT("Inju"));
}

void ASimCopterGroundAgent::SetMissionScriptedMover()
{
	bBehaviorActive = false;
	bMissionStationary = false;
	bMissionCarried = false;
	// The owner keeps a scripted mover on a chosen plane (e.g. the helicopter's landing surface),
	// so it must not snap to the terrain underneath.
	bSnapToGround = false;
	BehaviorStepVelocityCmPerSec = FVector::ZeroVector;
	BehaviorStepTimeRemainingSeconds = 0.0f;
	CurrentVelocityCmPerSec = FVector::ZeroVector;
	ExternalVelocityCmPerSec = FVector::ZeroVector;
	ClearForcedPedestrianFigureClip();
	ClearMoveTarget();
}

float ASimCopterGroundAgent::GetCapsuleHalfHeightCm() const
{
	return CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
}

void ASimCopterGroundAgent::SetCarriedBy(USceneComponent* CarryParentComponent, const FVector& RelativeLocation, const FRotator& RelativeRotation)
{
	if (CarryParentComponent == nullptr)
	{
		return;
	}

	bMissionCarried = true;
	bMissionStationary = true;
	bBehaviorActive = false;
	BehaviorStepVelocityCmPerSec = FVector::ZeroVector;
	BehaviorStepTimeRemainingSeconds = 0.0f;
	CurrentVelocityCmPerSec = FVector::ZeroVector;
	ExternalVelocityCmPerSec = FVector::ZeroVector;
	ClearMoveTarget();
	bSnapToGround = false;
	SetActorEnableCollision(false);
	if (CollisionComponent != nullptr)
	{
		CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	AttachToComponent(CarryParentComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	SetActorRelativeLocation(RelativeLocation);
	SetActorRelativeRotation(RelativeRotation);
	SetForcedPedestrianFigureClip(TEXT("Dead"));
	// Riding something takes a person out of every object-class search, so nobody tries to chase
	// or rescue someone who is already in the winch.
	BehaviorContext.Attributes[EBhavAttr::Visible] = 0;
}

void ASimCopterGroundAgent::ApplyAgentShape()
{
	if (CollisionComponent == nullptr || ProxyMeshComponent == nullptr)
	{
		return;
	}

	if (AgentKind == ESimCopterGroundAgentKind::Vehicle)
	{
		CollisionComponent->SetCapsuleSize(VehicleCapsuleRadiusCm * PopulationWorldScale, VehicleCapsuleHalfHeightCm * PopulationWorldScale);
		ProxyMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, (-VehicleCapsuleHalfHeightCm + VehicleFallbackZCm) * PopulationWorldScale));
		ProxyMeshComponent->SetRelativeScale3D(FVector(2.8f, 1.25f, 0.55f) * PopulationWorldScale);
		MovementSpeedCmPerSec = FMath::Max(MovementSpeedCmPerSec, 520.0f);
	}
	else
	{
		CollisionComponent->SetCapsuleSize(PedestrianCapsuleRadiusCm * PopulationWorldScale, PedestrianCapsuleHalfHeightCm * PopulationWorldScale);
		ProxyMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, (-PedestrianCapsuleHalfHeightCm + PedestrianFallbackZCm) * PopulationWorldScale));
		ProxyMeshComponent->SetRelativeScale3D(FVector(0.28f, 0.18f, 1.76f) * PopulationWorldScale);
		MovementSpeedCmPerSec = FMath::Clamp(MovementSpeedCmPerSec, 130.0f, 520.0f);
	}
}

void ASimCopterGroundAgent::UpdateMovement(float DeltaSeconds)
{
	if (RootComponent == nullptr)
	{
		return;
	}
	if (bMissionStationary)
	{
		CurrentVelocityCmPerSec = FVector::ZeroVector;
		ExternalVelocityCmPerSec = FVector::ZeroVector;
		return;
	}

	// Behavior-VM pedestrians move with the original per-tick model: a constant velocity
	// renewed by each MoveStep, with no target seeking or arrival deceleration (the cause of
	// the old stop-start pulse). Yaw is set from the stored facing attribute, not steering.
	// Avoidance move targets (car dodges) still take over via the branch below.
	const bool bUsingGuidanceTargetAtStart = IsGuidanceMoveTargetActive();
	if (bBehaviorActive && AgentKind == ESimCopterGroundAgentKind::Pedestrian && !IsAvoidanceMoveActive() && !bUsingGuidanceTargetAtStart)
	{
		if (BehaviorStepTimeRemainingSeconds > 0.0f)
		{
			BehaviorStepTimeRemainingSeconds -= DeltaSeconds;
			CurrentVelocityCmPerSec = BehaviorStepVelocityCmPerSec;
		}
		else
		{
			CurrentVelocityCmPerSec = FVector::ZeroVector;
		}
		const FVector Delta = (CurrentVelocityCmPerSec + ExternalVelocityCmPerSec) * DeltaSeconds;
		if (!Delta.IsNearlyZero())
		{
			RootComponent->MoveComponent(Delta, GetActorQuat(), false);
		}
		ExternalVelocityCmPerSec = FMath::VInterpTo(ExternalVelocityCmPerSec, FVector::ZeroVector, DeltaSeconds, 8.0f);
		return;
	}

	const bool bUsingAvoidanceTarget = IsAvoidanceMoveActive();
	if (!bHasMoveTarget && !bUsingAvoidanceTarget && !bUsingGuidanceTargetAtStart)
	{
		CurrentVelocityCmPerSec = FMath::VInterpTo(CurrentVelocityCmPerSec, FVector::ZeroVector, DeltaSeconds, 6.0f);
		ExternalVelocityCmPerSec = FMath::VInterpTo(ExternalVelocityCmPerSec, FVector::ZeroVector, DeltaSeconds, 8.0f);
		if (!ExternalVelocityCmPerSec.IsNearlyZero())
		{
			RootComponent->MoveComponent(ExternalVelocityCmPerSec * DeltaSeconds, GetActorQuat(), false);
		}
		return;
	}

	const FVector CurrentLocation = GetActorLocation();
	const bool bUsingAvoidancePathOffset = IsAvoidancePathOffsetActive();
	bool bUsingGuidanceTarget = bUsingGuidanceTargetAtStart;
	FVector BaseMoveTarget = bUsingGuidanceTarget ? GuidanceMoveTargetLocation : MoveTargetLocation;
	FVector ActiveMoveTarget = bUsingAvoidanceTarget
		? AvoidanceMoveTargetLocation
		: BaseMoveTarget + (bUsingAvoidancePathOffset ? AvoidancePathOffset : FVector::ZeroVector);
	FVector ToTarget = ActiveMoveTarget - CurrentLocation;
	FVector FlatToTarget(ToTarget.X, ToTarget.Y, 0.0f);
	float DistanceToTarget = FlatToTarget.Size();
	if (bUsingGuidanceTarget && DistanceToTarget <= TargetStopDistanceCm)
	{
		GuidanceMoveTargetTimeRemainingSeconds = 0.0f;
		bUsingGuidanceTarget = false;
		if (!bHasMoveTarget && !bUsingAvoidanceTarget)
		{
			CurrentVelocityCmPerSec = FVector::ZeroVector;
			ExternalVelocityCmPerSec = FVector::ZeroVector;
			return;
		}
		BaseMoveTarget = MoveTargetLocation;
		ActiveMoveTarget = MoveTargetLocation + (bUsingAvoidancePathOffset ? AvoidancePathOffset : FVector::ZeroVector);
		ToTarget = ActiveMoveTarget - CurrentLocation;
		FlatToTarget = FVector(ToTarget.X, ToTarget.Y, 0.0f);
		DistanceToTarget = FlatToTarget.Size();
	}
	if (DistanceToTarget <= TargetStopDistanceCm)
	{
		if (bUsingAvoidanceTarget)
		{
			AvoidanceMoveTimeRemainingSeconds = 0.0f;
			AvoidanceSpeedMultiplier = 1.0f;
		}
		else
		{
			ClearMoveTarget();
		}
		return;
	}

	const FVector DesiredDirection = FlatToTarget / DistanceToTarget;
	const float EffectiveSpeedScale = TrafficSpeedScale * (bUsingAvoidanceTarget
		? AvoidanceSpeedMultiplier
		: (bUsingAvoidancePathOffset ? AvoidancePathOffsetSpeedMultiplier : 1.0f));
	// A guided walk moves at the speed its caller asked for, which for a boarding passenger is the
	// shipped program's own `movespeed`. Without this the generic pedestrian speed (230 cm/s) took
	// over the moment guidance engaged, so a passenger crossed the last stretch to the aircraft at
	// nearly twice the rate BHAV 291 walks at.
	const float BaseSpeedCmPerSec = (bUsingGuidanceTarget && GuidanceMoveSpeedCmPerSec > 0.0f)
		? GuidanceMoveSpeedCmPerSec
		: MovementSpeedCmPerSec;
	const FVector DesiredVelocity = DesiredDirection * BaseSpeedCmPerSec * EffectiveSpeedScale;
	CurrentVelocityCmPerSec = FMath::VInterpTo(CurrentVelocityCmPerSec, DesiredVelocity, DeltaSeconds, AgentKind == ESimCopterGroundAgentKind::Vehicle ? 3.0f : 9.0f);

	const FVector Delta = (CurrentVelocityCmPerSec + ExternalVelocityCmPerSec) * DeltaSeconds;
	const FRotator CurrentRotation = GetActorRotation();
	
	float DesiredPitch = 0.0f;
	if (AgentKind == ESimCopterGroundAgentKind::Vehicle)
	{
		float ZAhead = CurrentLocation.Z;
		float ZBehind = CurrentLocation.Z;
		const float ProbeDist = 60.0f;
		if (TryGetWalkSurfaceZAt(CurrentLocation + DesiredDirection * ProbeDist, ZAhead) &&
			TryGetWalkSurfaceZAt(CurrentLocation - DesiredDirection * ProbeDist, ZBehind))
		{
			DesiredPitch = FMath::RadiansToDegrees(FMath::Atan2(ZAhead - ZBehind, ProbeDist * 2.0f));
		}
	}

	const FRotator DesiredRotation(0.0f, DesiredDirection.Rotation().Yaw, 0.0f);
	FRotator NewRotation = FMath::RInterpConstantTo(CurrentRotation, DesiredRotation, DeltaSeconds, TurnRateDegPerSec);
	
	if (AgentKind == ESimCopterGroundAgentKind::Vehicle)
	{
		const FRotator CurrentPitchOnly(CurrentRotation.Pitch, 0.0f, 0.0f);
		const FRotator DesiredPitchOnly(DesiredPitch, 0.0f, 0.0f);
		NewRotation.Pitch = FMath::RInterpTo(CurrentPitchOnly, DesiredPitchOnly, DeltaSeconds, 24.0f).Pitch;
	}

	// Move kinematically (no collision sweep). The traffic system handles road-following,
	// separation, and bump responses between agents; sweeping here would make capsules catch on
	// building corners and stall instead of continuing along the road graph.
	RootComponent->MoveComponent(Delta, NewRotation.Quaternion(), false);
	ExternalVelocityCmPerSec = FMath::VInterpTo(ExternalVelocityCmPerSec, FVector::ZeroVector, DeltaSeconds, 8.0f);
}

bool ASimCopterGroundAgent::TraceGround(FVector& OutGroundLocation) const
{
	if (GetWorld() == nullptr || CollisionComponent == nullptr)
	{
		return false;
	}

	const float HalfHeight = CollisionComponent->GetScaledCapsuleHalfHeight();
	const FVector CurrentLocation = GetActorLocation();

	if (AgentKind == ESimCopterGroundAgentKind::Vehicle)
	{
		float RoadSurfaceZ = 0.0f;
		if (TryGetWalkSurfaceZAt(CurrentLocation, RoadSurfaceZ))
		{
			OutGroundLocation = FVector(
				CurrentLocation.X,
				CurrentLocation.Y,
				RoadSurfaceZ + HalfHeight + 1.0f);
			return true;
		}
		return false;
	}

	// Trace well above to well below the agent so placement survives any gap between the traffic
	// spawner's terrain estimate and the city's actual rendered surface (the cause of the old
	// hovering). The Camera channel is blocked by the city terrain/mesh but ignored by every
	// pedestrian/vehicle/player capsule, so agents never snap onto one another.
	float StartZ = CurrentLocation.Z + GroundProbeUpCm;
	float TerrainFallbackZ = 0.0f;
	bool bHasTerrainClamp = false;

	// The original never derives a person's Z from the building model: people walk building
	// tiles at street level (Z = tile altitude + figure.twk feet adjust). A pedestrian's probe
	// therefore starts just above the tile's terrain altitude, below every roof, so it can pick
	// up sidewalk/road geometry but can never snap on top of a building. Vehicles keep the tall
	// probe (bridge decks sit far above the water tile's terrain altitude).
	if (AgentKind == ESimCopterGroundAgentKind::Pedestrian)
	{
		if (const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner()))
		{
			float TerrainWorldZ = 0.0f;
			if (TrafficSystem->TryGetTerrainWorldZAtWorldLocation(CurrentLocation, TerrainWorldZ))
			{
				// Building tiles are flat by construction, so the walk surface is the tile
				// altitude itself and the probe start must stay below the lowest roof. Open
				// tiles can slope, so grant the extra headroom only there.
				const int32 TileClass = TrafficSystem->GetPeopleTileClassAtWorldLocation(CurrentLocation);
				const bool bBuildingTile = TileClass >= 10 && TileClass <= 13;
				const float StreetLevelStartZ = TerrainWorldZ + PedestrianGroundProbeStartAboveTerrainCm +
					(bBuildingTile ? 0.0f : PedestrianGroundProbeSlopeHeadroomCm);
				// That clamp only describes someone who is walking the street. Anyone whose feet
				// are already above it is up there deliberately - the hospital paramedic and the
				// aerial cop standing on their helipad, a passenger dropped out of the cabin,
				// anyone still mid-fall - and probing from beneath their own feet is what pulled
				// them straight down through the roof into the building. Starting at the feet
				// leaves a street walker's probe exactly where it was (their feet rest on the
				// street, so this resolves to the same height) and lets an elevated person take
				// the roof under them as real ground.
				const float FeetZ = CurrentLocation.Z - HalfHeight;
				StartZ = FMath::Max(StreetLevelStartZ, FeetZ + PedestrianGroundProbeStartAboveTerrainCm);
				TerrainFallbackZ = TerrainWorldZ;
				bHasTerrainClamp = true;
			}
		}
	}

	const FVector Start(CurrentLocation.X, CurrentLocation.Y, StartZ);
	const float EndZ = FMath::Min(CurrentLocation.Z - HalfHeight, StartZ) - GroundProbeDistanceCm;
	const FVector End(CurrentLocation.X, CurrentLocation.Y, EndZ);
	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterGroundAgentSnap), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Camera, QueryParams) && Hit.bBlockingHit)
	{
		OutGroundLocation = FVector(CurrentLocation.X, CurrentLocation.Y, Hit.ImpactPoint.Z + HalfHeight + 1.0f);
		return true;
	}

	if (bHasTerrainClamp)
	{
		// Clamped probe started inside/under building geometry with no ground quad beneath:
		// stand at the tile's terrain altitude, exactly like the original placement.
		OutGroundLocation = FVector(CurrentLocation.X, CurrentLocation.Y, TerrainFallbackZ + HalfHeight + 1.0f);
		return true;
	}

	return false;
}

void ASimCopterGroundAgent::UpdateGroundSnap(float DeltaSeconds)
{
	FVector GroundedLocation;
	if (!TraceGround(GroundedLocation))
	{
		return;
	}

	// Vehicles keep exact instant placement (they always sit on the road/bridge surface).
	// Pedestrians are affected by gravity: they fall onto the surface below (when spawned in the
	// air or after walking off a ledge) and rest on it, instead of teleporting every tick.
	const FVector CurrentLocation = GetActorLocation();
	if (AgentKind != ESimCopterGroundAgentKind::Pedestrian || CurrentLocation.Z <= GroundedLocation.Z + 1.0f)
	{
		VerticalVelocityCmPerSec = 0.0f;
		SetActorLocation(GroundedLocation, false);
		if (bPassengerFallActive)
		{
			const float FallDistance = FMath::Max(0.0f, PassengerFallStartZ - GroundedLocation.Z);
			FinishPassengerFall(FallDistance);
		}
		return;
	}

	if (bPassengerFallActive && !bPassengerFallStarted)
	{
		bPassengerFallStarted = true;
		PassengerFallStartZ = CurrentLocation.Z;
	}
	VerticalVelocityCmPerSec -= GravityCmPerSec2 * DeltaSeconds;
	float NewZ = CurrentLocation.Z + VerticalVelocityCmPerSec * DeltaSeconds;
	if (NewZ <= GroundedLocation.Z)
	{
		NewZ = GroundedLocation.Z;
		VerticalVelocityCmPerSec = 0.0f;
	}
	SetActorLocation(FVector(CurrentLocation.X, CurrentLocation.Y, NewZ), false);
	if (bPassengerFallActive && NewZ <= GroundedLocation.Z + 1.0f)
	{
		const float FallDistance = FMath::Max(0.0f, PassengerFallStartZ - GroundedLocation.Z);
		FinishPassengerFall(FallDistance);
	}
}

void ASimCopterGroundAgent::SetVisualRootRelativeLocation(const FVector& Local)
{
	VisualRootBaseRelativeLocation = Local;
	if (VisualRoot != nullptr)
	{
		VisualRoot->SetRelativeLocation(Local + FVector(0.0f, 0.0f, WaterVisualOffsetCm));
	}
}

bool ASimCopterGroundAgent::IsStandingInWater() const
{
	if (AgentKind != ESimCopterGroundAgentKind::Pedestrian)
	{
		return false;
	}

	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	const ASimCity2000CityActor* City = TrafficSystem != nullptr ? TrafficSystem->GetCityActor() : nullptr;
	if (City == nullptr)
	{
		return false;
	}

	const FVector Location = GetActorLocation();
	float SurfaceZ = 0.0f;
	uint8 TerrainClass = 0xff;
	if (!City->TryGetWaterGameplaySurface(Location, SurfaceZ, TerrainClass) ||
		!SimCopterWaterGameplay::IsWaterTerrainClass(TerrainClass))
	{
		return false;
	}

	// A bridge deck, a pier and a helicopter in the hover are all "over a water tile" too. Only feet
	// at the sea plane mean feet in the sea, so the tile test is qualified by the height the ground
	// snap actually left this person at.
	const float FeetZ = Location.Z - GetCapsuleHalfHeightCm();
	return FeetZ <= SurfaceZ + WaterStandingClearanceCm;
}

void ASimCopterGroundAgent::UpdateWaterSubmersion(float DeltaSeconds)
{
	const bool bInWater = IsStandingInWater();
	const float Step = WaterSubmergeLerpSeconds > KINDA_SMALL_NUMBER
		? DeltaSeconds / WaterSubmergeLerpSeconds
		: 1.0f;
	WaterSubmergeAlpha = FMath::Clamp(WaterSubmergeAlpha + (bInWater ? Step : -Step), 0.0f, 1.0f);

	float Offset = 0.0f;
	if (WaterSubmergeAlpha > 0.0f)
	{
		// Half a body, so the waterline lands at the waist, plus the swell the water material is
		// displacing this patch of sea by right now - the same CPU evaluation the boats ride.
		float WaveCm = 0.0f;
		if (const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner()))
		{
			if (const ASimCity2000CityActor* City = TrafficSystem->GetCityActor())
			{
				WaveCm = City->GetWaterWaveOffsetCm(GetActorLocation());
			}
		}
		Offset = WaterSubmergeAlpha * (WaveCm - GetCapsuleHalfHeightCm());
	}

	if (!FMath::IsNearlyEqual(Offset, WaterVisualOffsetCm))
	{
		WaterVisualOffsetCm = Offset;
		SetVisualRootRelativeLocation(VisualRootBaseRelativeLocation);
	}
}

void ASimCopterGroundAgent::UpdateJankyAnimation(float DeltaSeconds)
{
	if (!bEnableJankyAnimation || VisualRoot == nullptr)
	{
		return;
	}

	AnimationTimeSeconds += DeltaSeconds;
	const float SpeedAlpha = FMath::Clamp(CurrentVelocityCmPerSec.Size() / FMath::Max(1.0f, MovementSpeedCmPerSec), 0.0f, 1.0f);
	const float Wave = FMath::Sin(AnimationTimeSeconds * JankyAnimationRate + AnimationPhase);

	if (AgentKind == ESimCopterGroundAgentKind::Vehicle)
	{
		const float Roll = Wave * 1.0f * SpeedAlpha;
		const float Pitch = FMath::Sin(AnimationTimeSeconds * JankyAnimationRate * 0.7f + AnimationPhase) * 0.65f * SpeedAlpha;
		VisualRoot->SetRelativeRotation(FRotator(Pitch, 0.0f, Roll));
		SetVisualRootRelativeLocation(FVector::ZeroVector);
	}
	else if (bUsingPedestrianFigure)
	{
		UpdateFigureAnimation(DeltaSeconds, SpeedAlpha);
	}
	else
	{
		const float Lean = Wave * 7.0f * SpeedAlpha; // degrees of roll (scale-independent)
		const float Bob = FMath::Abs(Wave) * 7.0f * SpeedAlpha * PopulationWorldScale;
		VisualRoot->SetRelativeRotation(FRotator(0.0f, 0.0f, Lean));
		SetVisualRootRelativeLocation(FVector(0.0f, 0.0f, Bob));

		if (bUsingPedestrianSprite)
		{
			const int32 DesiredSpriteRow = SpeedAlpha > 0.12f
				? FMath::FloorToInt(AnimationTimeSeconds * JankyAnimationRate) % FSimCopterPopulationSprite::People1Rows
				: 0;
			if (DesiredSpriteRow != PedestrianSpriteRow)
			{
				PedestrianSpriteRow = DesiredSpriteRow;
				FSimCopterPopulationSprite::BuildPeople1FrameQuad(OriginalMeshComponent, PedestrianSpriteColumn, PedestrianSpriteRow, PedestrianSpriteHeightCm);
				if (SpriteMaterialInstance != nullptr)
				{
					OriginalMeshComponent->SetMaterial(0, SpriteMaterialInstance);
				}
			}
		}
	}
}

void ASimCopterGroundAgent::ShowOriginalMesh(bool bUseOriginalMesh)
{
	bUsingOriginalMesh = bUseOriginalMesh;
	if (!bUseOriginalMesh)
	{
		bUsingPedestrianSprite = false;
		bUsingPedestrianBody = false;
		bUsingPedestrianFigure = false;
	}
	if (OriginalMeshComponent != nullptr)
	{
		OriginalMeshComponent->SetVisibility(bUseOriginalMesh, true);
	}
	if (ProxyMeshComponent != nullptr)
	{
		ProxyMeshComponent->SetVisibility(!bUseOriginalMesh, true);
	}
}

void ASimCopterGroundAgent::FinishPassengerFall(float FallDistanceCm)
{
	const int32 SourceEventId = PassengerFallSourceEventId;
	const bool bInjuredByFall = FallDistanceCm >= PassengerFallInjuryDistanceCm;
	bPassengerFallActive = false;
	bPassengerFallStarted = false;
	PassengerFallSourceEventId = INDEX_NONE;

	ClearForcedPedestrianFigureClip();
	if (bInjuredByFall)
	{
		SetMissionInjuredPose();
		if (InitialPersonState != 6)
		{
			if (UWorld* World = GetWorld())
			{
				if (ASimCopterMissionSystemActor* MissionActor = Cast<ASimCopterMissionSystemActor>(
					UGameplayStatics::GetActorOfClass(World, ASimCopterMissionSystemActor::StaticClass())))
				{
					MissionActor->ConvertDroppedTransportPassengerToMedevac(this, SourceEventId);
				}
			}
		}
	}
	else if (InitialPersonState == 6)
	{
		SetMissionInjuredPose();
	}
	else
	{
		if (SourceEventId != INDEX_NONE)
		{
			MissionEventId = SourceEventId;
		}
		ClearMissionPose();
		ResumeNormalPedestrianBehavior();
	}
}

FString ASimCopterGroundAgent::ResolveOriginalGameRoot() const
{
	const FString ConfiguredPath = OriginalGameRoot.Path.TrimStartAndEnd();
	if (!ConfiguredPath.IsEmpty())
	{
		const FString FullPath = FPaths::IsRelative(ConfiguredPath)
			? FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), ConfiguredPath))
			: FPaths::ConvertRelativePathToFull(ConfiguredPath);
		if (FPaths::DirectoryExists(FullPath) && !FSimCopterPrivAnimReader::ResolvePrivAnimPath(FullPath).IsEmpty())
		{
			return FullPath;
		}
	}

	// The figures need privanim specifically, so a root that merely looks like an install is not
	// good enough here.
	return SimCopterOriginalGame::ResolveRootBy([](const FString& Root)
	{
		return !FSimCopterPrivAnimReader::ResolvePrivAnimPath(Root).IsEmpty();
	});
}

void ASimCopterGroundAgent::ConfigureMarchingBandUniform(int32 BandIndex)
{
	SetPedestrianFigureName(TEXT("TubaExpert"));
	SetPedestrianFigureClothesOffset((BandIndex % 5) * 8 + 4);
	MovementSpeedCmPerSec = 220.0f;
	BuildPedestrianFigure();
}
