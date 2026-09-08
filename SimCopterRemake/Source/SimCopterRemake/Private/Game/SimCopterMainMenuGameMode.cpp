// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/SimCopterMainMenuGameMode.h"

#include "Audio/SimCopterAudioSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Formats/SimCopterOriginalGamePaths.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/SimCopterCareerProgression.h"
#include "Game/SimCopterCareerSubsystem.h"
#include "Game/SimCopterSaveSubsystem.h"
#include "Game/SimCopterSessionSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/SSimCopterCareerSelect.h"
#include "UI/SSimCopterMainMenu.h"
#include "UI/SSimCopterMessageBox.h"
#include "UI/SSimCopterSaveGamePicker.h"
#include "UI/SSimCopterUserCityPicker.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/SOverlay.h"
#include "UI/SSimCopterIntro.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "MediaSoundComponent.h"
#include "Misc/App.h"
#include "HAL/PlatformTime.h"

#define LOCTEXT_NAMESPACE "SimCopterMainMenuGameMode"

ASimCopterMainMenuGameMode::ASimCopterMainMenuGameMode()
{
	// The front end has nothing to possess; the shell is the whole level.
	DefaultPawnClass = nullptr;
	PrimaryActorTick.bCanEverTick = true;
}

void ASimCopterMainMenuGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Suppressed here too, not only in the city's player controller: the front end is the first
	// thing to come up, so setting it on the viewport client from here means the engine's
	// "PAUSED / START RESUME" overlay can never appear even on the very first pause of a session.
	if (GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		GEngine->GameViewport->SetSuppressTransitionMessage(true);
	}

	bool bOpenCareerSelect = false;
	bool bShowIntros = false;
	if (USimCopterSessionSubsystem* Session = GetGameInstance() != nullptr
			? GetGameInstance()->GetSubsystem<USimCopterSessionSubsystem>()
			: nullptr)
	{
		if (Session->HasCompletedCareerCity())
		{
			bOpenCareerSelect = true;
		}
		bShowIntros = !Session->bStartupIntrosShown && !bOpenCareerSelect && FApp::CanEverRender();
		Session->bStartupIntrosShown = true;
		Session->ClearPendingSession();
	}

	// The front end is the first thing that runs, so it is where a fresh install learns it has no
	// original game data: this drops the named folder and its note next to the executable rather
	// than waiting for the player to pick a city and get a dead end.
	SimCopterOriginalGame::EnsurePlayerRootFolder();

	Art = NewObject<USimCopterHangarArt>(this, TEXT("FrontEndArt"));
	Art->SetOriginalGameRoot(ResolveOriginalGameRoot());

	if (bShowIntros)
	{
		EnterScreen(ESimCopterFrontEndScreen::Intro);
		PlayNextIntro();
	}
	else if (bOpenCareerSelect)
	{
		EnterScreen(ESimCopterFrontEndScreen::CareerSelect);
	}
	else
	{
		EnterScreen(ESimCopterFrontEndScreen::MainMenu);
	}
}

void ASimCopterMainMenuGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseScreen();
	Super::EndPlay(EndPlayReason);
}

FString ASimCopterMainMenuGameMode::ResolveOriginalGameRoot() const
{
	return SimCopterOriginalGame::ResolveRoot();
}

TSharedRef<SWidget> ASimCopterMainMenuGameMode::BuildScreen(const ESimCopterFrontEndScreen NewScreen)
{
	switch (NewScreen)
	{
	case ESimCopterFrontEndScreen::Intro:
		IntroPlayer = NewObject<UMediaPlayer>(this);
		IntroPlayer->PlayOnOpen = true;
		IntroPlayer->SetLooping(false);
		IntroPlayer->OnEndReached.AddDynamic(this, &ASimCopterMainMenuGameMode::RequestIntroAdvance);
		IntroPlayer->OnMediaOpenFailed.AddDynamic(this, &ASimCopterMainMenuGameMode::IntroOpenFailed);
		IntroTexture = NewObject<UMediaTexture>(this);
		IntroTexture->AutoClear = true;
		IntroTexture->ClearColor = FLinearColor::Black;
		IntroTexture->NewStyleOutput = true;
		IntroTexture->SetMediaPlayer(IntroPlayer);
		IntroTexture->UpdateResource();
		IntroSound = NewObject<UMediaSoundComponent>(this);
		IntroSound->bIsUISound = true;
		IntroSound->SetMediaPlayer(IntroPlayer);
		IntroSound->RegisterComponent();
		IntroSound->Start();
		IntroBrush.SetResourceObject(IntroTexture);
		IntroBrush.ImageSize = FVector2D(640, 280);
		return SNew(SSimCopterIntro).MovieBrush(&IntroBrush)
			.OnSkip(FSimpleDelegate::CreateUObject(this, &ASimCopterMainMenuGameMode::RequestIntroAdvance));
	case ESimCopterFrontEndScreen::CareerSelect:
	{
		TArray<int32> Choices;
		bool bAdvancingCareer = false;
		if (USimCopterSessionSubsystem* Session = GetGameInstance() != nullptr
			? GetGameInstance()->GetSubsystem<USimCopterSessionSubsystem>()
			: nullptr)
		{
			if (Session->HasCompletedCareerCity())
			{
				SimCopterCareerProgression::GetSuccessors(Session->GetCompletedCareerCityIndex(), Choices);
				Session->ClearCompletedCareerCity();
				bAdvancingCareer = Choices.Num() > 0;
			}
		}

		if (Choices.Num() == 0)
		{
			// City 29 (Metropolis, Final Level) has an all -1 successor trio, so finishing it
			// leaves nothing to advance into and this is a brand new career, not a continuation:
			// drop the completed city's carried money and fleet before offering {0, 1, 2}.
			if (USimCopterCareerSubsystem* Career = GetGameInstance() != nullptr
				? GetGameInstance()->GetSubsystem<USimCopterCareerSubsystem>()
				: nullptr)
			{
				Career->ClearPendingCityTransfer();
			}
			SimCopterCareerProgression::GetNewCareerChoices(Choices);
		}

		// FUN_00457c90: Cancel exists only for a NEW career (`screen[0x2e]`); an advancement gets
		// one centred OK and its Esc does nothing. That is not decoration - backing out of an
		// advancement drops the player at the main menu with a career they can no longer re-enter.
		return SNew(SSimCopterCareerSelect)
			.Art(Art)
			.Cities(Choices)
			.AllowCancel(!bAdvancingCareer)
			.OnAccepted(FOnSimCopterCareerCityChosen::CreateUObject(
				this, &ASimCopterMainMenuGameMode::HandleCareerCityChosen))
			.OnCancelled(FSimpleDelegate::CreateLambda([this]()
			{
				EnterScreen(ESimCopterFrontEndScreen::MainMenu);
			}));
	}

	case ESimCopterFrontEndScreen::UserCityPicker:
	{
		TArray<FString> CityPaths;
		USimCopterSessionSubsystem::GetUserCityFilePaths(CityPaths);

		return SNew(SSimCopterUserCityPicker)
			.Art(Art)
			.CityFilePaths(CityPaths)
			.OnAccepted(FOnSimCopterUserCityChosen::CreateUObject(
				this, &ASimCopterMainMenuGameMode::HandleUserCityChosen))
			.OnCancelled(FSimpleDelegate::CreateLambda([this]()
			{
				EnterScreen(ESimCopterFrontEndScreen::MainMenu);
			}));
	}

	case ESimCopterFrontEndScreen::SavedGamePicker:
	{
		TArray<FSimCopterSaveSummary> Saves;
		if (const USimCopterSaveSubsystem* SaveSubsystem = USimCopterSaveSubsystem::Get(this))
		{
			SaveSubsystem->GetSaveSummaries(PendingSaveKind, Saves);
		}

		return SNew(SSimCopterSaveGamePicker)
			.Art(Art)
			.Kind(PendingSaveKind)
			.Saves(Saves)
			.OnAccepted(FOnSimCopterSaveGameChosen::CreateUObject(
				this, &ASimCopterMainMenuGameMode::HandleSavedGameChosen))
			.OnCancelled(FSimpleDelegate::CreateLambda([this]()
			{
				EnterScreen(ESimCopterFrontEndScreen::MainMenu);
			}));
	}

	case ESimCopterFrontEndScreen::Message:
		return SNew(SSimCopterMessageBox)
			.Art(Art)
			.Message(PendingMessage)
			.OnDismissed(FSimpleDelegate::CreateLambda([this]()
			{
				EnterScreen(ESimCopterFrontEndScreen::MainMenu);
			}));

	default:
		return SNew(SSimCopterMainMenu)
			.Art(Art)
			.OnItemChosen(FOnSimCopterMainMenuItemChosen::CreateUObject(
				this, &ASimCopterMainMenuGameMode::HandleMainMenuItem));
	}
}

void ASimCopterMainMenuGameMode::EnterScreen(const ESimCopterFrontEndScreen NewScreen)
{
	if (GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	CloseScreen();

	// A front-end page owns its standalone sounds in the original. The remake's audio subsystem
	// is world-scoped, so explicitly retire the outgoing page's one-shots before the next page
	// constructs its own bed. Also silence the world radio defensively: no helicopter is possessed
	// in the shell, and the main menu must have only its own menuback.wav music channel.
	if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
	{
		Audio->StopRadio();
		Audio->StopMusic();
		Audio->StopStandaloneSounds();
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

	GEngine->GameViewport->AddViewportWidgetContent(ScreenWidget.ToSharedRef(), 100);

	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
	{
		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetWidgetToFocus(Content);
		PlayerController->SetInputMode(InputMode);
		PlayerController->bShowMouseCursor = true;
	}
	else if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocus(Content, EFocusCause::SetDirectly);
	}
}

void ASimCopterMainMenuGameMode::CloseScreen()
{
	bAdvanceIntro = false;
	if (IntroPlayer)
	{
		IntroPlayer->OnEndReached.RemoveAll(this);
		IntroPlayer->OnMediaOpenFailed.RemoveAll(this);
		IntroPlayer->Close();
	}
	if (IntroSound)
	{
		IntroSound->Stop();
		IntroSound->DestroyComponent();
		IntroSound = nullptr;
	}

	if (Art != nullptr)
	{
		Art->StopMenuSkyMovie();
		Art->StopCareerCityMovies();
	}

	if (!ScreenWidget.IsValid())
	{
		return;
	}

	if (GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(ScreenWidget.ToSharedRef());
	}
	ScreenWidget.Reset();
	IntroBrush.SetResourceObject(nullptr);
	IntroTexture = nullptr;
	IntroPlayer = nullptr;
	Screen = ESimCopterFrontEndScreen::None;
}

void ASimCopterMainMenuGameMode::HandleMainMenuItem(const ESimCopterMainMenuItem Item)
{
	// FUN_0044c710's switch, item for item.
	switch (Item)
	{
	case ESimCopterMainMenuItem::NewCareerGame:
		// app[0xb0] = 1, then EnterState(5).
		if (USimCopterSaveSubsystem* Saves = USimCopterSaveSubsystem::Get(this))
		{
			Saves->BeginNewGame();
		}
		EnterScreen(ESimCopterFrontEndScreen::CareerSelect);
		return;

	case ESimCopterMainMenuItem::NewUserGame:
		// The original opens GetOpenFileName here; the remake lists the same folder itself.
		if (USimCopterSaveSubsystem* Saves = USimCopterSaveSubsystem::Get(this))
		{
			Saves->BeginNewGame();
		}
		EnterScreen(ESimCopterFrontEndScreen::UserCityPicker);
		return;

	case ESimCopterMainMenuItem::OpenCareerGame:
		PendingSaveKind = ESimCopterSessionKind::Career;
		EnterScreen(ESimCopterFrontEndScreen::SavedGamePicker);
		return;

	case ESimCopterMainMenuItem::OpenUserGame:
		PendingSaveKind = ESimCopterSessionKind::User;
		EnterScreen(ESimCopterFrontEndScreen::SavedGamePicker);
		return;

	case ESimCopterMainMenuItem::Quit:
		// EnterState(1). No confirmation - the original does not ask here either.
		QuitGame();
		return;
	}
}

void ASimCopterMainMenuGameMode::HandleCareerCityChosen(const int32 CareerCityIndex)
{
	USimCopterSessionSubsystem* Session = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<USimCopterSessionSubsystem>()
		: nullptr;
	if (Session == nullptr)
	{
		return;
	}

	Session->RequestCareerCity(CareerCityIndex);

	const FString CityFile = Session->GetCityFilePath();
	if (CityFile.IsEmpty() || !FPaths::FileExists(CityFile))
	{
		Session->ClearPendingSession();
		// Nothing resolved, so make the folder the message names exist before naming it - a player
		// who has never had the data has nowhere to put it otherwise.
		SimCopterOriginalGame::EnsurePlayerRootFolder();
		PendingMessage = FText::Format(
			LOCTEXT("MissingCareerCity", "Cannot find cities/career/city{0}.sc2.\n\n{1}"),
			FText::AsNumber(CareerCityIndex, &FNumberFormattingOptions::DefaultNoGrouping()),
			SimCopterOriginalGame::GetMissingDataHint());
		EnterScreen(ESimCopterFrontEndScreen::Message);
		return;
	}

	StartPendingSession();
}

void ASimCopterMainMenuGameMode::HandleUserCityChosen(const FString& CityFilePath)
{
	USimCopterSessionSubsystem* Session = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<USimCopterSessionSubsystem>()
		: nullptr;
	if (Session == nullptr || CityFilePath.IsEmpty())
	{
		return;
	}

	Session->RequestUserCity(CityFilePath);
	StartPendingSession();
}

void ASimCopterMainMenuGameMode::HandleSavedGameChosen(const FString& SlotName)
{
	USimCopterSaveSubsystem* Saves = USimCopterSaveSubsystem::Get(this);
	if (Saves == nullptr)
	{
		PendingMessage = LOCTEXT("NoSaveService", "The saved-game service is unavailable.");
		EnterScreen(ESimCopterFrontEndScreen::Message);
		return;
	}

	FString Error;
	if (!Saves->LoadGame(SlotName, PendingSaveKind, Error))
	{
		PendingMessage = FText::FromString(Error);
		EnterScreen(ESimCopterFrontEndScreen::Message);
		return;
	}
	StartPendingSession();
}

void ASimCopterMainMenuGameMode::StartPendingSession()
{
	USimCopterSessionSubsystem* Session = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<USimCopterSessionSubsystem>()
		: nullptr;

	if (Session == nullptr || !Session->HasPendingSession())
	{
		UE_LOG(LogTemp, Warning, TEXT("SimCopter front end: no session was requested, staying in the shell."));
		return;
	}

	UE_LOG(LogTemp, Display, TEXT("SimCopter front end: starting %s session, city file '%s'"),
		Session->GetSessionKind() == ESimCopterSessionKind::Career ? TEXT("career") : TEXT("user"),
		*Session->GetCityFilePath());

	CloseScreen();
	UGameplayStatics::OpenLevel(this, FName(USimCopterSessionSubsystem::GetCityLevelName()));
}

void ASimCopterMainMenuGameMode::QuitGame()
{
	UKismetSystemLibrary::QuitGame(this, UGameplayStatics::GetPlayerController(this, 0), EQuitPreference::Quit, false);
}

void ASimCopterMainMenuGameMode::SimNewCareer(int32 CareerCityIndex)
{
	if (USimCopterSaveSubsystem* Saves = USimCopterSaveSubsystem::Get(this))
	{
		Saves->BeginNewGame();
	}

	USimCopterSessionSubsystem* Session = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<USimCopterSessionSubsystem>()
		: nullptr;
	if (Session == nullptr)
	{
		return;
	}

	Session->RequestCareerCity(CareerCityIndex);
	StartPendingSession();
}

void ASimCopterMainMenuGameMode::SimNewUserGame(int32 CityIndex)
{
	if (USimCopterSaveSubsystem* Saves = USimCopterSaveSubsystem::Get(this))
	{
		Saves->BeginNewGame();
	}

	USimCopterSessionSubsystem* Session = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<USimCopterSessionSubsystem>()
		: nullptr;
	if (Session == nullptr)
	{
		return;
	}

	TArray<FString> CityPaths;
	USimCopterSessionSubsystem::GetUserCityFilePaths(CityPaths);
	if (!CityPaths.IsValidIndex(CityIndex))
	{
		UE_LOG(LogTemp, Display, TEXT("SimNewUserGame <index> - available cities:"));
		for (int32 Index = 0; Index < CityPaths.Num(); ++Index)
		{
			UE_LOG(LogTemp, Display, TEXT("  %2d  %s"), Index, *FPaths::GetCleanFilename(CityPaths[Index]));
		}
		return;
	}

	Session->RequestUserCity(CityPaths[CityIndex]);
	StartPendingSession();
}

void ASimCopterMainMenuGameMode::SimLoadGame(const FString& SlotName)
{
	USimCopterSaveSubsystem* Saves = USimCopterSaveSubsystem::Get(this);
	if (Saves == nullptr)
	{
		return;
	}

	// Console loading accepts either kind; the file itself remains validated before travel.
	FString Error;
	if (!Saves->LoadGame(SlotName, ESimCopterSessionKind::None, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("SimLoadGame: %s"), *Error);
		return;
	}
	StartPendingSession();
}

void ASimCopterMainMenuGameMode::RequestIntroAdvance()
{
	if (Screen == ESimCopterFrontEndScreen::Intro) bAdvanceIntro = true;
}

void ASimCopterMainMenuGameMode::IntroOpenFailed(FString Url)
{
	UE_LOG(LogTemp, Warning, TEXT("Could not play intro: %s"), *Url);
	RequestIntroAdvance();
}

void ASimCopterMainMenuGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Screen != ESimCopterFrontEndScreen::Intro || !IntroPlayer) return;
	// Defer transitions out of Slate/media callbacks; coalesce presses in the same frame.
	// A failed backend must never strand startup on a black screen.
	if (bAdvanceIntro || IntroPlayer->HasError() ||
		(IntroPlayer->IsPreparing() && FPlatformTime::Seconds() - IntroOpenedAt > 15.0))
	{
		PlayNextIntro();
	}
}

void ASimCopterMainMenuGameMode::PlayNextIntro()
{
	// SCHOOK: LoadIntroMovies 0x0044cbb0 - INTRO1 then INTRO2, second at (0,100)
	// in a 640x280 display rectangle. Both are centred within the 640x480 screen.
	IntroPlayer->Close();
	bAdvanceIntro = false;
	++IntroIndex;
	if (IntroIndex > 2)
	{
		EnterScreen(ESimCopterFrontEndScreen::MainMenu);
		return;
	}
	IntroOpenedAt = FPlatformTime::Seconds();
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() /
		FString::Printf(TEXT("Generated/Movies/Intro/INTRO%d.mp4"), IntroIndex));
	if (!IntroPlayer->OpenFile(Path)) IntroOpenFailed(Path);
}

#undef LOCTEXT_NAMESPACE
