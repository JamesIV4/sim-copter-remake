// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterGroundAgent.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Formats/MaxisMeshLibrary.h"
#include "Formats/MaxisProceduralMeshBuilder.h"
#include "Formats/SimCopterPeopleCityRules.h"
#include "Formats/SimCopterPeopleReader.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Game/SimCopterVehicleMaterialSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Ground/SimCopterCriminalCar.h"
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
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterGroundAgent, Log, All);

namespace
{
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
constexpr const TCHAR* SpriteMaterialPath = TEXT("/Game/Materials/M_SimCopterSpriteTexture.M_SimCopterSpriteTexture");

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
			Helicopter->RemoveMissionPassengersForMission(1, MissionEventId, GetMissionPassengerKind());
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
	BehaviorContext.ResetToState(InitialPersonState);
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
			const bool bUnresolvedMissionPerson =
				MissionEventId != INDEX_NONE &&
				!bMissionResolutionReported &&
				Missions != nullptr &&
				Missions->IsMissionEventActive(MissionEventId);
			const bool bHospitalParamedic = bPersistentHospitalRoofCrew;
			if (bUnresolvedMissionPerson || bHospitalParamedic)
			{
				// A decoded program may time out or reach Disappear, but that cannot be allowed to
				// erase an unresolved mission dependency. State-5 hospital staff are likewise a
				// persistent service point: the original population code can recycle them, while
				// doing so visibly after a handoff makes the worker appear to vanish.
				BehaviorContext.bRequestDespawn = false;
				ResetBehaviorProgramOverride();
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
		if (bMissionWavesWhenIdle && BehaviorContext.PendingAnimMnemonic == TEXT("NoMo"))
		{
			BehaviorContext.PendingAnimMnemonic = TEXT("Wave");
		}
		if (bUsingPedestrianFigure && BehaviorContext.PendingAnimMnemonic != FigureMnemonic)
		{
			RebuildFigureClip(BehaviorContext.PendingAnimMnemonic);
		}
		BehaviorContext.PendingAnimMnemonic.Reset();
	}

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

		// FUN_004c9470: ambient people (+0x168) may only enter tile classes from their
		// behavior-class row (DAT_0058ec00); result 3 otherwise. Non-ambient movement keeps
		// the pre-VM rows as a safety net (missions steer via goto-object opcodes instead).
		if (bAmbient)
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
		// surface there is the roof, far above the 5-unit climb allowance. BHAV 308's
		// "move through walls" flag (+0x190) bypasses it after repeated failures.
		if (!bMoveThroughWalls)
		{
			float SurfaceZ = 0.0f;
			if (TryGetWalkSurfaceZAt(TargetLocation, SurfaceZ))
			{
				const float Rise = SurfaceZ - CurrentFeetZ;
				if (Rise > MaxClimbCm)
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

bool ASimCopterGroundAgent::TryGetWalkSurfaceZAt(const FVector& WorldLocation, float& OutSurfaceZ) const
{
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
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
		// DAT_005040d0+0xa4 is the player's helicopter, and it exists whether or not they are
		// sitting in it - so this must find the airframe, not "whatever body the player is in".
		// BHAV 291 probes it at one tile to decide a transport passenger may climb aboard.
		Found = ResolvePlayerHelicopter();
		break;
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

void ASimCopterGroundAgent::UpdateDescendingHelicopterAvoidance()
{
	// NOT FOUND IN THE SHIPPED PROGRAMS - reconstructed. No BHAV a transport fare or an ambient
	// pedestrian runs contains a "get out from under the helicopter" branch: only the criminal
	// programs test the airframe's altitude (BHAV 1173 rec[10], opcode 14 case 1). But standing
	// under a descending helicopter until it lands on you is not what the game does, so the
	// trigger is remake-side while the pieces are the original's - opcode 14 case 1's altitude
	// test, and BHAV 904 "Rxn: Run away (dir already set)" pushed through the same reaction path
	// every tool interaction uses.
	if (!bBehaviorActive || AgentKind != ESimCopterGroundAgentKind::Pedestrian ||
		bMissionCarried || bMissionStationary || bBehaviorMoveSuspended || BehaviorCarrier.IsValid())
	{
		return;
	}
	if (BehaviorContext.ActiveReactionProgramId == SimCopterInteraction::RunAwayReactionProgram)
	{
		return; // already scrambling
	}

	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	const ASimCopterHelicopterPawn* Helicopter = ResolvePlayerHelicopter();
	if (TrafficSystem == nullptr || Helicopter == nullptr)
	{
		return;
	}

	// Only when it is coming down on top of me: same tile, and low enough that the next few
	// metres of descent land on my head. The altitude band is opcode 14 case 1's, doubled - the
	// point is to move before the skids arrive, not as they touch.
	int32 MyX = INDEX_NONE;
	int32 MyY = INDEX_NONE;
	int32 HeliX = INDEX_NONE;
	int32 HeliY = INDEX_NONE;
	const FVector HelicopterLocation = Helicopter->GetActorLocation();
	if (!TryGetCurrentTileCoordinate(MyX, MyY) ||
		!TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(HelicopterLocation, HeliX, HeliY) ||
		MyX != HeliX || MyY != HeliY)
	{
		return;
	}

	const float UnitCm = FMath::Max(1.0f, TrafficSystem->GetPeopleWorldCmPerOriginalUnit());
	const float ClearanceCm =
		(HelicopterLocation.Z - Helicopter->GetSimpleCollisionHalfHeight()) -
		(GetActorLocation().Z + GetCapsuleHalfHeightCm());
	if (ClearanceCm > UnitCm * 8.0f)
	{
		return;
	}

	// Face away from it, then run. BHAV 904 moves on the facing it is given, which is why the
	// original's name for it is "dir already set".
	Context_FaceAwayFromHelicopter(HelicopterLocation);
	PushBehaviorReaction(SimCopterInteraction::RunAwayReactionProgram);
}

void ASimCopterGroundAgent::Context_FaceAwayFromHelicopter(const FVector& HelicopterLocation)
{
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	if (TrafficSystem == nullptr)
	{
		return;
	}
	const FVector Away = GetActorLocation() + (GetActorLocation() - HelicopterLocation).GetSafeNormal2D() * 200.0f;
	BehaviorContext.Attributes[EBhavAttr::Facing] =
		uint16(TrafficSystem->GetPeopleStoredFacingFromWorldLocations(GetActorLocation(), Away) & 7);
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
		// lines (the remake has no people voice bank yet, so only the animation half runs).
		Context.PendingAnimMnemonic = Context.RandomBounded(2) == 0 ? TEXT("2Gab") : TEXT("HipH");
		return;
	}

	Context.PendingAnimMnemonic = TEXT("Whoa");
}

ESimCopterBehaviorStepResult ASimCopterGroundAgent::StepTowardSelectedObject(FSimCopterPersonContext& Context)
{
	// FUN_004ca940: turn to face the object, take one ordinary move step, and report arrival once
	// the two are on the same tile (the original's move result 10 plus a half-tile height gate).
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

	int32 MyX = INDEX_NONE;
	int32 MyY = INDEX_NONE;
	int32 TheirX = INDEX_NONE;
	int32 TheirY = INDEX_NONE;
	if (!TryGetCurrentTileCoordinate(MyX, MyY) ||
		!TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(Context.SelectedLocation, TheirX, TheirY))
	{
		return ESimCopterBehaviorStepResult::NoTarget;
	}

	// FUN_004ca940 arrives on the same tile within 5 original units of vertical separation. The
	// original measures between two ground-referenced positions; the remake keeps an actor origin
	// mid-body, so the comparison has to be feet-to-doorsill or a landed helicopter is permanently
	// "too high" to climb into - 5 units is only about 31 cm.
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
	if (MyX == TheirX && MyY == TheirY && FMath::Abs(TargetReferenceZ - MyFeetZ) < HeightGateCm)
	{
		return ESimCopterBehaviorStepResult::Arrived;
	}

	FaceSelectedObject(Context);
	MoveStep(Context);
	return ESimCopterBehaviorStepResult::Moving;
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

bool ASimCopterGroundAgent::BoardCarrier(
	AActor* NewCarrier,
	const bool bAsHarnessRider,
	const bool bAllowAirborneCabinTransfer)
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

		// Normal cabin boarding goes through the same landed/settled gate as the proven mission
		// transfer path. The one exception is the decoded op-58 harness-to-cabin transition:
		// winding the rope in is supposed to bring its rider aboard while airborne.
		if ((!bAllowAirborneCabinTransfer && !Helicopter->CanTransferMissionPassengers()) ||
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
					1, MissionEventId, GetMissionPassengerKind());
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
		if (Helicopter->AddMissionPassengersForMission(
				1, MissionEventId, GetMissionPassengerKind()) <= 0)
		{
			return false;
		}
		bClaimedPassengerSeat = true;
		SetActorHiddenInGame(true);
	}
	else
	{
		SetActorHiddenInGame(false);
	}

	BehaviorCarrier = NewCarrier;
	bRidingHarness = bAsHarnessRider;

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
		if (Cast<ASimCopterGroundAgent>(NewCarrier) != nullptr)
		{
			// Keep a patient visibly slung across the paramedic instead of occupying the same
			// transform and disappearing inside their body.
			SetActorRelativeLocation(FVector(40.0f, 0.0f, -6.0f));
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
	if (bClaimedPassengerSeat)
	{
		if (ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(Carrier))
		{
			Helicopter->RemoveMissionPassengersForMission(1, MissionEventId, GetMissionPassengerKind());
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
	SnapToGroundImmediate();
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
	Person->SetActorLocation(GetActorLocation(), false);
	return Person->BoardCarrier(this, /*bAsHarnessRider*/ false);
}

bool ASimCopterGroundAgent::DropSelectedPerson(FSimCopterPersonContext& Context)
{
	ASimCopterGroundAgent* Person = Cast<ASimCopterGroundAgent>(Context.SelectedObject.Get());
	if (Person == nullptr)
	{
		return false;
	}
	return Person->AlightFromCarrier();
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

void ASimCopterGroundAgent::ThrowProjectileAtSelection(FSimCopterPersonContext& Context, const bool bAtSelection)
{
	// FUN_004cbfd0 / FUN_004cc130 both bind "Thro" and hand a projectile to FUN_0048e0b0. The
	// remake has no rioter projectile object, so only the animation half is reproduced - which is
	// the visible half, and stops a rioter standing inert where the original throws something.
	if (bAtSelection)
	{
		FaceSelectedObject(Context);
	}
	Context.PendingAnimMnemonic = TEXT("Thro");
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

int32 ASimCopterGroundAgent::GetPlayerHelicopterSpeed() const
{
	const ASimCopterHelicopterPawn* Helicopter = ResolvePlayerHelicopter();
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	if (Helicopter == nullptr || TrafficSystem == nullptr)
	{
		return 0;
	}
	// BHAV 264's thresholds (250 / 125) are in the original's units, so convert out of centimetres.
	const float UnitCm = FMath::Max(1.0f, TrafficSystem->GetPeopleWorldCmPerOriginalUnit());
	return FMath::RoundToInt(Helicopter->GetVelocity().Size() / UnitCm);
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

	FVector Centroid = FVector::ZeroVector;
	if (!TrafficSystem->MeasureBehaviorCrowd(*this, RadiusTiles, OutCount, OutAverageAgitation, Centroid))
	{
		return false;
	}

	// The bearing the original reports is already the stored octant (it applies the same -2 turn as
	// opcode 18), which is why BHAV 852 can assign it straight to the facing attribute.
	int32 Octant = 0;
	if (TryGetBehaviorFacingOctantToward(Centroid, Octant))
	{
		OutFacingOctant = Octant;
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
			UpdateCarriedTransform();
			UpdateOriginalBehavior(DeltaSeconds);
			UpdateJankyAnimation(DeltaSeconds);
			return;
		}
	}
	UpdateDescendingHelicopterAvoidance();
	UpdateOriginalBehavior(DeltaSeconds);
	UpdateMovement(DeltaSeconds);
	if (bSnapToGround)
	{
		UpdateGroundSnap(DeltaSeconds);
	}
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

	const float HalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	OriginalMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -HalfHeight));
	bUsingPedestrianSprite = true;
	ShowOriginalMesh(true);
	return true;
}

bool ASimCopterGroundAgent::BuildPedestrianBody()
{
	// Prefer the decoded original privanim.df figures; the box body is the offline fallback.
	if (bUseOriginalFigures && BuildPedestrianFigure())
	{
		return true;
	}

	LastMeshLoadError.Reset();
	bUsingPedestrianSprite = false;
	bUsingPedestrianBody = false;
	bUsingPedestrianFigure = false;
	PedestrianSpriteTexture = nullptr;
	SpriteMaterialInstance = nullptr;
	OriginalMeshComponent->ClearAllMeshSections();
	DisableVehicleHeadlights();

	PedestrianOutfitIndex = FSimCopterPopulationBody::ResolveOutfitIndex(this);
	FSimCopterPopulationBody::BuildPerson(OriginalMeshComponent, PedestrianOutfitIndex, PedestrianBodyHeightCm * PopulationWorldScale);

	if (VertexColorMaterial != nullptr)
	{
		OriginalMeshComponent->SetMaterial(0, VertexColorMaterial);
	}

	const float HalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	// Feet sit at the capsule bottom; the body is modelled from Z=0 (feet) upward.
	OriginalMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -HalfHeight));
	bUsingPedestrianBody = true;
	ShowOriginalMesh(true);
	return true;
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

	// Head sprite: pick from the original's SIM3D.BMP head-image table.
	const TArray<int32>& HeadTable = FSimCopterPopulationFigure::GetHeadImageTable();
	FigureHeadIndex = int32((Hash / 3u) % uint32(HeadTable.Num()));
	if (SpriteMaterial == nullptr)
	{
		SpriteMaterial = LoadSpriteMaterialNoWarn();
	}
	if (SpriteMaterial != nullptr)
	{
		if (const FMaxisTextureImage* HeadImage = FigureShared->HeadImages.Find(HeadTable[FigureHeadIndex]))
		{
			FigureHeadTexture = FSimCopterPopulationSprite::CreateTextureFromImage(this, *HeadImage, TEXT("SimCopterFigureHead"));
		}
		if (FigureHeadTexture != nullptr)
		{
			FigureHeadMaterialInstance = UMaterialInstanceDynamic::Create(SpriteMaterial, this);
			if (FigureHeadMaterialInstance != nullptr)
			{
				FigureHeadMaterialInstance->SetTextureParameterValue(TEXT("Texture"), FigureHeadTexture);
			}
		}
	}

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
	VisualRoot->SetRelativeLocation(FVector::ZeroVector);

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
	// roof) have no behavior VM to bind clips for them, so the wave is chosen here instead.
	const bool bWaving = !bWalking && bMissionWavesWhenIdle;
	{
		const FString Desired = bWalking ? TEXT("1Wal") : (bWaving ? TEXT("Wave") : TEXT("NoMo"));
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
		Light->SetAttenuationRadius(HeadlightAttenuationRadiusCm);
		Light->SetInnerConeAngle(11.0f);
		Light->SetOuterConeAngle(26.0f);
		Light->SetLightColor(FLinearColor(HeadlightColor));
		Light->SetVisibility(bEnableVehicleHeadlights);
	}
}

void ASimCopterGroundAgent::DisableVehicleHeadlights()
{
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
	bHasMoveTarget = true;
}

void ASimCopterGroundAgent::ClearMoveTarget()
{
	bHasMoveTarget = false;
	CurrentVelocityCmPerSec = FVector::ZeroVector;
	AvoidancePathOffsetTimeRemainingSeconds = 0.0f;
	AvoidancePathOffsetSpeedMultiplier = 1.0f;
	GuidanceMoveTargetTimeRemainingSeconds = 0.0f;
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

void ASimCopterGroundAgent::SetGuidanceMoveTarget(const FVector& NewTargetLocation, float DurationSeconds)
{
	GuidanceMoveTargetLocation = NewTargetLocation;
	GuidanceMoveTargetTimeRemainingSeconds = FMath::Max(0.0f, DurationSeconds);
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
		VisualRoot->SetRelativeLocation(FVector::ZeroVector);
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
		VisualRoot->SetRelativeLocation(FVector::ZeroVector);
	}
	SetActorLocation(WorldLocation, false);
	bSnapToGround = true;
	// Leave them lying injured on the ground, ready to be picked up again.
	SetMissionInjuredPose();
	SnapToGroundImmediate();
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
		VisualRoot->SetRelativeLocation(FVector::ZeroVector);
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
	const FVector DesiredVelocity = DesiredDirection * MovementSpeedCmPerSec * EffectiveSpeedScale;
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
		VisualRoot->SetRelativeLocation(FVector::ZeroVector);
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
		VisualRoot->SetRelativeLocation(FVector(0.0f, 0.0f, Bob));

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
	if (ConfiguredPath.IsEmpty())
	{
		return FString();
	}

	if (FPaths::IsRelative(ConfiguredPath))
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), ConfiguredPath));
	}

	return FPaths::ConvertRelativePathToFull(ConfiguredPath);
}
