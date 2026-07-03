// Copyright Epic Games, Inc. All Rights Reserved.

#include "Missions/SimCopterMissionSystemActor.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Ground/SimCopterGroundAgent.h"
#include "Ground/SimCopterOnFootPawn.h"
#include "Ground/SimCopterTrafficSystemActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
FString FormatSignedAmount(int32 Value, const TCHAR* Unit)
{
	return FString::Printf(TEXT("%+d %s"), Value, Unit);
}

const TCHAR* GetMissionDeltaLabel(int32 TextId)
{
	switch (TextId)
	{
	case 0x3a2: return TEXT("Flame started");
	case 0x3a3: return TEXT("Flame doused");
	case 0x3a4: return TEXT("Building burned");
	case 0x3a5: return TEXT("Building saved");
	case 0x3a6: return TEXT("Debris doused");
	case 0x3a8: return TEXT("Rescue delivered");
	case 0x3a9: return TEXT("Passenger delivered");
	case 0x3aa: return TEXT("Patient delivered");
	case 0x3ab: return TEXT("Victim picked up");
	case 0x3ac: return TEXT("Rioter dispersed");
	case 0x3b1: return TEXT("Person died");
	case 0x3b6: return TEXT("Car doused");
	case 0x3b7: return TEXT("Car cleared");
	case 0x3b8: return TEXT("Car burned");
	default: return TEXT("Mission update");
	}
}

FString ResolveCareerTweakPath()
{
	TArray<FString, TInlineAllocator<3>> Candidates;
	Candidates.Add(FPaths::ProjectContentDir() / TEXT("OriginalGame/tweak/career.twk"));
	Candidates.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("Reference/SimCopterOriginalGame/tweak/career.twk")));
	Candidates.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame/tweak/career.twk")));

	for (FString Candidate : Candidates)
	{
		Candidate = FPaths::ConvertRelativePathToFull(Candidate);
		FPaths::NormalizeFilename(Candidate);
		if (FPaths::FileExists(Candidate))
		{
			return Candidate;
		}
	}

	return Candidates.Last();
}

bool IsValidMissionTile(int32 TileX, int32 TileY)
{
	return TileX >= 0 && TileX < 128 && TileY >= 0 && TileY < 128;
}

FLinearColor WithAlpha(FLinearColor Color, float Alpha)
{
	Color.A = Alpha;
	return Color;
}
}

ASimCopterMissionSystemActor::ASimCopterMissionSystemActor()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ASimCopterMissionSystemActor::BeginPlay()
{
	Super::BeginPlay();
	
	// Assuming 0 for random seed for parity tests if we want, but normally a real seed.
	MissionSystem.Initialize(this, 12345);
	
	MissionSystem.LoadCareerData(ResolveCareerTweakPath());

	EnsureMessageLogWidget();
	EnsureMissionMarkerWidget();
}

void ASimCopterMissionSystemActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveMissionMarkerWidget();
	RemoveMessageLogWidget();
	Super::EndPlay(EndPlayReason);
}

void ASimCopterMissionSystemActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	MissionSystem.Tick(DeltaTime);
	ProcessPassengerTransfers();
	MissionSystem.AdvanceCareerIfComplete();

	bool bChangedLog = false;
	for (int32 Index = MissionMessageLog.Num() - 1; Index >= 0; --Index)
	{
		MissionMessageLog[Index].RemainingSeconds -= DeltaTime;
		if (MissionMessageLog[Index].RemainingSeconds <= 0.0f)
		{
			MissionMessageLog.RemoveAt(Index);
			bChangedLog = true;
		}
	}

	if (bChangedLog)
	{
		RefreshMessageLogWidget();
	}

	RefreshMissionMarkerWidget();
}

int32 ASimCopterMissionSystemActor::GetXbldTileId(int32 TileX, int32 TileY) const
{
	if (const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		return TrafficSystem->GetXbldTileId(TileX, TileY);
	}
	return 0;
}

int32 ASimCopterMissionSystemActor::GetBuildingFootprintSize(int32 TileX, int32 TileY) const
{
	if (const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		return TrafficSystem->GetBuildingFootprintSize(TileX, TileY);
	}
	return 1;
}

bool ASimCopterMissionSystemActor::GetCameraTile(int32& OutTileX, int32& OutTileY) const
{
	if (const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		if (const UWorld* World = GetWorld())
		{
			if (const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0))
			{
				if (PlayerController->PlayerCameraManager != nullptr &&
					TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(PlayerController->PlayerCameraManager->GetCameraLocation(), OutTileX, OutTileY))
				{
					return true;
				}
			}
		}
	}

	OutTileX = 64;
	OutTileY = 64;
	return false;
}

bool ASimCopterMissionSystemActor::GetPlayerTile(int32& OutTileX, int32& OutTileY) const
{
	if (const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		if (const UWorld* World = GetWorld())
		{
			if (const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0))
			{
				if (const APawn* Pawn = PlayerController->GetPawn())
				{
					if (TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(Pawn->GetActorLocation(), OutTileX, OutTileY))
					{
						return true;
					}
				}
			}
		}
	}

	OutTileX = 64;
	OutTileY = 64;
	return false;
}

bool ASimCopterMissionSystemActor::IsModalUiActive() const
{
	return false;
}

void ASimCopterMissionSystemActor::OnBuildingFireIgnited(int32 TileX, int32 TileY, int32 EventId)
{
}

bool ASimCopterMissionSystemActor::TryActivatePlaneCrash(int32 EventId)
{
	return false;
}

bool ASimCopterMissionSystemActor::TryActivateTrainCrash(int32 EventId)
{
	return false;
}

bool ASimCopterMissionSystemActor::TryActivateBoatRescue(int32 EventId, int32 Timer1616, int32& OutTileX, int32& OutTileY)
{
	return false;
}

bool ASimCopterMissionSystemActor::TryActivateTrainRescue(int32 EventId, int32 Timer1616, int32& OutTileX, int32& OutTileY)
{
	return false;
}

bool ASimCopterMissionSystemActor::TryStartTrafficJam(int32 EventId, int32& OutTileX, int32& OutTileY)
{
	if (ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		return TrafficSystem->TryStartTrafficJam(EventId, OutTileX, OutTileY);
	}
	return false;
}

void ASimCopterMissionSystemActor::EndTrafficJam(int32 EventId)
{
	if (ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		TrafficSystem->EndTrafficJam(EventId);
	}
}



bool ASimCopterMissionSystemActor::TryStartCarFire(int32 EventId, int32& OutTileX, int32& OutTileY)
{
	if (ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		return TrafficSystem->TryStartCarFire(EventId, OutTileX, OutTileY);
	}
	return false;
}

bool ASimCopterMissionSystemActor::TrySpawnMissionPerson(int32 Mode, int32 SubState, int32 TileX, int32 TileY, int32 EventId)
{
	if (ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		return TrafficSystem->TrySpawnMissionPerson(Mode, SubState, TileX, TileY, EventId);
	}
	return false;
}

void ASimCopterMissionSystemActor::PlayRadioVoice(int32 VoiceId, int32 Volume)
{
	if (USoundBase** Sound = RadioVoices.Find(VoiceId))
	{
		UGameplayStatics::PlaySound2D(this, *Sound, Volume / 255.0f);
	}
}

void ASimCopterMissionSystemActor::PlayUiSound(int32 SoundId)
{
	if (USoundBase** Sound = UiSounds.Find(SoundId))
	{
		UGameplayStatics::PlaySound2D(this, *Sound);
	}
}

bool ASimCopterMissionSystemActor::TryActivateSpeederCar(int32 EventId, int32 TileX, int32 TileY)
{
	return false;
}

void ASimCopterMissionSystemActor::OnUiMessage(const SimCopterMissions::FSimCopterMissionUiMessage& Message)
{
	FLinearColor Color = FLinearColor::White;
	const FString Text = FormatMissionUiMessage(Message, Color);
	if (!Text.IsEmpty())
	{
		PushMissionLogMessage(Text, Color);
	}
}

ASimCopterTrafficSystemActor* ASimCopterMissionSystemActor::ResolveTrafficSystem() const
{
	if (SourceTrafficSystem != nullptr && IsValid(SourceTrafficSystem))
	{
		return SourceTrafficSystem;
	}

	if (!bUseActiveTrafficSystem)
	{
		return nullptr;
	}

	if (UWorld* World = GetWorld())
	{
		return Cast<ASimCopterTrafficSystemActor>(UGameplayStatics::GetActorOfClass(World, ASimCopterTrafficSystemActor::StaticClass()));
	}

	return nullptr;
}

void ASimCopterMissionSystemActor::NotifyMedevacVictimBoarded(int32 EventId, int32 Count)
{
	if (EventId == INDEX_NONE || Count <= 0)
	{
		return;
	}

	MissionPassengersOnboard.FindOrAdd(EventId) += Count;
	MissionSystem.PostEvent(SimCopterMissions::EVT_VictimPickedUp, EventId, Count);
}

void ASimCopterMissionSystemActor::GetTransferReadyHelicopters(TArray<ASimCopterHelicopterPawn*>& OutHelicopters) const
{
	OutHelicopters.Reset();
	if (GetWorld() == nullptr)
	{
		return;
	}

	TArray<AActor*> HelicopterActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ASimCopterHelicopterPawn::StaticClass(), HelicopterActors);
	for (AActor* Actor : HelicopterActors)
	{
		ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(Actor);
		if (Helicopter != nullptr && Helicopter->CanTransferMissionPassengers())
		{
			OutHelicopters.Add(Helicopter);
		}
	}
}

void ASimCopterMissionSystemActor::ProcessPassengerTransfers()
{
	ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem == nullptr)
	{
		return;
	}

	TArray<ASimCopterHelicopterPawn*> Helicopters;
	GetTransferReadyHelicopters(Helicopters);

	struct FPassengerMissionSnapshot
	{
		int32 EventId = INDEX_NONE;
		int32 PickupX = INDEX_NONE;
		int32 PickupY = INDEX_NONE;
		int32 DropoffX = INDEX_NONE;
		int32 DropoffY = INDEX_NONE;
		int32 WaitingPassengers = 0;
		int32 DeliverablePassengers = 0;
		bool bTransport = false;
		bool bMedevac = false;
	};

	TArray<FPassengerMissionSnapshot, TInlineAllocator<8>> PassengerMissions;
	TSet<int32> ActivePassengerEventIds;
	for (const SimCopterMissions::FSimCopterMissionRecord& Record : MissionSystem.GetRecords())
	{
		if (!Record.bActive)
		{
			continue;
		}

		const bool bTransport = (Record.TypeMask & SimCopterMissions::TYPE_Transport) != 0;
		const bool bMedevac = (Record.TypeMask & SimCopterMissions::TYPE_Medevac) != 0;
		if (!bTransport && !bMedevac)
		{
			continue;
		}

		ActivePassengerEventIds.Add(Record.EventId);

		FPassengerMissionSnapshot Snapshot;
		Snapshot.EventId = Record.EventId;
		Snapshot.PickupX = Record.TileX;
		Snapshot.PickupY = Record.TileY;
		Snapshot.DropoffX = Record.SecondaryX;
		Snapshot.DropoffY = Record.SecondaryY;
		Snapshot.bTransport = bTransport;
		Snapshot.bMedevac = bMedevac;
		if (bTransport)
		{
			Snapshot.WaitingPassengers = FMath::Max(0, Record.TransportPassengers - Record.VictimsPickedUp - Record.PassengersLost);
			Snapshot.DeliverablePassengers = FMath::Max(0, Record.TransportPassengers - Record.TransportDelivered - Record.PassengersLost);
		}
		else
		{
			Snapshot.WaitingPassengers = FMath::Max(0, Record.MedevacVictims - Record.VictimsPickedUp - Record.Casualties);
			Snapshot.DeliverablePassengers = FMath::Max(0, Record.MedevacVictims - Record.MedevacDelivered - Record.Casualties);
		}
		PassengerMissions.Add(Snapshot);
	}

	for (auto It = MissionPassengersOnboard.CreateIterator(); It; ++It)
	{
		if (!ActivePassengerEventIds.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}

	auto IsWorldLocationNearTile = [this, TrafficSystem](const FVector& WorldLocation, int32 TileX, int32 TileY, float RadiusCm) -> bool
	{
		if (!IsValidMissionTile(TileX, TileY))
		{
			return false;
		}

		FVector TileLocation = FVector::ZeroVector;
		if (!TrafficSystem->TryGetTileCenterWorldLocation(TileX, TileY, TileLocation))
		{
			return false;
		}

		return FVector::DistSquared2D(WorldLocation, TileLocation) <= FMath::Square(RadiusCm) &&
			FMath::Abs(WorldLocation.Z - TileLocation.Z) <= PassengerTransferMaxVerticalDeltaCm;
	};

	if (ASimCopterOnFootPawn* OnFootPawn = Cast<ASimCopterOnFootPawn>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0)))
	{
		if (!OnFootPawn->IsCarryingMissionPerson())
		{
			for (const FPassengerMissionSnapshot& Mission : PassengerMissions)
			{
				if (!Mission.bMedevac || Mission.WaitingPassengers <= 0)
				{
					continue;
				}

				if (ASimCopterGroundAgent* Patient = TrafficSystem->FindMissionPersonNear(
					Mission.EventId,
					OnFootPawn->GetActorLocation(),
					MedevacOnFootPickupRadiusCm,
					PassengerTransferMaxVerticalDeltaCm))
				{
					OnFootPawn->PickUpMissionPerson(Patient);
					break;
				}
			}
		}
	}

	for (const FPassengerMissionSnapshot& Mission : PassengerMissions)
	{
		int32& Onboard = MissionPassengersOnboard.FindOrAdd(Mission.EventId);
		FVector DropoffLocation = FVector::ZeroVector;
		const bool bHasDropoffLocation =
			IsValidMissionTile(Mission.DropoffX, Mission.DropoffY) &&
			TrafficSystem->TryGetTileCenterWorldLocation(Mission.DropoffX, Mission.DropoffY, DropoffLocation);

		if (bHasDropoffLocation && Mission.DeliverablePassengers > 0)
		{
			const int32 ReleasedOnGround = TrafficSystem->ReleaseMissionPeopleNear(
				Mission.EventId,
				DropoffLocation,
				Mission.DeliverablePassengers,
				PassengerDropoffRadiusCm,
				PassengerTransferMaxVerticalDeltaCm);
			if (ReleasedOnGround > 0)
			{
				if (Mission.bMedevac)
				{
					Onboard = FMath::Max(0, Onboard - ReleasedOnGround);
					MissionSystem.PostEvent(SimCopterMissions::EVT_MedevacDelivered, Mission.EventId, ReleasedOnGround);
				}
				else
				{
					Onboard = FMath::Max(0, Onboard - ReleasedOnGround);
					MissionSystem.PostEvent(SimCopterMissions::EVT_TransportDelivered, Mission.EventId, ReleasedOnGround);
				}
			}
		}

		if (Mission.bTransport && bHasDropoffLocation)
		{
			for (ASimCopterHelicopterPawn* Helicopter : Helicopters)
			{
				if (Helicopter == nullptr || !IsWorldLocationNearTile(Helicopter->GetActorLocation(), Mission.DropoffX, Mission.DropoffY, PassengerDropoffRadiusCm))
				{
					continue;
				}

				const int32 OnHelicopter = Helicopter->GetMissionPassengerCount(Mission.EventId, ESimCopterMissionPassengerKind::Transport);
				const int32 Delivered = Helicopter->RemoveMissionPassengersForMission(OnHelicopter, Mission.EventId, ESimCopterMissionPassengerKind::Transport);
				if (Delivered <= 0)
				{
					continue;
				}

				Onboard = FMath::Max(0, Onboard - Delivered);
				TrafficSystem->SpawnMissionPeopleAtWorldLocation(
					Delivered,
					Helicopter->GetPassengerDropWorldLocation(),
					INDEX_NONE,
					0,
					-1,
					185.0f);
				MissionSystem.PostEvent(SimCopterMissions::EVT_TransportDelivered, Mission.EventId, Delivered);
				break;
			}
		}

		if (!Mission.bTransport || Mission.WaitingPassengers <= 0)
		{
			continue;
		}

		for (ASimCopterHelicopterPawn* Helicopter : Helicopters)
		{
			if (Helicopter == nullptr)
			{
				continue;
			}

			if (!IsWorldLocationNearTile(Helicopter->GetActorLocation(), Mission.PickupX, Mission.PickupY, PassengerPickupRadiusCm))
			{
				continue;
			}

			const int32 SeatsAvailable = FMath::Min(Mission.WaitingPassengers, Helicopter->GetAvailablePassengerSeats());
			if (SeatsAvailable <= 0)
			{
				continue;
			}

			TrafficSystem->GuideMissionPeopleToLocation(
				Mission.EventId,
				Helicopter->GetActorLocation(),
				Helicopter->GetActorLocation(),
				SeatsAvailable,
				PassengerPickupRadiusCm,
				PassengerTransferMaxVerticalDeltaCm,
				PassengerBoardGuidanceSeconds);

			const int32 PickedUp = TrafficSystem->BoardMissionPeopleTouching(
				Mission.EventId,
				Helicopter->GetActorLocation(),
				SeatsAvailable,
				PassengerBoardTouchRadiusCm,
				PassengerTransferMaxVerticalDeltaCm);
			if (PickedUp <= 0)
			{
				continue;
			}

			const int32 Boarded = Helicopter->AddMissionPassengersForMission(PickedUp, Mission.EventId, ESimCopterMissionPassengerKind::Transport);
			if (Boarded <= 0)
			{
				continue;
			}

			Onboard += Boarded;
			MissionSystem.PostEvent(SimCopterMissions::EVT_VictimPickedUp, Mission.EventId, Boarded);
			break;
		}
	}
}

void ASimCopterMissionSystemActor::EnsureMessageLogWidget()
{
	if (!bShowMissionMessageLog || MessageLogWidget.IsValid() || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	TSharedRef<SVerticalBox> LogBox = SNew(SVerticalBox);
	MessageLogBox = LogBox;
	MessageLogWidget =
		SNew(SBox)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(FMargin(MessageLogScreenPadding.X, MessageLogScreenPadding.Y, 0.0f, 0.0f))
		.Visibility(EVisibility::Collapsed)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.58f))
			.Padding(FMargin(10.0f, 8.0f))
			[
				LogBox
			]
		];

	GEngine->GameViewport->AddViewportWidgetContent(MessageLogWidget.ToSharedRef(), 20);
}

void ASimCopterMissionSystemActor::RemoveMessageLogWidget()
{
	if (GEngine != nullptr && GEngine->GameViewport != nullptr && MessageLogWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MessageLogWidget.ToSharedRef());
	}

	MessageLogBox.Reset();
	MessageLogWidget.Reset();
}

void ASimCopterMissionSystemActor::RefreshMessageLogWidget()
{
	EnsureMessageLogWidget();
	if (!MessageLogBox.IsValid() || !MessageLogWidget.IsValid())
	{
		return;
	}

	MessageLogBox->ClearChildren();
	for (const FSimCopterMissionLogEntry& Entry : MissionMessageLog)
	{
		MessageLogBox->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 1.5f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(Entry.Text))
			.ColorAndOpacity(Entry.Color)
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14))
		];
	}

	MessageLogWidget->SetVisibility(MissionMessageLog.Num() > 0 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
}

void ASimCopterMissionSystemActor::EnsureMissionMarkerWidget()
{
	if (!bShowMissionWorldMarkers || MissionMarkerWidget.IsValid() || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	TSharedRef<SConstraintCanvas> MarkerCanvas = SNew(SConstraintCanvas);
	MissionMarkerCanvas = MarkerCanvas;
	MissionMarkerWidget =
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			MarkerCanvas
		];

	GEngine->GameViewport->AddViewportWidgetContent(MissionMarkerWidget.ToSharedRef(), 15);
}

void ASimCopterMissionSystemActor::RemoveMissionMarkerWidget()
{
	if (GEngine != nullptr && GEngine->GameViewport != nullptr && MissionMarkerWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MissionMarkerWidget.ToSharedRef());
	}

	MissionMarkerCanvas.Reset();
	MissionMarkerWidget.Reset();
}

void ASimCopterMissionSystemActor::RefreshMissionMarkerWidget()
{
	EnsureMissionMarkerWidget();
	if (!bShowMissionWorldMarkers || !MissionMarkerCanvas.IsValid() || !MissionMarkerWidget.IsValid())
	{
		return;
	}

	MissionMarkerCanvas->ClearChildren();

	TArray<FSimCopterMissionWorldMarkerEntry> Markers;
	BuildMissionWorldMarkers(Markers);

	const FVector2D ClampedMarkerSize(
		FMath::Clamp(MissionMarkerSize.X, 36.0f, 220.0f),
		FMath::Clamp(MissionMarkerSize.Y, 22.0f, 80.0f));

	for (const FSimCopterMissionWorldMarkerEntry& Marker : Markers)
	{
		FVector2D ScreenPosition;
		bool bClamped = false;
		if (!ProjectMissionMarkerToScreen(Marker.WorldLocation, ScreenPosition, bClamped))
		{
			continue;
		}

		const FVector2D DrawPosition(
			ScreenPosition.X - ClampedMarkerSize.X * 0.5f,
			ScreenPosition.Y - ClampedMarkerSize.Y * 0.5f);
		const FLinearColor BackgroundColor = WithAlpha(Marker.Color, bClamped ? 0.72f : 0.86f);

		MissionMarkerCanvas->AddSlot()
		.Offset(FMargin(DrawPosition.X, DrawPosition.Y, ClampedMarkerSize.X, ClampedMarkerSize.Y))
		.Alignment(FVector2D::ZeroVector)
		[
			SNew(SBox)
			.WidthOverride(ClampedMarkerSize.X)
			.HeightOverride(ClampedMarkerSize.Y)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
				.BorderBackgroundColor(BackgroundColor)
				.Padding(FMargin(7.0f, 3.0f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(Marker.Label))
					.Justification(ETextJustify::Center)
					.ColorAndOpacity(FLinearColor::White)
					.ShadowOffset(FVector2D(1.0f, 1.0f))
					.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13))
				]
			]
		];
	}

	MissionMarkerWidget->SetVisibility(Markers.Num() > 0 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
}

void ASimCopterMissionSystemActor::BuildMissionWorldMarkers(TArray<FSimCopterMissionWorldMarkerEntry>& OutMarkers) const
{
	OutMarkers.Reset();

	auto AddTileMarker = [this, &OutMarkers](int32 TileX, int32 TileY, const TCHAR* Label, const FString& Detail, const FLinearColor& Color)
	{
		FVector WorldLocation;
		if (!TryMakeMissionMarkerWorldLocation(TileX, TileY, WorldLocation))
		{
			return;
		}

		FSimCopterMissionWorldMarkerEntry Marker;
		Marker.WorldLocation = WorldLocation;
		Marker.Label = Label;
		Marker.Detail = Detail;
		Marker.Color = Color;
		OutMarkers.Add(Marker);
	};

	for (const SimCopterMissions::FSimCopterMissionRecord& Record : MissionSystem.GetRecords())
	{
		if (!Record.bActive || Record.TypeMask == 0)
		{
			continue;
		}

		const bool bHasDropoff = IsValidMissionTile(Record.SecondaryX, Record.SecondaryY);
		const bool bHasPassengerPickup = (Record.TypeMask & SimCopterMissions::TYPE_Transport) != 0;
		const bool bHasMedicalPickup = (Record.TypeMask & SimCopterMissions::TYPE_Medevac) != 0;
		const bool bHasRescuePickup = (Record.TypeMask & SimCopterMissions::TYPE_RescuePeople) != 0;
		const bool bPickedUpAnyPassenger = Record.VictimsPickedUp > 0 || Record.TransportDelivered > 0 || Record.RescueDelivered > 0 || Record.MedevacDelivered > 0;

		if (bHasPassengerPickup)
		{
			const int32 TransportOnboard = FMath::Max(0, Record.VictimsPickedUp - Record.TransportDelivered - Record.PassengersLost);
			const int32 TransportWaiting = FMath::Max(0, Record.TransportPassengers - Record.VictimsPickedUp - Record.PassengersLost);
			if (TransportOnboard > 0 && bHasDropoff)
			{
				AddTileMarker(Record.SecondaryX, Record.SecondaryY, TEXT("DROP"), Record.Name, FLinearColor(0.05f, 0.72f, 0.32f, 1.0f));
			}
			else if (TransportWaiting > 0)
			{
				AddTileMarker(Record.TileX, Record.TileY, TEXT("PICKUP"), Record.Name, FLinearColor(0.08f, 0.46f, 0.95f, 1.0f));
			}
			continue;
		}

		if (bHasMedicalPickup)
		{
			if (bPickedUpAnyPassenger && bHasDropoff)
			{
				AddTileMarker(Record.SecondaryX, Record.SecondaryY, TEXT("DROP"), Record.Name, FLinearColor(0.05f, 0.72f, 0.32f, 1.0f));
			}
			else
			{
				AddTileMarker(Record.TileX, Record.TileY, TEXT("PATIENT"), Record.Name, FLinearColor(0.8f, 0.12f, 0.55f, 1.0f));
			}
			continue;
		}

		if (bHasRescuePickup)
		{
			if (bPickedUpAnyPassenger && bHasDropoff)
			{
				AddTileMarker(Record.SecondaryX, Record.SecondaryY, TEXT("DROP"), Record.Name, FLinearColor(0.05f, 0.72f, 0.32f, 1.0f));
			}
			else
			{
				AddTileMarker(Record.TileX, Record.TileY, TEXT("RESCUE"), Record.Name, FLinearColor(0.95f, 0.55f, 0.08f, 1.0f));
			}
			continue;
		}

		if ((Record.TypeMask & (SimCopterMissions::TYPE_BuildingFire | SimCopterMissions::TYPE_CarFire)) != 0)
		{
			AddTileMarker(Record.TileX, Record.TileY, TEXT("FIRE"), Record.Name, FLinearColor(0.96f, 0.14f, 0.08f, 1.0f));
			continue;
		}

		if ((Record.TypeMask & SimCopterMissions::TYPE_TrafficJam) != 0)
		{
			AddTileMarker(Record.TileX, Record.TileY, TEXT("JAM"), Record.Name, FLinearColor(1.0f, 0.78f, 0.12f, 1.0f));
			continue;
		}

		if ((Record.TypeMask & SimCopterMissions::TYPE_Riot) != 0)
		{
			AddTileMarker(Record.TileX, Record.TileY, TEXT("RIOT"), Record.Name, FLinearColor(0.68f, 0.22f, 0.82f, 1.0f));
			continue;
		}

		if ((Record.TypeMask & (SimCopterMissions::TYPE_CriminalA | SimCopterMissions::TYPE_CriminalC |
			SimCopterMissions::TYPE_CriminalCar | SimCopterMissions::TYPE_SpeederEvent)) != 0)
		{
			AddTileMarker(Record.TileX, Record.TileY, TEXT("TARGET"), Record.Name, FLinearColor(0.86f, 0.18f, 0.18f, 1.0f));
			continue;
		}

		AddTileMarker(Record.TileX, Record.TileY, TEXT("MISSION"), Record.Name, FLinearColor(0.15f, 0.55f, 1.0f, 1.0f));
	}
}

bool ASimCopterMissionSystemActor::TryMakeMissionMarkerWorldLocation(int32 TileX, int32 TileY, FVector& OutWorldLocation) const
{
	if (!IsValidMissionTile(TileX, TileY))
	{
		return false;
	}

	if (const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		if (TrafficSystem->TryGetTileCenterWorldLocation(TileX, TileY, OutWorldLocation))
		{
			OutWorldLocation.Z += MissionMarkerWorldZOffsetCm;
			return true;
		}
	}

	return false;
}

bool ASimCopterMissionSystemActor::ProjectMissionMarkerToScreen(const FVector& WorldLocation, FVector2D& OutScreenPosition, bool& bOutClamped) const
{
	bOutClamped = false;
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	if (PlayerController == nullptr)
	{
		return false;
	}

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	PlayerController->GetViewportSize(ViewportX, ViewportY);
	if (ViewportX <= 0 || ViewportY <= 0)
	{
		return false;
	}

	FVector CameraLocation;
	FRotator CameraRotation;
	PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
	const FVector CameraLocal = CameraRotation.UnrotateVector(WorldLocation - CameraLocation);

	FVector2D ScreenPosition = FVector2D::ZeroVector;
	const bool bInFront = CameraLocal.X > 1.0f;
	bool bProjected = false;
	if (bInFront)
	{
		bProjected = PlayerController->ProjectWorldLocationToScreen(WorldLocation, ScreenPosition, true);
	}

	if (!bProjected)
	{
		const FVector2D ViewCenter(float(ViewportX) * 0.5f, float(ViewportY) * 0.5f);
		if (bInFront)
		{
			const float Aspect = FMath::Max(float(ViewportX) / FMath::Max(1.0f, float(ViewportY)), 0.01f);
			const float FovDeg = PlayerController->PlayerCameraManager != nullptr ? PlayerController->PlayerCameraManager->GetFOVAngle() : 90.0f;
			const float TanHalfHorizontalFov = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(FovDeg, 5.0f, 170.0f) * 0.5f));
			const float TanHalfVerticalFov = TanHalfHorizontalFov / Aspect;
			const float NormalizedX = CameraLocal.Y / (CameraLocal.X * TanHalfHorizontalFov);
			const float NormalizedY = CameraLocal.Z / (CameraLocal.X * TanHalfVerticalFov);
			ScreenPosition = FVector2D(
				ViewCenter.X + NormalizedX * ViewCenter.X,
				ViewCenter.Y - NormalizedY * ViewCenter.Y);
		}
		else
		{
			FVector2D Direction(CameraLocal.Y, -CameraLocal.Z);
			if (Direction.IsNearlyZero())
			{
				Direction = FVector2D(0.0f, 1.0f);
			}
			Direction.Normalize();
			ScreenPosition = ViewCenter + Direction * FMath::Max(float(ViewportX), float(ViewportY));
		}
	}

	const FVector2D ClampedMarkerSize(
		FMath::Clamp(MissionMarkerSize.X, 36.0f, 220.0f),
		FMath::Clamp(MissionMarkerSize.Y, 22.0f, 80.0f));
	const float MinX = MissionMarkerEdgePadding + ClampedMarkerSize.X * 0.5f;
	const float MaxX = float(ViewportX) - MissionMarkerEdgePadding - ClampedMarkerSize.X * 0.5f;
	const float MinY = MissionMarkerEdgePadding + ClampedMarkerSize.Y * 0.5f;
	const float MaxY = float(ViewportY) - MissionMarkerEdgePadding - ClampedMarkerSize.Y * 0.5f;
	const FVector2D ClampedPosition(
		FMath::Clamp(ScreenPosition.X, MinX, MaxX),
		FMath::Clamp(ScreenPosition.Y, MinY, MaxY));

	bOutClamped = !bInFront || FVector2D::Distance(ScreenPosition, ClampedPosition) > 0.5f;
	OutScreenPosition = ClampedPosition;
	return true;
}

void ASimCopterMissionSystemActor::PushMissionLogMessage(const FString& Text, const FLinearColor& Color)
{
	if (!bShowMissionMessageLog)
	{
		return;
	}

	FSimCopterMissionLogEntry Entry;
	Entry.Text = Text;
	Entry.Color = Color;
	Entry.RemainingSeconds = MessageLogDurationSeconds;
	MissionMessageLog.Insert(Entry, 0);

	while (MissionMessageLog.Num() > MaxMessageLogEntries)
	{
		MissionMessageLog.Pop(EAllowShrinking::No);
	}

	UE_LOG(LogTemp, Display, TEXT("[Mission] %s"), *Text);
	RefreshMessageLogWidget();
}

FString ASimCopterMissionSystemActor::FormatMissionUiMessage(const SimCopterMissions::FSimCopterMissionUiMessage& Message, FLinearColor& OutColor) const
{
	const FString MissionName = !Message.MissionName.IsEmpty()
		? Message.MissionName
		: FString::Printf(TEXT("Mission #%d"), Message.EventId);

	switch (Message.Kind)
	{
	case 5:
		OutColor = FLinearColor(0.65f, 0.9f, 1.0f, 1.0f);
		return FString::Printf(TEXT("Mission started: %s"), *MissionName);
	case 6:
		OutColor = Message.ValueA < 0 ? FLinearColor(1.0f, 0.38f, 0.32f, 1.0f) : FLinearColor(0.55f, 1.0f, 0.55f, 1.0f);
		return FString::Printf(
			TEXT("Mission ended: %s (%s, %s)"),
			*MissionName,
			*FormatSignedAmount(Message.ValueA, TEXT("points")),
			*FormatSignedAmount(Message.ValueB, TEXT("cash")));
	case 8:
		OutColor = Message.ValueA < 0 ? FLinearColor(1.0f, 0.38f, 0.32f, 1.0f) : FLinearColor(0.55f, 1.0f, 0.55f, 1.0f);
		return FString::Printf(
			TEXT("%s: %s (%s)"),
			GetMissionDeltaLabel(Message.TextId),
			*FormatSignedAmount(Message.ValueA, TEXT("points")),
			*MissionName);
	case 9:
		OutColor = Message.ValueA < 0 ? FLinearColor(1.0f, 0.38f, 0.32f, 1.0f) : FLinearColor(1.0f, 0.86f, 0.34f, 1.0f);
		return FString::Printf(
			TEXT("%s: %s (%s)"),
			GetMissionDeltaLabel(Message.TextId),
			*FormatSignedAmount(Message.ValueA, TEXT("cash")),
			*MissionName);
	default:
		OutColor = FLinearColor::White;
		return FString();
	}
}
