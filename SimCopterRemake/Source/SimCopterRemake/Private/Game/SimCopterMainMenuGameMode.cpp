// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/SimCopterMainMenuGameMode.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Game/SimCopterSessionSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/SSimCopterMainMenu.h"
#include "Widgets/SOverlay.h"

ASimCopterMainMenuGameMode::ASimCopterMainMenuGameMode()
{
	// The front end has nothing to possess; the menu is the whole level.
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

	OpenMainMenu();
}

void ASimCopterMainMenuGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseMainMenu();
	Super::EndPlay(EndPlayReason);
}

void ASimCopterMainMenuGameMode::OpenMainMenu()
{
	if (MainMenuWidget.IsValid() || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	USimCopterSessionSubsystem* Session = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<USimCopterSessionSubsystem>()
		: nullptr;

	MainMenuWidget =
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SSimCopterMainMenu)
			.Session(Session)
			.OnStartRequested(FSimpleDelegate::CreateUObject(this, &ASimCopterMainMenuGameMode::StartPendingSession))
			.OnQuitRequested(FSimpleDelegate::CreateUObject(this, &ASimCopterMainMenuGameMode::QuitGame))
		];

	GEngine->GameViewport->AddViewportWidgetContent(MainMenuWidget.ToSharedRef(), 100);

	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
	{
		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PlayerController->SetInputMode(InputMode);
		PlayerController->bShowMouseCursor = true;
	}
}

void ASimCopterMainMenuGameMode::CloseMainMenu()
{
	if (!MainMenuWidget.IsValid())
	{
		return;
	}

	if (GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MainMenuWidget.ToSharedRef());
	}
	MainMenuWidget.Reset();
}

void ASimCopterMainMenuGameMode::StartPendingSession()
{
	USimCopterSessionSubsystem* Session = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<USimCopterSessionSubsystem>()
		: nullptr;

	if (Session == nullptr || !Session->HasPendingSession())
	{
		UE_LOG(LogTemp, Warning, TEXT("SimCopter main menu: no session was requested, staying in the front end."));
		return;
	}

	UE_LOG(LogTemp, Display, TEXT("SimCopter main menu: starting %s session, city file '%s'"),
		Session->GetSessionKind() == ESimCopterSessionKind::Career ? TEXT("career") : TEXT("user"),
		*Session->GetCityFilePath());

	CloseMainMenu();
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
