// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/SimCopterPlayerController.h"

#include "Audio/SimCopterAudioSubsystem.h"
#include "Audio/SimCopterRadio.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/SimCopterSessionSubsystem.h"
#include "Game/SimCopterSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Missions/SimCopterMissionSystemActor.h"
#include "UI/SSimCopterCitySettings.h"
#include "UI/SSimCopterControlSettings.h"
#include "UI/SSimCopterGraphicsSettings.h"
#include "UI/SSimCopterMessageBox.h"
#include "UI/SSimCopterSettingsMenu.h"
#include "UI/SSimCopterSoundSettings.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/SOverlay.h"

#define LOCTEXT_NAMESPACE "SimCopterPlayerController"

namespace
{
/** The action DefaultInput.ini binds to Escape. */
const TCHAR* const SettingsAction = TEXT("SimCopterSettingsMenu");
}

ASimCopterPlayerController::ASimCopterPlayerController()
{
	bShowMouseCursor = true;
}

void ASimCopterPlayerController::BeginPlay()
{
	Super::BeginPlay();

	Art = NewObject<USimCopterHangarArt>(this, TEXT("SettingsArt"));
	Art->SetOriginalGameRoot(ResolveOriginalGameRoot());

	// The stored settings are the mixer's and the renderer's starting point; the front end never
	// gets a chance to apply them because it runs in a different world.
	if (USimCopterSettings* Settings = USimCopterSettings::Get(this))
	{
		Settings->ApplyAll(this);
	}
}

void ASimCopterPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseScreen();
	Super::EndPlay(EndPlayReason);
}

void ASimCopterPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent != nullptr)
	{
		// bExecuteWhenPaused, because the screen pauses the sim and then has to be closable.
		FInputActionBinding& Binding = InputComponent->BindAction(
			SettingsAction, IE_Pressed, this, &ASimCopterPlayerController::SimSettings);
		Binding.bExecuteWhenPaused = true;
	}
}

FString ASimCopterPlayerController::ResolveOriginalGameRoot()
{
	TArray<FString, TInlineAllocator<3>> Candidates;
	Candidates.Add(FPaths::ProjectContentDir() / TEXT("OriginalGame"));
	Candidates.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("Reference/SimCopterOriginalGame")));
	Candidates.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame")));

	for (FString Candidate : Candidates)
	{
		Candidate = FPaths::ConvertRelativePathToFull(Candidate);
		FPaths::NormalizeDirectoryName(Candidate);
		if (FPaths::DirectoryExists(Candidate))
		{
			return Candidate;
		}
	}

	return FString();
}

void ASimCopterPlayerController::SimSettings()
{
	// The original raises the page from command 0x3f and answers Escape on the page itself with
	// 0x3ea, so the key does not toggle - but a second press with the screen already up should
	// still close it rather than doing nothing.
	if (IsSettingsOpen())
	{
		CloseScreen();
		return;
	}

	OpenSettings();
}

// ---------------------------------------------------------------------------------------------
// The reference-counted pause, FUN_004346c0 / [vt+0x44]
// ---------------------------------------------------------------------------------------------

void ASimCopterPlayerController::PushPause()
{
	if (PauseDepth++ == 0)
	{
		SetPause(true);
	}
}

void ASimCopterPlayerController::PopPause()
{
	if (PauseDepth > 0 && --PauseDepth == 0)
	{
		SetPause(false);
	}
}

// ---------------------------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------------------------

bool ASimCopterPlayerController::IsUserGame() const
{
	const USimCopterSessionSubsystem* Session = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<USimCopterSessionSubsystem>()
		: nullptr;

	// DAT_00518d50 == 1. A session entered directly (PIE into the city level) has no kind at all;
	// it plays a single SimCity file the way mode 1 does, so it counts as a user game.
	return Session == nullptr || Session->GetSessionKind() != ESimCopterSessionKind::Career;
}

ASimCopterMissionSystemActor* ASimCopterPlayerController::ResolveMissionSystem() const
{
	return Cast<ASimCopterMissionSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass()));
}

void ASimCopterPlayerController::OpenSettings()
{
	PushPause();
	EnterScreen(ESimCopterSettingsScreen::Menu);
}

TSharedRef<SWidget> ASimCopterPlayerController::BuildScreen(const ESimCopterSettingsScreen NewScreen)
{
	switch (NewScreen)
	{
	case ESimCopterSettingsScreen::CitySettings:
	{
		FSimCopterCitySettingsValues Values;
		if (const ASimCopterMissionSystemActor* Missions = ResolveMissionSystem())
		{
			const SimCopterMissions::FSimCopterCareerCity& City = Missions->GetSessionCareerCity();
			Values.Difficulty = City.Difficulty;
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Values.Weights); ++Index)
			{
				Values.Weights[Index] = City.Weights[Index];
			}
		}

		return SNew(SSimCopterCitySettings)
			.Art(Art)
			.Values(Values)
			.OnAccepted(FOnSimCopterCitySettingsAccepted::CreateLambda(
				[this](const FSimCopterCitySettingsValues& Accepted)
				{
					// FUN_00440ec0 writes the eight values straight back into the live block.
					if (ASimCopterMissionSystemActor* Missions = ResolveMissionSystem())
					{
						SimCopterMissions::FSimCopterCareerCity City = Missions->GetSessionCareerCity();
						City.Difficulty = Accepted.Difficulty;
						for (int32 Index = 0; Index < UE_ARRAY_COUNT(Accepted.Weights); ++Index)
						{
							City.Weights[Index] = Accepted.Weights[Index];
						}
						Missions->SetSessionCareerCity(City);
					}
					EnterScreen(ESimCopterSettingsScreen::Menu);
				}))
			.OnCancelled(FSimpleDelegate::CreateLambda([this]()
			{
				EnterScreen(ESimCopterSettingsScreen::Menu);
			}));
	}

	case ESimCopterSettingsScreen::Graphics:
		return SNew(SSimCopterGraphicsSettings)
			.Art(Art)
			.OnAccepted(FOnSimCopterGraphicsSettingsClosed::CreateLambda([this]()
			{
				EnterScreen(ESimCopterSettingsScreen::Menu);
			}))
			.OnCancelled(FOnSimCopterGraphicsSettingsClosed::CreateLambda([this]()
			{
				EnterScreen(ESimCopterSettingsScreen::Menu);
			}));

	case ESimCopterSettingsScreen::Sound:
	{
		USimCopterSettings* Settings = USimCopterSettings::Get(this);
		const USimCopterRadioSubsystem* Radio = USimCopterRadioSubsystem::Get(this);

		FSimCopterSoundSettingsValues Values;
		if (Settings != nullptr)
		{
			Values.GameVolume = Settings->GetGameVolume();
			Values.RadioVolume = Settings->GetRadioVolume();
			Values.RadioStation = Settings->GetRadioStation();
			Values.bDj = Settings->IsDjEnabled();
			Values.bCommercials = Settings->AreCommercialsEnabled();
			Values.bAutoQuiet = Settings->IsAutoQuietEnabled();
		}

		TArray<FString> CallSigns;
		if (Radio != nullptr)
		{
			for (const FSimCopterRadioStation& Station : Radio->GetStations())
			{
				CallSigns.Add(Station.CallSign);
			}
		}

		// The page previews as it is dragged, so both Preview and Accept push the same values -
		// Accept only adds the save.
		const auto Push = [this](const FSimCopterSoundSettingsValues& New)
		{
			if (USimCopterSettings* Store = USimCopterSettings::Get(this))
			{
				Store->SetGameVolume(New.GameVolume);
				Store->SetRadioVolume(New.RadioVolume);
				Store->SetRadioStation(New.RadioStation);
				Store->SetDjEnabled(New.bDj);
				Store->SetCommercialsEnabled(New.bCommercials);
				Store->SetAutoQuietEnabled(New.bAutoQuiet);
				Store->ApplyAll(this);
			}
		};

		return SNew(SSimCopterSoundSettings)
			.Art(Art)
			.Values(Values)
			.StationCount(Radio != nullptr ? Radio->GetStationCount() : 0)
			.StationCallSigns(CallSigns)
			.OnPreviewChanged(FOnSimCopterSoundSettingsAccepted::CreateLambda(Push))
			.OnAccepted(FOnSimCopterSoundSettingsAccepted::CreateLambda(
				[this, Push](const FSimCopterSoundSettingsValues& New)
				{
					Push(New);
					if (USimCopterSettings* Store = USimCopterSettings::Get(this))
					{
						Store->Save();
					}
					EnterScreen(ESimCopterSettingsScreen::Menu);
				}))
			.OnCancelled(FSimpleDelegate::CreateLambda([this]()
			{
				EnterScreen(ESimCopterSettingsScreen::Menu);
			}));
	}

	case ESimCopterSettingsScreen::Controls:
		return SNew(SSimCopterControlSettings)
			.Art(Art)
			.OnAccepted(FOnSimCopterControlSettingsClosed::CreateLambda([this]()
			{
				EnterScreen(ESimCopterSettingsScreen::Menu);
			}))
			.OnCancelled(FOnSimCopterControlSettingsClosed::CreateLambda([this]()
			{
				EnterScreen(ESimCopterSettingsScreen::Menu);
			}));

	case ESimCopterSettingsScreen::Message:
		return SNew(SSimCopterMessageBox)
			.Art(Art)
			.Message(PendingMessage)
			.OnDismissed(FSimpleDelegate::CreateLambda([this]()
			{
				EnterScreen(ESimCopterSettingsScreen::Menu);
			}));

	case ESimCopterSettingsScreen::Confirm:
		return SNew(SSimCopterMessageBox)
			.Art(Art)
			.Message(PendingMessage)
			.Confirm(true)
			.OnConfirmed(FSimpleDelegate::CreateLambda([this]() { LeaveCity(); }))
			.OnDismissed(FSimpleDelegate::CreateLambda([this]()
			{
				EnterScreen(ESimCopterSettingsScreen::Menu);
			}));

	default:
		return SNew(SSimCopterSettingsMenu)
			.Art(Art)
			.AllowCitySettings(IsUserGame())
			.OnItemChosen(FOnSimCopterSettingsItemChosen::CreateUObject(
				this, &ASimCopterPlayerController::HandleSettingsItem));
	}
}

void ASimCopterPlayerController::EnterScreen(const ESimCopterSettingsScreen NewScreen)
{
	if (GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	// Only the widget changes; the pause stays where it is, because the screen as a whole is still
	// up and the original's counter is likewise untouched moving between its pages.
	if (ScreenWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(ScreenWidget.ToSharedRef());
		ScreenWidget.Reset();
	}

	TSharedRef<SWidget> Content = BuildScreen(NewScreen);
	ScreenWidget =
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			Content
		];
	Screen = NewScreen;

	// Above the cockpit overlays, which sit at 25.
	GEngine->GameViewport->AddViewportWidgetContent(ScreenWidget.ToSharedRef(), 200);

	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetWidgetToFocus(Content);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}

void ASimCopterPlayerController::CloseScreen()
{
	if (!ScreenWidget.IsValid())
	{
		return;
	}

	if (GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(ScreenWidget.ToSharedRef());
	}
	ScreenWidget.Reset();
	Screen = ESimCopterSettingsScreen::None;

	PopPause();
	RestoreGameInput();
}

void ASimCopterPlayerController::RestoreGameInput()
{
	// The cockpit needs the pointer for the dash and the tool flaps, so it is GameAndUI rather
	// than GameOnly - the same mode the helicopter pawn sets when it is possessed.
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}

// ---------------------------------------------------------------------------------------------
// FUN_0044c9e0's switch
// ---------------------------------------------------------------------------------------------

void ASimCopterPlayerController::ShowMessage(const FText& Message)
{
	PendingMessage = Message;
	EnterScreen(ESimCopterSettingsScreen::Message);
}

void ASimCopterPlayerController::HandleSettingsItem(const ESimCopterSettingsItem Item)
{
	switch (Item)
	{
	case ESimCopterSettingsItem::CitySettings:
		EnterScreen(ESimCopterSettingsScreen::CitySettings);
		return;

	case ESimCopterSettingsItem::Graphics:
		EnterScreen(ESimCopterSettingsScreen::Graphics);
		return;

	case ESimCopterSettingsItem::Sound:
		EnterScreen(ESimCopterSettingsScreen::Sound);
		return;

	case ESimCopterSettingsItem::Controls:
		EnterScreen(ESimCopterSettingsScreen::Controls);
		return;

	case ESimCopterSettingsItem::SaveGame:
	case ESimCopterSettingsItem::SaveGameAs:
		// FUN_0044c9e0 runs FUN_004200e0 / FUN_00420670 here and reports STRINGTABLE 48 "Game
		// saved!" in a message box. The remake has no save system, so it refuses in the same box
		// rather than pretending - the same treatment the main menu gives Open Career/User Game.
		ShowMessage(LOCTEXT(
			"NoSaving",
			"Saving is not implemented yet.\nThe city and your progress are lost when you leave."));
		return;

	case ESimCopterSettingsItem::LeaveCity:
		// FUN_004352f0(0, 11, 0x20002): a modal Yes/No on STRINGTABLE 11.
		PendingMessage = LOCTEXT("LeaveCityConfirm", "Are you sure you want to leave this city?");
		EnterScreen(ESimCopterSettingsScreen::Confirm);
		return;

	case ESimCopterSettingsItem::Continue:
		// [vt+0x44] resume, then close the page.
		CloseScreen();
		return;
	}
}

void ASimCopterPlayerController::LeaveCity()
{
	// The original follows the confirm with a second Yes/No on STRINGTABLE 49 ("Do you want to
	// save the game?"). With no save system there is nothing to ask, so the port goes straight to
	// the main menu - which is what answering No does.
	if (USimCopterSessionSubsystem* Session = GetGameInstance() != nullptr
			? GetGameInstance()->GetSubsystem<USimCopterSessionSubsystem>()
			: nullptr)
	{
		Session->ClearPendingSession();
	}

	if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
	{
		Audio->StopRadio();
		Audio->StopMusic();
	}

	CloseScreen();
	UGameplayStatics::OpenLevel(this, FName(USimCopterSessionSubsystem::GetMainMenuLevelName()));
}

#undef LOCTEXT_NAMESPACE
