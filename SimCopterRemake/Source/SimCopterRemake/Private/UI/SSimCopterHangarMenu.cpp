// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSimCopterHangarMenu.h"

#include "Flight/SimCopterHelicopterPawn.h"
#include "Flight/SimCopterHelicopterRegistry.h"
#include "Game/SimCopterCareerSubsystem.h"
#include "Math/TransformCalculus2D.h"
#include "Missions/SimCopterMissionSystemActor.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
using namespace SimCopterHangarLayout;

// Ink on the notebook pages, and the label colour on the hangar backdrop.
const FLinearColor PaperInk(0.06f, 0.06f, 0.08f, 1.0f);
const FLinearColor PaperInkDim(0.24f, 0.24f, 0.28f, 1.0f);
const FLinearColor ShellLabel(0.94f, 0.94f, 0.90f, 1.0f);
const FLinearColor SelectionTint(0.10f, 0.28f, 0.85f, 1.0f);

// The original's button strips: button.bmp is three 100x28 frames, cat_btn.bmp three 86x28.
const TCHAR* const UpscaledHangarBackdrop = TEXT("DHANGAR-upscaled.png");
const TCHAR* const ShellButtonStrip = TEXT("BUTTON.BMP");
const TCHAR* const CatalogButtonStrip = TEXT("CAT_BTN.BMP");
constexpr int32 ButtonFrameCount = 3;

// The original prints these readouts as plain integers - no thousands separator.
FString FormatDollars(const int32 Value)
{
	return FString::FromInt(Value);
}

// "12:34" of session time. The original stamps its log with the in-game date (strings 500..521);
// nothing in the remake keeps a calendar, so elapsed time stands in.
FString FormatLogStamp(const float Seconds)
{
	const int32 Whole = FMath::Max(0, FMath::FloorToInt(Seconds));
	return FString::Printf(TEXT("%02d:%02d"), Whole / 60, Whole % 60);
}
}

void SSimCopterHangarMenu::Construct(const FArguments& InArgs)
{
	Art = InArgs._Art;
	Shop = InArgs._Shop;
	OnDoneRequested = InArgs._OnDoneRequested;

	// Open on the row the player is already flying, the way FUN_0042d420 does.
	if (const ASimCopterHelicopterPawn* Helicopter = Shop.Helicopter.Get())
	{
		const int32 Row = SimCopterHangarLayout::GetCatalogRowForTypeIndex(Helicopter->GetHelicopterTypeIndex());
		CatalogRow = Row != INDEX_NONE ? Row : 0;
	}

	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);
	PageCanvas = Canvas;

	TSharedRef<SOverlay> BackdropOverlay = SNew(SOverlay);
	Backdrop = BackdropOverlay;

	ChildSlot
	[
		SNew(SOverlay)
		// A black surround so the page letterboxes cleanly on any aspect ratio.
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 1.0f))
			[
				BackdropOverlay
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SScaleBox)
			.Stretch(EStretch::ScaleToFit)
			[
				SNew(SBox)
				.WidthOverride(USimCopterHangarArt::PageWidth)
				.HeightOverride(USimCopterHangarArt::PageHeight)
				[
					Canvas
				]
			]
		]
	];

	RebuildPage();
}

void SSimCopterHangarMenu::ShowPage(const EPage NewPage)
{
	Page = NewPage;
	StatusText.Reset();
	RebuildPage();
}

void SSimCopterHangarMenu::RebuildPage()
{
	if (!PageCanvas.IsValid() || !Backdrop.IsValid())
	{
		return;
	}

	PageCanvas->ClearChildren();
	Backdrop->ClearChildren();
	ButtonStyles.Reset();

	switch (Page)
	{
	case EPage::Hangar:
		BuildHangarPage(*PageCanvas);
		break;
	case EPage::Catalog:
		BuildCatalogPage(*PageCanvas);
		break;
	case EPage::MissionLog:
		BuildMissionLogPage(*PageCanvas);
		break;
	case EPage::Inventory:
		BuildInventoryPage(*PageCanvas);
		break;
	}

	if (!StatusText.IsEmpty())
	{
		AddAt(*PageCanvas, 20.0f, 2.0f, 600.0f, 16.0f,
			MakePageText(StatusText, 11, FLinearColor(1.0f, 0.92f, 0.55f, 1.0f), ETextJustify::Center, /*bBold=*/true, /*bWrap=*/false));
	}
}

void SSimCopterHangarMenu::AddAt(
	SConstraintCanvas& Canvas,
	const float X,
	const float Y,
	const float Width,
	const float Height,
	TSharedRef<SWidget> Content)
{
	Canvas.AddSlot()
	.Offset(FMargin(X, Y, Width, Height))
	.Alignment(FVector2D::ZeroVector)
	[
		Content
	];
}

void SSimCopterHangarMenu::AddPageBackground(SConstraintCanvas& Canvas, const TCHAR* FileName)
{
	USimCopterHangarArt* ArtObject = Art.Get();
	const FSlateBrush* Brush = ArtObject != nullptr ? ArtObject->GetBitmap(FileName) : nullptr;
	if (Brush != nullptr)
	{
		AddAt(Canvas, 0.0f, 0.0f, USimCopterHangarArt::PageWidth, USimCopterHangarArt::PageHeight,
			SNew(SImage).Image(Brush));
		return;
	}

	// No original artwork installed: a plain paper-coloured page keeps every page readable.
	AddAt(Canvas, 0.0f, 0.0f, USimCopterHangarArt::PageWidth, USimCopterHangarArt::PageHeight,
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(FLinearColor(0.88f, 0.86f, 0.80f, 1.0f)));
}

TSharedRef<SWidget> SSimCopterHangarMenu::MakeArtButton(
	const TCHAR* StripFileName,
	const int32 FrameCount,
	const FString& Label,
	FOnClicked OnClicked,
	const bool bEnabled,
	const int32 FontSize)
{
	USimCopterHangarArt* ArtObject = Art.Get();
	const FSlateBrush* Normal = ArtObject != nullptr ? ArtObject->GetStripFrame(StripFileName, 0, FrameCount) : nullptr;
	const FSlateBrush* Pressed = ArtObject != nullptr ? ArtObject->GetStripFrame(StripFileName, 1, FrameCount) : nullptr;
	const FSlateBrush* Disabled = ArtObject != nullptr ? ArtObject->GetStripFrame(StripFileName, 2, FrameCount) : nullptr;

	TSharedRef<SButton> Button = SNew(SButton)
		// A shell button must never hold keyboard focus; the space bar belongs to the game.
		.IsFocusable(false)
		.IsEnabled(bEnabled)
		.ContentPadding(FMargin(0.0f))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.OnClicked(OnClicked)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Label))
			.Justification(ETextJustify::Center)
			.ColorAndOpacity(bEnabled ? ShellLabel : FLinearColor(0.55f, 0.55f, 0.55f, 1.0f))
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), FontSize))
		];

	if (Normal != nullptr)
	{
		TSharedRef<FButtonStyle> Style = MakeShared<FButtonStyle>();
		Style->SetNormal(*Normal);
		Style->SetHovered(*Normal);
		Style->SetPressed(Pressed != nullptr ? *Pressed : *Normal);
		Style->SetDisabled(Disabled != nullptr ? *Disabled : *Normal);
		Style->SetNormalPadding(FMargin(0.0f));
		Style->SetPressedPadding(FMargin(0.0f));
		ButtonStyles.Add(Style);
		Button->SetButtonStyle(&Style.Get());
	}

	return Button;
}

TSharedRef<SWidget> SSimCopterHangarMenu::MakeHotspot(FOnClicked OnClicked, const FString& ToolTip)
{
	return SNew(SButton)
		.IsFocusable(false)
		.ButtonColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.0f))
		.ContentPadding(FMargin(0.0f))
		.ToolTipText(FText::FromString(ToolTip))
		.OnClicked(OnClicked);
}

TSharedRef<SWidget> SSimCopterHangarMenu::MakePageText(
	const FString& Text,
	const int32 FontSize,
	const FLinearColor& Color,
	const ETextJustify::Type Justify,
	const bool bBold,
	const bool bWrap,
	const float WrapWidth)
{
	TSharedRef<STextBlock> Block = SNew(STextBlock)
		.Text(FText::FromString(Text))
		.Justification(Justify)
		.ColorAndOpacity(Color)
		.Font(FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), FontSize));

	if (bWrap && WrapWidth > 0.0f)
	{
		Block->SetWrapTextAt(WrapWidth);
	}
	else if (bWrap)
	{
		Block->SetAutoWrapText(true);
	}

	return Block;
}

// --- hangar page -----------------------------------------------------------------------------

void SSimCopterHangarMenu::BuildHangarPage(SConstraintCanvas& Canvas)
{
	// The high-resolution reconstruction fills the same viewport backdrop slot as dhangar.bmp.
	// Keep the original available as a fallback for a missing or unreadable bundled image.
	USimCopterHangarArt* ArtObject = Art.Get();
	const FSlateBrush* Backdrop3D =
		ArtObject != nullptr ? ArtObject->GetBundledSlateImage(UpscaledHangarBackdrop) : nullptr;
	if (Backdrop3D == nullptr && ArtObject != nullptr)
	{
		Backdrop3D = ArtObject->GetBitmap(TEXT("DHANGAR.BMP"));
	}
	if (Backdrop3D != nullptr && Backdrop.IsValid())
	{
		Backdrop->AddSlot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SScaleBox)
			.Stretch(EStretch::ScaleToFill)
			[
				SNew(SImage).Image(Backdrop3D)
			]
		];
	}

	// Strings 125..128.
	const TCHAR* const Labels[4] = { TEXT("Catalog"), TEXT("Mission Log"), TEXT("Inventory"), TEXT("Done") };
	const EPage Targets[3] = { EPage::Catalog, EPage::MissionLog, EPage::Inventory };

	for (int32 Index = 0; Index < 4; ++Index)
	{
		FOnClicked Clicked = Index < 3
			? FOnClicked::CreateSP(this, &SSimCopterHangarMenu::HandleShowPage, Targets[Index])
			: FOnClicked::CreateSP(this, &SSimCopterHangarMenu::HandleDone);

		AddAt(Canvas, HangarButtonX[Index], HangarButtonY, ShellButtonWidth, ShellButtonHeight,
			MakeArtButton(ShellButtonStrip, ButtonFrameCount, Labels[Index], Clicked));
	}
}

// --- catalog ---------------------------------------------------------------------------------

void SSimCopterHangarMenu::BuildCatalogPage(SConstraintCanvas& Canvas)
{
	const bool bUpgrades = CatalogRow < 0 || CatalogRow >= CatalogTabCount;
	AddPageBackground(Canvas, bUpgrades ? TEXT("CATALOGE.BMP") : TEXT("CATALOG.BMP"));

	if (bUpgrades)
	{
		BuildCatalogUpgradesPage(Canvas);
	}
	else
	{
		BuildCatalogHelicopterPage(Canvas);
	}

	BuildCatalogTabs(Canvas);
}

void SSimCopterHangarMenu::BuildCatalogTabs(SConstraintCanvas& Canvas)
{
	USimCopterHangarArt* ArtObject = Art.Get();
	if (const FSlateBrush* Strip = ArtObject != nullptr ? ArtObject->GetCatalogTabStrip(CatalogRow) : nullptr)
	{
		AddAt(Canvas, CatalogTabStripX, CatalogTabStripY, CatalogTabStripWidth, CatalogTabStripHeight,
			SNew(SImage).Image(Strip));
	}

	for (int32 Tab = 0; Tab < CatalogTabCount; ++Tab)
	{
		const int32 TypeIndex = SimCopterHangarLayout::GetTypeIndexForCatalogRow(Tab);
		AddAt(Canvas, CatalogTabLeft[Tab], CatalogTabStripY, CatalogTabRight[Tab] - CatalogTabLeft[Tab], CatalogTabHitHeight,
			MakeHotspot(
				FOnClicked::CreateSP(this, &SSimCopterHangarMenu::HandleSelectCatalogRow, Tab),
				SimCopterHangarShop::GetModelDisplayName(TypeIndex)));
	}

	// The "Upgrades" tab (string 435) is printed under the model tabs on both catalog pages.
	const bool bUpgrades = CatalogRow < 0 || CatalogRow >= CatalogTabCount;
	AddAt(Canvas, 140.0f, 441.0f, 160.0f, 17.0f,
		MakePageText(TEXT("Upgrades"), 11, bUpgrades ? SelectionTint : PaperInk, ETextJustify::Left, /*bBold=*/true, /*bWrap=*/false));
	AddAt(Canvas, 134.0f, 439.0f, 174.0f, 20.0f,
		MakeHotspot(FOnClicked::CreateSP(this, &SSimCopterHangarMenu::HandleSelectCatalogRow, int32(INDEX_NONE)), TEXT("Upgrades")));
}

void SSimCopterHangarMenu::BuildCatalogHelicopterPage(SConstraintCanvas& Canvas)
{
	USimCopterHangarArt* ArtObject = Art.Get();
	const int32 TypeIndex = SimCopterHangarLayout::GetTypeIndexForCatalogRow(CatalogRow);

	if (const FSlateBrush* Drawing = ArtObject != nullptr ? ArtObject->GetCatalogDrawing(CatalogRow) : nullptr)
	{
		AddAt(Canvas, CatalogDrawingX, CatalogDrawingY, CatalogDrawingWidth, CatalogDrawingHeight,
			SNew(SImage).Image(Drawing));
	}
	else
	{
		// Without the blueprint the page still has to say which model it is showing.
		AddAt(Canvas, CatalogDrawingX + 12.0f, CatalogDrawingY + 12.0f, CatalogDrawingWidth - 24.0f, 40.0f,
			MakePageText(SimCopterHangarShop::GetModelDisplayName(TypeIndex), 22, PaperInk, ETextJustify::Left, true, false));
	}

	// Left panel: History (430) then Specialties (431).
	AddAt(Canvas, CatalogHistoryX + 6.0f, CatalogHistoryY + 3.0f, CatalogHistoryWidth - 12.0f, 16.0f,
		MakePageText(TEXT("History"), 11, PaperInk, ETextJustify::Left, true, false));
	AddAt(Canvas, CatalogHistoryX + 10.0f, CatalogHistoryY + 18.0f, CatalogHistoryWidth - 16.0f, 32.0f,
		MakePageText(SimCopterHangarShop::GetCatalogHistory(CatalogRow), 9, PaperInkDim, ETextJustify::Left, false, true, CatalogHistoryWidth - 16.0f));
	AddAt(Canvas, CatalogHistoryX + 6.0f, CatalogHistoryY + 54.0f, CatalogHistoryWidth - 12.0f, 16.0f,
		MakePageText(TEXT("Specialties"), 11, PaperInk, ETextJustify::Left, true, false));
	AddAt(Canvas, CatalogHistoryX + 10.0f, CatalogHistoryY + 69.0f, CatalogHistoryWidth - 16.0f, 32.0f,
		MakePageText(SimCopterHangarShop::GetCatalogSpecialties(CatalogRow), 9, PaperInkDim, ETextJustify::Left, false, true, CatalogHistoryWidth - 16.0f));

	// Right panel: Description (432).
	AddAt(Canvas, CatalogDescriptionX + 6.0f, CatalogDescriptionY + 3.0f, CatalogDescriptionWidth - 12.0f, 16.0f,
		MakePageText(TEXT("Description"), 11, PaperInk, ETextJustify::Left, true, false));
	AddAt(Canvas, CatalogDescriptionX + 10.0f, CatalogDescriptionY + 18.0f, CatalogDescriptionWidth - 16.0f, 86.0f,
		MakePageText(SimCopterHangarShop::GetCatalogDescription(CatalogRow), 9, PaperInkDim, ETextJustify::Left, false, true, CatalogDescriptionWidth - 16.0f));

	// Funds and value readouts (strings 433 / 434).
	const SimCopterHangarShop::FRowState State = SimCopterHangarShop::GetHelicopterRowState(Shop, CatalogRow);
	AddAt(Canvas, CatalogPanelX, CatalogFundsLabelY, CatalogPanelWidth, 32.0f,
		MakePageText(TEXT("Current\nFunds"), 11, PaperInk, ETextJustify::Center, true, false));
	AddAt(Canvas, CatalogPanelX, CatalogFundsValueY, CatalogPanelWidth, 20.0f,
		MakePageText(FormatDollars(SimCopterHangarShop::GetCurrentFunds(Shop)), 14, PaperInk, ETextJustify::Center, true, false));
	AddAt(Canvas, CatalogPanelX, CatalogValueLabelY, CatalogPanelWidth, 32.0f,
		MakePageText(TEXT("Item\nValue"), 11, PaperInk, ETextJustify::Center, true, false));
	AddAt(Canvas, CatalogPanelX, CatalogValueValueY, CatalogPanelWidth, 20.0f,
		MakePageText(FormatDollars(State.ItemValue), 14, PaperInk, ETextJustify::Center, true, false));

	// Buy (442) / Sell (443) / Done (444).
	AddAt(Canvas, CatalogPanelX, CatalogButtonY[0], CatalogButtonWidth, CatalogButtonHeight,
		MakeArtButton(CatalogButtonStrip, ButtonFrameCount, TEXT("Buy"),
			FOnClicked::CreateSP(this, &SSimCopterHangarMenu::HandleBuy), State.bCanBuy));
	AddAt(Canvas, CatalogPanelX, CatalogButtonY[1], CatalogButtonWidth, CatalogButtonHeight,
		MakeArtButton(CatalogButtonStrip, ButtonFrameCount, TEXT("Sell"),
			FOnClicked::CreateSP(this, &SSimCopterHangarMenu::HandleSell), State.bCanSell));
	AddAt(Canvas, CatalogPanelX, CatalogButtonY[2], CatalogButtonWidth, CatalogButtonHeight,
		MakeArtButton(CatalogButtonStrip, ButtonFrameCount, TEXT("Done"),
			FOnClicked::CreateSP(this, &SSimCopterHangarMenu::HandleShowPage, EPage::Hangar)));

	if (State.bOwned)
	{
		AddAt(Canvas, CatalogPanelX - 4.0f, CatalogValueValueY + 20.0f, CatalogPanelWidth + 8.0f, 14.0f,
			MakePageText(TEXT("On the books"), 9, SelectionTint, ETextJustify::Center, true, false));
	}
}

void SSimCopterHangarMenu::BuildCatalogUpgradesPage(SConstraintCanvas& Canvas)
{
	// The page's three letterheads (strings 439..441) are part of cataloge.bmp itself, so nothing
	// is drawn for them here - printing them again just doubles the ink.

	for (int32 Row = 0; Row < SimCopterHangarShop::UpgradeRowCount; ++Row)
	{
		const SimCopterHangarShop::FRowState State = SimCopterHangarShop::GetUpgradeRowState(Shop, Row);

		// The icon is printed on the page; the selected row gets the original's blue outline.
		if (Row == UpgradeRow)
		{
			AddAt(Canvas, UpgradeIconLeft[Row] - 2.0f, UpgradeIconTop[Row] - 2.0f, UpgradeIconWidth + 4.0f, UpgradeIconHeight + 4.0f,
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush(TEXT("Border")))
				.BorderBackgroundColor(SelectionTint));
		}

		// Font 8 with a 4px margin is the largest that fits every one of the five blurbs inside a
		// 167x96 cell without the last line running off the bottom.
		AddAt(Canvas, UpgradeTextLeft[Row] + 4.0f, UpgradeTextTop[Row] + 4.0f, UpgradeTextWidth - 8.0f, UpgradeTextHeight - 6.0f,
			MakePageText(
				SimCopterHangarShop::GetUpgradeDescription(Row),
				8,
				State.bOwned ? PaperInk : PaperInkDim,
				ETextJustify::Left,
				false,
				true,
				UpgradeTextWidth - 8.0f));

		if (State.bOwned)
		{
			AddAt(Canvas, UpgradeIconLeft[Row], UpgradeIconTop[Row] + UpgradeIconHeight - 14.0f, UpgradeIconWidth, 13.0f,
				MakePageText(TEXT("OWNED"), 8, SelectionTint, ETextJustify::Center, true, false));
		}

		// Both cells select the row, as the original's whole-cell hit test does.
		AddAt(Canvas, UpgradeIconLeft[Row], UpgradeIconTop[Row], UpgradeIconWidth, UpgradeIconHeight,
			MakeHotspot(
				FOnClicked::CreateSP(this, &SSimCopterHangarMenu::HandleSelectUpgradeRow, Row),
				SimCopterHelicopterRegistry::GetToolDisplayName(SimCopterHangarShop::GetToolForUpgradeRow(Row))));
		AddAt(Canvas, UpgradeTextLeft[Row], UpgradeTextTop[Row], UpgradeTextWidth, UpgradeTextHeight,
			MakeHotspot(
				FOnClicked::CreateSP(this, &SSimCopterHangarMenu::HandleSelectUpgradeRow, Row),
				SimCopterHelicopterRegistry::GetToolDisplayName(SimCopterHangarShop::GetToolForUpgradeRow(Row))));
	}

	// Readouts (strings 437 / 438) in the bottom-right cell.
	const SimCopterHangarShop::FRowState Selected = SimCopterHangarShop::GetUpgradeRowState(Shop, UpgradeRow);
	AddAt(Canvas, UpgradeFundsLabelX, UpgradeFundsRowY, UpgradeLabelWidth, 18.0f,
		MakePageText(TEXT("Current Funds"), 12, PaperInk, ETextJustify::Left, true, false));
	AddAt(Canvas, UpgradeFundsValueX, UpgradeFundsRowY, UpgradeValueWidth, 18.0f,
		MakePageText(FormatDollars(SimCopterHangarShop::GetCurrentFunds(Shop)), 12, PaperInk, ETextJustify::Left, true, false));
	AddAt(Canvas, UpgradeFundsLabelX, UpgradeValueRowY, UpgradeLabelWidth, 18.0f,
		MakePageText(TEXT("Item Value"), 12, PaperInk, ETextJustify::Left, true, false));
	AddAt(Canvas, UpgradeFundsValueX, UpgradeValueRowY, UpgradeValueWidth, 18.0f,
		MakePageText(FormatDollars(Selected.ItemValue), 12, PaperInk, ETextJustify::Left, true, false));

	AddAt(Canvas, UpgradeBuyX, UpgradeBuyY, ShellButtonWidth, ShellButtonHeight,
		MakeArtButton(ShellButtonStrip, ButtonFrameCount, TEXT("Buy"),
			FOnClicked::CreateSP(this, &SSimCopterHangarMenu::HandleBuy), Selected.bCanBuy));
	AddAt(Canvas, UpgradeBuyX, UpgradeSellY, ShellButtonWidth, ShellButtonHeight,
		MakeArtButton(ShellButtonStrip, ButtonFrameCount, TEXT("Sell"),
			FOnClicked::CreateSP(this, &SSimCopterHangarMenu::HandleSell), Selected.bCanSell));
	AddAt(Canvas, UpgradeDoneX, UpgradeDoneY, ShellButtonWidth, ShellButtonHeight,
		MakeArtButton(ShellButtonStrip, ButtonFrameCount, TEXT("Done"),
			FOnClicked::CreateSP(this, &SSimCopterHangarMenu::HandleShowPage, EPage::Hangar)));
}

// --- mission log -----------------------------------------------------------------------------

void SSimCopterHangarMenu::BuildMissionLogPage(SConstraintCanvas& Canvas)
{
	AddPageBackground(Canvas, TEXT("MSSNLOG.BMP"));

	const USimCopterCareerSubsystem* Career = Shop.Career.Get();
	TArray<FSimCopterCareerLogEntry> Entries;
	if (Career != nullptr)
	{
		Entries = Career->GetLogEntries();
	}

	if (LogSort == ELogSort::ByType)
	{
		// String 531: group the lines by the mission they belong to, newest first inside a group.
		Entries.StableSort([](const FSimCopterCareerLogEntry& Left, const FSimCopterCareerLogEntry& Right)
		{
			if (Left.TypeMask != Right.TypeMask)
			{
				return Left.TypeMask < Right.TypeMask;
			}
			return Left.SessionSeconds < Right.SessionSeconds;
		});
	}

	AddAt(Canvas, LogHeaderX + 8.0f, LogHeaderY + 7.0f, LogPageWidth - 16.0f, 16.0f,
		MakePageText(
			FString::Printf(TEXT("Mission Log        %d entries        %s"),
				Entries.Num(),
				LogSort == ELogSort::ByTime ? TEXT("By Time") : TEXT("By Type")),
			11,
			PaperInk,
			ETextJustify::Left,
			true,
			false));

	// The page holds this many ruled lines; the newest entries win when the log is longer.
	const int32 VisibleLines = FMath::FloorToInt(LogPageHeight / LogLineHeight);
	const int32 First = FMath::Max(0, Entries.Num() - VisibleLines);

	for (int32 Index = First; Index < Entries.Num(); ++Index)
	{
		const FSimCopterCareerLogEntry& Entry = Entries[Index];
		const float LineY = LogPageY + 4.0f + (Index - First) * LogLineHeight;

		AddAt(Canvas, LogPageX + 8.0f, LineY, 40.0f, LogLineHeight,
			MakePageText(FormatLogStamp(Entry.SessionSeconds), 9, PaperInkDim, ETextJustify::Left, false, false));
		AddAt(Canvas, LogPageX + 52.0f, LineY, LogPageWidth - 60.0f, LogLineHeight,
			MakePageText(Entry.Text, 9, PaperInk, ETextJustify::Left, false, false));
	}

	if (Entries.Num() == 0)
	{
		AddAt(Canvas, LogPageX + 8.0f, LogPageY + 8.0f, LogPageWidth - 16.0f, 16.0f,
			MakePageText(TEXT("Nothing logged yet."), 10, PaperInkDim, ETextJustify::Left, false, false));
	}

	// Strings 530 / 531 / 532.
	AddAt(Canvas, LogByTimeX, LogButtonY, ShellButtonWidth, ShellButtonHeight,
		MakeArtButton(ShellButtonStrip, ButtonFrameCount, TEXT("By Time"),
			FOnClicked::CreateSP(this, &SSimCopterHangarMenu::HandleSetLogSort, ELogSort::ByTime), LogSort != ELogSort::ByTime));
	AddAt(Canvas, LogByTypeX, LogButtonY, ShellButtonWidth, ShellButtonHeight,
		MakeArtButton(ShellButtonStrip, ButtonFrameCount, TEXT("By Type"),
			FOnClicked::CreateSP(this, &SSimCopterHangarMenu::HandleSetLogSort, ELogSort::ByType), LogSort != ELogSort::ByType));
	AddAt(Canvas, LogDoneX, LogButtonY, ShellButtonWidth, ShellButtonHeight,
		MakeArtButton(ShellButtonStrip, ButtonFrameCount, TEXT("Done"),
			FOnClicked::CreateSP(this, &SSimCopterHangarMenu::HandleShowPage, EPage::Hangar)));
}

// --- inventory -------------------------------------------------------------------------------

void SSimCopterHangarMenu::BuildInventoryPage(SConstraintCanvas& Canvas)
{
	AddPageBackground(Canvas, TEXT("INVNTORY.BMP"));

	// Letterheads (strings 421 / 420 / 422). Unlike the upgrades page these are not printed on the
	// artwork - the clipboard's header band is blank paper.
	AddAt(Canvas, InventoryHeaderLeftX, InventoryHeaderY, InventoryHeaderWidth, InventoryHeaderHeight,
		MakePageText(SimCopterHangarShop::GetInventoryHeaderLeft(), 6, PaperInk, ETextJustify::Left, false, true, InventoryHeaderWidth));
	AddAt(Canvas, InventoryHeaderCentreX, InventoryHeaderY + 4.0f, InventoryHeaderWidth, 26.0f,
		MakePageText(SimCopterHangarShop::GetInventoryHeaderCentre(), 8, PaperInk, ETextJustify::Center, true, false));
	AddAt(Canvas, InventoryHeaderRightX, InventoryHeaderY, InventoryHeaderWidth, InventoryHeaderHeight,
		MakePageText(SimCopterHangarShop::GetInventoryHeaderRight(), 6, PaperInk, ETextJustify::Left, false, true, InventoryHeaderWidth));

	// The five column labels are printed along the clipboard's diagonals, so they are rotated to
	// match (strings 410..414).
	for (int32 Column = 0; Column < InventoryColumnCount; ++Column)
	{
		TSharedRef<SWidget> Label = MakePageText(
			SimCopterHangarShop::GetInventoryColumnName(Column),
			8,
			PaperInk,
			ETextJustify::Left,
			false,
			false);
		Label->SetRenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(-45.0f))));
		Label->SetRenderTransformPivot(FVector2D(0.0f, 0.5f));

		AddAt(Canvas, InventoryColumnLeft[Column] + 4.0f, 232.0f, 120.0f, 12.0f, Label);
	}

	USimCopterHangarArt* ArtObject = Art.Get();
	const FSlateBrush* Tick = ArtObject != nullptr ? ArtObject->GetBitmap(TEXT("INVNCHK.BMP"), /*bColorKeyed=*/true) : nullptr;

	const USimCopterCareerSubsystem* Career = Shop.Career.Get();
	const ASimCopterHelicopterPawn* Helicopter = Shop.Helicopter.Get();
	const int32 EquipmentMask = Helicopter != nullptr ? Helicopter->GetEquipmentState().CareerEquipmentMask : 0;

	int32 Row = 0;
	for (const FSimCopterHelicopterDefinition& Definition : SimCopterHelicopterRegistry::GetDefinitions())
	{
		if (Row >= InventoryRowCount)
		{
			// The clipboard is a printed form: it has eleven ruled rows and no more.
			break;
		}
		if (Career == nullptr || !Career->OwnsHelicopter(Definition.InternalTypeIndex))
		{
			continue;
		}

		const float RowY = InventoryFirstRowY + Row * InventoryRowHeight;
		AddAt(Canvas, InventoryNameColumnX + 6.0f, RowY + 2.0f, InventoryNameColumnWidth - 12.0f, InventoryRowHeight - 2.0f,
			MakePageText(SimCopterHangarShop::GetModelDisplayName(Definition.InternalTypeIndex), 10, PaperInk, ETextJustify::Left, false, false));

		// The equipment mask is one career record, not one per airframe, so every row carries the
		// same ticks - which is exactly what career + 0x48 can express.
		for (int32 Column = 0; Column < InventoryColumnCount; ++Column)
		{
			const ESimCopterHelicopterTool Tool = SimCopterHangarShop::GetToolForInventoryColumn(Column);
			const int32 Bit = SimCopterHelicopterRegistry::GetToolCareerBit(Tool);
			if (Bit == 0 || (EquipmentMask & Bit) == 0)
			{
				continue;
			}

			const float CellX = InventoryColumnLeft[Column] + (InventoryColumnWidth - 18.0f) * 0.5f;
			if (Tick != nullptr)
			{
				AddAt(Canvas, CellX, RowY + 1.0f, 18.0f, 15.0f, SNew(SImage).Image(Tick));
			}
			else
			{
				AddAt(Canvas, CellX, RowY + 1.0f, 18.0f, 15.0f,
					MakePageText(TEXT("X"), 10, PaperInk, ETextJustify::Center, true, false));
			}
		}

		++Row;
	}

	AddAt(Canvas, InventoryDoneX, InventoryDoneY, ShellButtonWidth, ShellButtonHeight,
		MakeArtButton(ShellButtonStrip, ButtonFrameCount, TEXT("Done"),
			FOnClicked::CreateSP(this, &SSimCopterHangarMenu::HandleShowPage, EPage::Hangar)));
}

// --- handlers --------------------------------------------------------------------------------

FReply SSimCopterHangarMenu::HandleShowPage(const EPage NewPage)
{
	ShowPage(NewPage);
	return FReply::Handled();
}

FReply SSimCopterHangarMenu::HandleDone()
{
	OnDoneRequested.ExecuteIfBound();
	return FReply::Handled();
}

FReply SSimCopterHangarMenu::HandleSelectCatalogRow(const int32 NewRow)
{
	CatalogRow = NewRow;
	StatusText.Reset();
	RebuildPage();
	return FReply::Handled();
}

FReply SSimCopterHangarMenu::HandleSelectUpgradeRow(const int32 NewRow)
{
	UpgradeRow = NewRow;
	StatusText.Reset();
	RebuildPage();
	return FReply::Handled();
}

FReply SSimCopterHangarMenu::HandleBuy()
{
	const bool bUpgrades = CatalogRow < 0 || CatalogRow >= CatalogTabCount;
	FString Message;
	if (bUpgrades)
	{
		SimCopterHangarShop::BuyUpgrade(Shop, UpgradeRow, Message);
	}
	else
	{
		SimCopterHangarShop::BuyHelicopter(Shop, CatalogRow, Message);
	}

	StatusText = Message;
	RebuildPage();
	return FReply::Handled();
}

FReply SSimCopterHangarMenu::HandleSell()
{
	const bool bUpgrades = CatalogRow < 0 || CatalogRow >= CatalogTabCount;
	FString Message;
	if (bUpgrades)
	{
		SimCopterHangarShop::SellUpgrade(Shop, UpgradeRow, Message);
	}
	else
	{
		SimCopterHangarShop::SellHelicopter(Shop, CatalogRow, Message);
	}

	StatusText = Message;
	RebuildPage();
	return FReply::Handled();
}

FReply SSimCopterHangarMenu::HandleSetLogSort(const ELogSort NewSort)
{
	LogSort = NewSort;
	RebuildPage();
	return FReply::Handled();
}
