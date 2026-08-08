// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSimCopterUserCityPicker.h"

#include "Formats/SimCopterOriginalGamePaths.h"
#include "InputCoreTypes.h"
#include "Misc/Paths.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SimCopterUserCityPicker"

using namespace SimCopterFrontEnd;
using namespace SimCopterUserCityPickerLayout;

namespace
{
const TCHAR* const PickerPage = TEXT("MENU4.BMP");

// The navy title band takes light text; the pale list panel takes dark.
const FLinearColor TitleText(0.90f, 0.93f, 0.98f, 1.0f);
const FLinearColor ListText(0.08f, 0.08f, 0.09f, 1.0f);

}

void SSimCopterUserCityPicker::Construct(const FArguments& InArgs)
{
	OnAccepted = InArgs._OnAccepted;
	OnCancelled = InArgs._OnCancelled;

	for (const FString& Path : InArgs._CityFilePaths)
	{
		Entries.Add(MakeShared<FString>(Path));
	}

	USimCopterHangarArt* ArtObject = InArgs._Art;
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);

	const float PageX = FMath::RoundToFloat((ScreenWidth - PageWidth) * 0.5f);
	const float PageY = FMath::RoundToFloat((ScreenHeight - PageHeight) * 0.5f);
	const auto AddAtPage = [&Canvas, PageX, PageY](const FRect& Rect, TSharedRef<SWidget> Widget)
	{
		AddAt(Canvas, FRect{ PageX + Rect.Left, PageY + Rect.Top, PageX + Rect.Right, PageY + Rect.Bottom }, Widget);
	};

	AddAt(Canvas, FRect{ PageX, PageY, PageX + PageWidth, PageY + PageHeight },
		MakePageImage(ArtObject, PickerPage));

	AddAtPage(Menu4PickerTitleRect,
		SNew(SBox)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Title", "Open A SimCity File")) // STRINGTABLE 40
			.Justification(ETextJustify::Center)
			.Font(PageFont(Menu4PickerTitleFontHeight, /*bBold=*/true))
			.ColorAndOpacity(FSlateColor(TitleText))
		]);

	if (Entries.Num() > 0)
	{
		// The page prints a pale panel behind the list, so the list and its rows have to be
		// transparent; the highlight is a translucent tint over the same paper.
		ListStyle = MakeShared<FTableViewStyle>();
		ListStyle->SetBackgroundBrush(FSlateNoResource());

		const FSlateColorBrush Highlight(FLinearColor(0.20f, 0.34f, 0.55f, 0.45f));
		RowStyle = MakeShared<FTableRowStyle>();
		RowStyle->SetEvenRowBackgroundBrush(FSlateNoResource());
		RowStyle->SetEvenRowBackgroundHoveredBrush(FSlateColorBrush(FLinearColor(0.0f, 0.0f, 0.0f, 0.12f)));
		RowStyle->SetOddRowBackgroundBrush(FSlateNoResource());
		RowStyle->SetOddRowBackgroundHoveredBrush(FSlateColorBrush(FLinearColor(0.0f, 0.0f, 0.0f, 0.12f)));
		RowStyle->SetActiveBrush(Highlight);
		RowStyle->SetActiveHoveredBrush(Highlight);
		RowStyle->SetInactiveBrush(Highlight);
		RowStyle->SetInactiveHoveredBrush(Highlight);
		RowStyle->SetSelectorFocusedBrush(FSlateNoResource());
		RowStyle->SetTextColor(FSlateColor(ListText));
		RowStyle->SetSelectedTextColor(FSlateColor(ListText));

		AddAtPage(ListRect,
			SAssignNew(ListView, SListView<TSharedPtr<FString>>)
			.ListViewStyle(ListStyle.Get())
			.ListItemsSource(&Entries)
			.SelectionMode(ESelectionMode::Single)
			.OnGenerateRow(this, &SSimCopterUserCityPicker::MakeRow)
			.OnMouseButtonDoubleClick_Lambda([this](TSharedPtr<FString>) { Accept(); }));

		ListView->SetSelection(Entries[0]);
	}
	else
	{
		// The "no cities" message is the one thing on this page that shows when MENU4.BMP itself is
		// missing - both want the same folder - so it lands on MakePageImage's dark fallback plate,
		// where ListText would be invisible.
		const bool bHasPage = HasPageBitmap(ArtObject, PickerPage);
		AddAtPage(ListRect,
			SNew(STextBlock)
			.Text(FText::Format(
				LOCTEXT("NoCities", "No .sc2 files found.\n\n{0}"),
				SimCopterOriginalGame::GetMissingDataHint()))
			.Justification(ETextJustify::Center)
			.AutoWrapText(true)
			.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
			.LineHeightPercentage(1.15f)
			.Font(PageFont(ListFontHeight))
			.ColorAndOpacity(FSlateColor(bHasPage ? ListText : PlateTextColor)));
	}

	AddAtPage(FRect{ AcceptButtonX, ButtonY, AcceptButtonX + ButtonWidth, ButtonY + ButtonHeight },
		MakeButton(
			ArtObject,
			LOCTEXT("Ok", "OK"), // STRINGTABLE 20
			ButtonFontHeight,
			FOnClicked::CreateLambda([this]() { Accept(); return FReply::Handled(); }),
			ButtonStyles));

	AddAtPage(FRect{ CancelButtonX, ButtonY, CancelButtonX + ButtonWidth, ButtonY + ButtonHeight },
		MakeButton(
			ArtObject,
			LOCTEXT("Cancel", "Cancel"), // STRINGTABLE 21
			ButtonFontHeight,
			FOnClicked::CreateLambda([this]()
			{
				OnCancelled.ExecuteIfBound();
				return FReply::Handled();
			}),
			ButtonStyles));

	ChildSlot
	[
		MakeScaledScreen(Canvas)
	];
}

TSharedRef<ITableRow> SSimCopterUserCityPicker::MakeRow(
	TSharedPtr<FString> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
		.Style(RowStyle.Get())
		.Padding(FMargin(6.0f, 1.0f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(Item.IsValid() ? FPaths::GetCleanFilename(*Item) : FString()))
			.Font(PageFont(ListFontHeight))
			.ColorAndOpacity(FSlateColor(ListText))
		];
}

void SSimCopterUserCityPicker::Accept()
{
	if (!ListView.IsValid())
	{
		return;
	}

	const TArray<TSharedPtr<FString>> Selected = ListView->GetSelectedItems();
	if (Selected.Num() > 0 && Selected[0].IsValid())
	{
		OnAccepted.ExecuteIfBound(*Selected[0]);
	}
}

FReply SSimCopterUserCityPicker::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape)
	{
		OnCancelled.ExecuteIfBound();
		return FReply::Handled();
	}
	if (Key == EKeys::Enter)
	{
		Accept();
		return FReply::Handled();
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE
