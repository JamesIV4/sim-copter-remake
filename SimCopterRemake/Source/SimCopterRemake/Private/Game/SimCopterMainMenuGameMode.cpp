// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/SimCopterMainMenuGameMode.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/SimCopterCareerProgression.h"
#include "Game/SimCopterSessionSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/SSimCopterCareerSelect.h"
#include "UI/SSimCopterMainMenu.h"
#include "UI/SSimCopterMessageBox.h"
#include "UI/SSimCopterUserCityPicker.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/SOverlay.h"

#define LOCTEXT_NAMESPACE "SimCopterMainMenuGameMode"

ASimCopterMainMenuGameMode::ASimCopterMainMenuGameMode()
{
	// The front end has nothing to possess; the shell is the whole level.
	DefaultPawnClass = nullptr;
}

void ASimCopterMainMenuGameMode::BeginPlay()
{
	Super::BeginPlay();

	// A session left over from a previous city would otherwise be re-applied on the next start.
	if (USimCopterSessionSubsystem* Session = GetGameInstance() != nullptr
			? GetGameInstance()->GetSubsystem<USimCopterSessionSubsystem>()
			: nullptr)
	{
		Session->ClearPendingSession();
	}

	Art = NewObject<USimCopterHangarArt>(this, TEXT("FrontEndArt"));
	Art->SetOriginalGameRoot(ResolveOriginalGameRoot());

	EnterScreen(ESimCopterFrontEndScreen::MainMenu);
}

void ASimCopterMainMenuGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseScreen();
	Super::EndPlay(EndPlayReason);
}

FString ASimCopterMainMenuGameMode::ResolveOriginalGameRoot() const
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

TSharedRef<SWidget> ASimCopterMainMenuGameMode::BuildScreen(const ESimCopterFrontEndScreen NewScreen)
{
	switch (NewScreen)
	{
	case ESimCopterFrontEndScreen::CareerSelect:
	{
		// A new career always offers cities 0, 1 and 2 (FUN_00457c90's null-trio branch). The
		// successor graph only comes into play once a career is running, which the remake reaches
		// through the mission system rather than through this screen.
		TArray<int32> Choices;
		SimCopterCareerProgression::GetNewCareerChoices(Choices);

		return SNew(SSimCopterCareerSelect)
			.Art(Art)
			.Cities(Choices)
			.AllowCancel(true)
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
	if (!ScreenWidget.IsValid())
	{
		return;
	}

	if (GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(ScreenWidget.ToSharedRef());
	}
	ScreenWidget.Reset();
	Screen = ESimCopterFrontEndScreen::None;
}

void ASimCopterMainMenuGameMode::HandleMainMenuItem(const ESimCopterMainMenuItem Item)
{
	// FUN_0044c710's switch, item for item.
	switch (Item)
	{
	case ESimCopterMainMenuItem::NewCareerGame:
		// app[0xb0] = 1, then EnterState(5).
		EnterScreen(ESimCopterFrontEndScreen::CareerSelect);
		return;

	case ESimCopterMainMenuItem::NewUserGame:
		// The original opens GetOpenFileName here; the remake lists the same folder itself.
		EnterScreen(ESimCopterFrontEndScreen::UserCityPicker);
		return;

	case ESimCopterMainMenuItem::OpenCareerGame:
	case ESimCopterMainMenuItem::OpenUserGame:
		// Both open a saved game. The remake has no save system, so it refuses the same way the
		// original's demo build did (STRINGTABLE 653) rather than opening a picker with nothing
		// in it. The items stay on the menu because the original's item set is fixed at five.
		PendingMessage = LOCTEXT(
			"NoSavedGames",
			"Saved games are not implemented yet. Select 'New Career Game' or 'New User Game'.");
		EnterScreen(ESimCopterFrontEndScreen::Message);
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
		PendingMessage = FText::Format(
			LOCTEXT(
				"MissingCareerCity",
				"Cannot find {0}.\nThe original game folder has to be in place under Reference/SimCopterOriginalGame."),
			FText::FromString(USimCopterSessionSubsystem::ResolveCareerCityFilePath(CareerCityIndex)));
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

#undef LOCTEXT_NAMESPACE
