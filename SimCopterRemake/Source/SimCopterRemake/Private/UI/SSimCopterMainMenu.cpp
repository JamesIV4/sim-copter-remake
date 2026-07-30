// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SSimCopterMainMenu.h"

#include "Framework/Application/SlateApplication.h"
#include "Game/SimCopterSessionSubsystem.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "UI/SimCopterMissionCatalog.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimCopterMainMenu"

using namespace SimCopterMissions;

namespace
{
const FLinearColor LabelColor(0.62f, 0.72f, 0.82f, 1.0f);
const FLinearColor ValueColor(0.94f, 0.97f, 1.0f, 1.0f);
const FLinearColor TitleColor(1.0f, 0.85f, 0.45f, 1.0f);
const FLinearColor DisabledColor(0.45f, 0.48f, 0.52f, 1.0f);
const FLinearColor StatusColor(1.0f, 0.72f, 0.55f, 1.0f);

FSlateFontInfo MenuFont(int32 Size, bool bBold = false)
{
	return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size);
}

// The five main-menu items are full-width rows, like the original's stacked bitmap buttons.
TSharedRef<SWidget> MakeMenuItem(const FText& Label, const FText& Tooltip, FOnClicked OnClicked, bool bEnabled = true)
{
	return SNew(SButton)
		.HAlign(HAlign_Center)
		.IsEnabled(bEnabled)
		.ToolTipText(Tooltip)
		.ContentPadding(FMargin(10.0f, 7.0f))
		.OnClicked(OnClicked)
		[
			SNew(STextBlock)
			.Text(Label)
			.ColorAndOpacity(bEnabled ? ValueColor : DisabledColor)
			.Font(MenuFont(14, true))
		];
}

TSharedRef<SWidget> MakeSmallButton(const FText& Label, FOnClicked OnClicked)
{
	return SNew(SButton)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.ContentPadding(FMargin(8.0f, 3.0f))
		.OnClicked(OnClicked)
		[
			SNew(STextBlock).Text(Label).Font(MenuFont(12, true))
		];
}

// MinDesiredWidth, not WidthOverride: a fixed width gets squeezed when a row runs out of room and
// the glyph is then clipped away entirely, leaving a blank button.
TSharedRef<SWidget> MakeArrow(const FText& Label, FOnClicked OnClicked)
{
	return SNew(SBox)
		.MinDesiredWidth(28.0f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ContentPadding(FMargin(7.0f, 1.0f))
			.OnClicked(OnClicked)
			[
				SNew(STextBlock).Text(Label).Font(MenuFont(12, true))
			]
		];
}

TSharedPtr<SWidget> FindFirstFocusableWidget(const TSharedRef<SWidget>& Root)
{
	if (Root->SupportsKeyboardFocus() && Root->IsEnabled())
	{
		return Root;
	}

	FChildren* Children = Root->GetChildren();
	for (int32 ChildIndex = 0; Children != nullptr && ChildIndex < Children->Num(); ++ChildIndex)
	{
		if (TSharedPtr<SWidget> Result = FindFirstFocusableWidget(Children->GetChildAt(ChildIndex)))
		{
			return Result;
		}
	}

	return nullptr;
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
}

constexpr int32 SSimCopterMainMenu::NewCareerCityChoices[3];

void SSimCopterMainMenu::Construct(const FArguments& InArgs)
{
	Session = InArgs._Session;
	OnStartRequested = InArgs._OnStartRequested;
	OnQuitRequested = InArgs._OnQuitRequested;

	bCareerDataLoaded = CareerData.LoadCareerData(ResolveCareerTweakPath());
	USimCopterSessionSubsystem::GetUserCityFilePaths(UserCityPaths);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(FLinearColor(0.02f, 0.03f, 0.05f, 0.94f))
		.Padding(FMargin(22.0f, 18.0f))
		[
			SNew(SBox)
			.WidthOverride(660.0f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Title", "SIMCOPTER"))
					.ColorAndOpacity(TitleColor)
					.Font(MenuFont(30, true))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 14.0f))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Subtitle", "Main Menu - placeholder styling"))
					.ColorAndOpacity(LabelColor)
					.Font(MenuFont(11))
				]

				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(Body, SVerticalBox)
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))
				[
					SNew(STextBlock)
					.Text(this, &SSimCopterMainMenu::GetStatusText)
					.ColorAndOpacity(StatusColor)
					.AutoWrapText(true)
					.Font(MenuFont(10))
				]
			]
		]
	];

	ShowPanel(EPanel::Root);
}

void SSimCopterMainMenu::ShowPanel(EPanel NewPanel)
{
	Panel = NewPanel;

	if (!Body.IsValid())
	{
		return;
	}

	Body->ClearChildren();

	TSharedRef<SWidget> PanelContent =
		Panel == EPanel::CareerCity ? BuildCareerCityPanel() :
		Panel == EPanel::UserCity ? BuildUserCityPanel() :
		BuildRootPanel();

	Body->AddSlot().AutoHeight()[PanelContent];
	InitialFocusWidget = FindFirstFocusableWidget(PanelContent);
	if (InitialFocusWidget.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocus(InitialFocusWidget, EFocusCause::Navigation);
	}
}

TSharedRef<SWidget> SSimCopterMainMenu::BuildRootPanel()
{
	const FText NoSaves = LOCTEXT("NoSaveSystem", "Saved games are not implemented yet.");

	return SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 5.0f))
		[
			MakeMenuItem(
				LOCTEXT("NewCareer", "NEW CAREER GAME"),
				LOCTEXT("NewCareerTip", "Begin a new career. You choose one of three cities."),
				FOnClicked::CreateSP(this, &SSimCopterMainMenu::HandleNewCareerGame))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 5.0f))
		[
			MakeMenuItem(
				LOCTEXT("OpenCareer", "OPEN CAREER GAME"),
				NoSaves,
				FOnClicked::CreateSP(this, &SSimCopterMainMenu::HandleUnavailable, NoSaves),
				/*bEnabled=*/false)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 5.0f))
		[
			MakeMenuItem(
				LOCTEXT("NewUser", "NEW USER GAME"),
				LOCTEXT("NewUserTip", "Play any SimCity 2000 city. The original opened a file dialog here."),
				FOnClicked::CreateSP(this, &SSimCopterMainMenu::HandleNewUserGame))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 5.0f))
		[
			MakeMenuItem(
				LOCTEXT("OpenUser", "OPEN USER GAME"),
				NoSaves,
				FOnClicked::CreateSP(this, &SSimCopterMainMenu::HandleUnavailable, NoSaves),
				/*bEnabled=*/false)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeMenuItem(
				LOCTEXT("Quit", "QUIT"),
				LOCTEXT("QuitTip", "Leave SimCopter."),
				FOnClicked::CreateSP(this, &SSimCopterMainMenu::HandleQuit))
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 16.0f, 0.0f, 8.0f))
		[
			SNew(SSeparator)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			BuildMissionOptions()
		];
}

TSharedRef<SWidget> SSimCopterMainMenu::BuildCareerCityPanel()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ChooseCareerCity", "NEW CAREER GAME - choose your first city"))
			.ColorAndOpacity(TitleColor)
			.Font(MenuFont(14, true))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				MakeArrow(LOCTEXT("Prev", "<"), FOnClicked::CreateSP(this, &SSimCopterMainMenu::HandleCareerPrev))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(FMargin(8.0f, 0.0f))
			[
				SNew(STextBlock)
				.Text(this, &SSimCopterMainMenu::GetCareerCityLineText)
				.ColorAndOpacity(ValueColor)
				.Font(MenuFont(14, true))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				MakeArrow(LOCTEXT("Next", ">"), FOnClicked::CreateSP(this, &SSimCopterMainMenu::HandleCareerNext))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(36.0f, 4.0f, 0.0f, 14.0f))
		[
			SNew(STextBlock)
			.Text(this, &SSimCopterMainMenu::GetCareerCityDetailText)
			.ColorAndOpacity(LabelColor)
			.AutoWrapText(true)
			.Font(MenuFont(10))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
			[
				MakeMenuItem(
					LOCTEXT("FlyThisCity", "FLY THIS CITY"),
					LOCTEXT("FlyThisCityTip", "Load this career city and start taking jobs."),
					FOnClicked::CreateSP(this, &SSimCopterMainMenu::HandleStartCareer))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				MakeSmallButton(LOCTEXT("Back", "BACK"), FOnClicked::CreateSP(this, &SSimCopterMainMenu::HandleBack))
			]
		];
}

TSharedRef<SWidget> SSimCopterMainMenu::BuildUserCityPanel()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ChooseUserCity", "NEW USER GAME - choose a SimCity 2000 city"))
			.ColorAndOpacity(TitleColor)
			.Font(MenuFont(14, true))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				MakeArrow(LOCTEXT("Prev", "<"), FOnClicked::CreateSP(this, &SSimCopterMainMenu::HandleUserPrev))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(FMargin(8.0f, 0.0f))
			[
				SNew(STextBlock)
				.Text(this, &SSimCopterMainMenu::GetUserCityLineText)
				.ColorAndOpacity(ValueColor)
				.Font(MenuFont(14, true))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				MakeArrow(LOCTEXT("Next", ">"), FOnClicked::CreateSP(this, &SSimCopterMainMenu::HandleUserNext))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(36.0f, 4.0f, 0.0f, 14.0f))
		[
			SNew(STextBlock)
			.Text(LOCTEXT("UserCityDetail", "User cities use career City0's tuning (difficulty 0, no crime or riots), the way FUN_004080c0 seeds a user game."))
			.ColorAndOpacity(LabelColor)
			.AutoWrapText(true)
			.Font(MenuFont(10))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
			[
				MakeMenuItem(
					LOCTEXT("FlyUserCity", "FLY THIS CITY"),
					LOCTEXT("FlyUserCityTip", "Load this city and start taking jobs."),
					FOnClicked::CreateSP(this, &SSimCopterMainMenu::HandleStartUserCity))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				MakeSmallButton(LOCTEXT("Back", "BACK"), FOnClicked::CreateSP(this, &SSimCopterMainMenu::HandleBack))
			]
		];
}

TSharedRef<SWidget> SSimCopterMainMenu::BuildMissionOptions()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ExtrasHeader", "DEVELOPMENT EXTRAS (not in the original menu)"))
			.ColorAndOpacity(LabelColor)
			.Font(MenuFont(10, true))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).MinDesiredWidth(104.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("StartWithMission", "Start with"))
					.ColorAndOpacity(LabelColor)
					.Font(MenuFont(10, true))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				MakeArrow(LOCTEXT("Prev", "<"), FOnClicked::CreateSP(this, &SSimCopterMainMenu::HandleMissionPrev))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(FMargin(8.0f, 0.0f))
			[
				SNew(STextBlock)
				.Text(this, &SSimCopterMainMenu::GetMissionLineText)
				.ColorAndOpacity(ValueColor)
				.Font(MenuFont(12, true))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				MakeArrow(LOCTEXT("Next", ">"), FOnClicked::CreateSP(this, &SSimCopterMainMenu::HandleMissionNext))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(104.0f, 2.0f, 0.0f, 8.0f))
		[
			SNew(STextBlock)
			.Text(this, &SSimCopterMainMenu::GetMissionDetailText)
			.ColorAndOpacity(LabelColor)
			.AutoWrapText(true)
			.Font(MenuFont(10))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SButton)
			.HAlign(HAlign_Left)
			.ContentPadding(FMargin(8.0f, 3.0f))
			.ToolTipText(LOCTEXT(
				"FirstMissionTip",
				"The original waits out a 180 second opening countdown before the first scheduled job (DAT_00505fb4)."))
			.OnClicked(FOnClicked::CreateSP(this, &SSimCopterMainMenu::HandleToggleFirstMission))
			[
				SNew(STextBlock)
				.Text(this, &SSimCopterMainMenu::GetFirstMissionToggleText)
				.ColorAndOpacity(ValueColor)
				.Font(MenuFont(11))
			]
		];
}

FText SSimCopterMainMenu::GetCareerCityLineText() const
{
	const int32 CityIndex = NewCareerCityChoices[FMath::Clamp(CareerChoice, 0, 2)];
	return FText::Format(
		LOCTEXT("CareerCityLine", "City{0}   (choice {1} of 3)   cities/career/city{0}.sc2"),
		FText::AsNumber(CityIndex),
		FText::AsNumber(CareerChoice + 1));
}

FText SSimCopterMainMenu::GetCareerCityDetailText() const
{
	const int32 CityIndex = NewCareerCityChoices[FMath::Clamp(CareerChoice, 0, 2)];
	const FSimCopterCareerCity* City = bCareerDataLoaded ? CareerData.GetCareerCityByIndex(CityIndex) : nullptr;
	if (City == nullptr)
	{
		return LOCTEXT("NoCareerData", "career.twk was not found, so this city will run with built-in defaults.");
	}

	return FText::Format(
		LOCTEXT(
			"CareerCityDetail",
			"Difficulty {0} (tier {1})   {2} points to finish   ${3} on completion   day/night flag {4}\nJob mix  Fire {5}  Crime {6}  Rescue {7}  Riot {8}  Traffic {9}  MedEvac {10}  Transport {11}"),
		FText::AsNumber(City->Difficulty),
		FText::AsNumber(City->Difficulty + 1),
		FText::AsNumber(City->PointsNeeded),
		FText::AsNumber(City->MoneyEarned),
		FText::AsNumber(City->DayOrNight),
		FText::AsNumber(static_cast<int32>(City->Weights[0])),
		FText::AsNumber(static_cast<int32>(City->Weights[1])),
		FText::AsNumber(static_cast<int32>(City->Weights[2])),
		FText::AsNumber(static_cast<int32>(City->Weights[3])),
		FText::AsNumber(static_cast<int32>(City->Weights[4])),
		FText::AsNumber(static_cast<int32>(City->Weights[5])),
		FText::AsNumber(static_cast<int32>(City->Weights[6])));
}

FText SSimCopterMainMenu::GetUserCityLineText() const
{
	if (UserCityPaths.Num() == 0)
	{
		return LOCTEXT("NoUserCities", "No .sc2 files found under the original game's cities folder");
	}

	const int32 Index = FMath::Clamp(UserCityChoice, 0, UserCityPaths.Num() - 1);
	return FText::Format(
		LOCTEXT("UserCityLine", "{0}   ({1} of {2})"),
		FText::FromString(FPaths::GetCleanFilename(UserCityPaths[Index])),
		FText::AsNumber(Index + 1),
		FText::AsNumber(UserCityPaths.Num()));
}

FText SSimCopterMainMenu::GetMissionLineText() const
{
	const TArrayView<const FSimCopterMissionCatalogEntry> Catalog = GetSimCopterMissionCatalog();
	if (!Catalog.IsValidIndex(MissionChoice))
	{
		return LOCTEXT("NoMission", "nothing - jobs arrive on the city's own schedule");
	}

	return FText::Format(
		LOCTEXT("MissionLine", "{0}   (mask 0x{1})   {2} of {3}"),
		FText::FromString(FSimCopterMissionSystem::GetTypeDisplayName(Catalog[MissionChoice].TypeMask)),
		FText::FromString(FString::Printf(TEXT("%x"), Catalog[MissionChoice].TypeMask)),
		FText::AsNumber(MissionChoice + 1),
		FText::AsNumber(Catalog.Num()));
}

FText SSimCopterMainMenu::GetMissionDetailText() const
{
	const TArrayView<const FSimCopterMissionCatalogEntry> Catalog = GetSimCopterMissionCatalog();
	if (!Catalog.IsValidIndex(MissionChoice))
	{
		return FText::GetEmpty();
	}

	const FSimCopterMissionCatalogEntry& Entry = Catalog[MissionChoice];
	const FText Description = FText::Format(
		LOCTEXT("MissionDetail", "{0} bucket - {1}"),
		FText::FromString(Entry.Bucket),
		FText::FromString(Entry.Note));

	if (Entry.bWorldHookPorted)
	{
		return Description;
	}

	return FText::Format(
		LOCTEXT("MissionDetailUnported", "{0}\nThis type's world hook is still a stub, so it will report a placement failure."),
		Description);
}

FText SSimCopterMainMenu::GetFirstMissionToggleText() const
{
	return bStartFirstMissionImmediately
		? LOCTEXT("FirstMissionOn", "[x] first scheduled job arrives immediately")
		: LOCTEXT("FirstMissionOff", "[ ] first scheduled job arrives immediately");
}

FReply SSimCopterMainMenu::HandleNewCareerGame()
{
	StatusText = FText::GetEmpty();
	ShowPanel(EPanel::CareerCity);
	return FReply::Handled();
}

FReply SSimCopterMainMenu::HandleNewUserGame()
{
	StatusText = FText::GetEmpty();
	ShowPanel(EPanel::UserCity);
	return FReply::Handled();
}

FReply SSimCopterMainMenu::HandleUnavailable(FText Reason)
{
	StatusText = Reason;
	return FReply::Handled();
}

FReply SSimCopterMainMenu::HandleQuit()
{
	OnQuitRequested.ExecuteIfBound();
	return FReply::Handled();
}

FReply SSimCopterMainMenu::HandleBack()
{
	StatusText = FText::GetEmpty();
	ShowPanel(EPanel::Root);
	return FReply::Handled();
}

FReply SSimCopterMainMenu::HandleCareerPrev()
{
	CareerChoice = (CareerChoice + 2) % 3;
	return FReply::Handled();
}

FReply SSimCopterMainMenu::HandleCareerNext()
{
	CareerChoice = (CareerChoice + 1) % 3;
	return FReply::Handled();
}

FReply SSimCopterMainMenu::HandleUserPrev()
{
	if (UserCityPaths.Num() > 0)
	{
		UserCityChoice = (UserCityChoice + UserCityPaths.Num() - 1) % UserCityPaths.Num();
	}
	return FReply::Handled();
}

FReply SSimCopterMainMenu::HandleUserNext()
{
	if (UserCityPaths.Num() > 0)
	{
		UserCityChoice = (UserCityChoice + 1) % UserCityPaths.Num();
	}
	return FReply::Handled();
}

void SSimCopterMainMenu::StepMission(int32 Delta)
{
	// INDEX_NONE ("nothing") is one step below the first entry, so the list wraps through it.
	const int32 Count = GetSimCopterMissionCatalog().Num();
	const int32 Slots = Count + 1;
	const int32 Current = MissionChoice + 1;
	MissionChoice = ((Current + Delta % Slots + Slots) % Slots) - 1;
}

FReply SSimCopterMainMenu::HandleMissionPrev()
{
	StepMission(-1);
	return FReply::Handled();
}

FReply SSimCopterMainMenu::HandleMissionNext()
{
	StepMission(1);
	return FReply::Handled();
}

FReply SSimCopterMainMenu::HandleToggleFirstMission()
{
	bStartFirstMissionImmediately = !bStartFirstMissionImmediately;
	return FReply::Handled();
}

void SSimCopterMainMenu::ApplyMissionOptionsToSession()
{
	USimCopterSessionSubsystem* SessionPtr = Session.Get();
	if (SessionPtr == nullptr)
	{
		return;
	}

	const TArrayView<const FSimCopterMissionCatalogEntry> Catalog = GetSimCopterMissionCatalog();
	SessionPtr->SetPendingMissionTypeMask(Catalog.IsValidIndex(MissionChoice) ? Catalog[MissionChoice].TypeMask : 0);
	SessionPtr->SetStartFirstMissionImmediately(bStartFirstMissionImmediately);
}

FReply SSimCopterMainMenu::HandleStartCareer()
{
	USimCopterSessionSubsystem* SessionPtr = Session.Get();
	if (SessionPtr == nullptr)
	{
		StatusText = LOCTEXT("NoSession", "The session subsystem is missing, so the city cannot be started.");
		return FReply::Handled();
	}

	const int32 CityIndex = NewCareerCityChoices[FMath::Clamp(CareerChoice, 0, 2)];
	SessionPtr->RequestCareerCity(CityIndex);
	ApplyMissionOptionsToSession();

	if (SessionPtr->GetCityFilePath().IsEmpty() || !FPaths::FileExists(SessionPtr->GetCityFilePath()))
	{
		StatusText = FText::Format(
			LOCTEXT("MissingCareerCity", "Cannot find {0}. The original game folder has to be in place under Reference/SimCopterOriginalGame."),
			FText::FromString(USimCopterSessionSubsystem::ResolveCareerCityFilePath(CityIndex)));
		SessionPtr->ClearPendingSession();
		return FReply::Handled();
	}

	OnStartRequested.ExecuteIfBound();
	return FReply::Handled();
}

FReply SSimCopterMainMenu::HandleStartUserCity()
{
	USimCopterSessionSubsystem* SessionPtr = Session.Get();
	if (SessionPtr == nullptr || UserCityPaths.Num() == 0)
	{
		StatusText = LOCTEXT("NoUserCityToStart", "No city file to load. Put the original game folder under Reference/SimCopterOriginalGame.");
		return FReply::Handled();
	}

	SessionPtr->RequestUserCity(UserCityPaths[FMath::Clamp(UserCityChoice, 0, UserCityPaths.Num() - 1)]);
	ApplyMissionOptionsToSession();

	OnStartRequested.ExecuteIfBound();
	return FReply::Handled();
}

FReply SSimCopterMainMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Gamepad_FaceButton_Right && Panel != EPanel::Root)
	{
		return HandleBack();
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE
