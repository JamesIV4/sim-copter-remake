// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SSimCopterSaveGamePicker.h"

#include "Brushes/SlateColorBrush.h"
#include "InputCoreTypes.h"
#include "Styling/SlateBrush.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SimCopterSaveGamePicker"

using namespace SimCopterFrontEnd;
using namespace SimCopterSaveGamePickerLayout;

namespace
{
const TCHAR* const PickerPage = TEXT("MENU4.BMP");
const FLinearColor TitleText(0.90f, 0.93f, 0.98f, 1.0f);
const FLinearColor ListText(0.08f, 0.08f, 0.09f, 1.0f);
}

void SSimCopterSaveGamePicker::Construct(const FArguments& InArgs)
{
	OnAccepted = InArgs._OnAccepted;
	OnCancelled = InArgs._OnCancelled;
	for (const FSimCopterSaveSummary& Summary : InArgs._Saves)
	{
		Entries.Add(MakeShared<FSimCopterSaveSummary>(Summary));
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
	AddAtPage(TitleRect,
		SNew(STextBlock)
		.Text(InArgs._Kind == ESimCopterSessionKind::Career
			? LOCTEXT("CareerTitle", "Open A SimCopter Career Game")
			: LOCTEXT("UserTitle", "Open A SimCopter User Game"))
		.Justification(ETextJustify::Center)
		.Font(PageFont(TitleFontHeight, /*bBold=*/true))
		.ColorAndOpacity(FSlateColor(TitleText)));

	if (Entries.Num() > 0)
	{
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
			SAssignNew(ListView, SListView<TSharedPtr<FSimCopterSaveSummary>>)
			.ListViewStyle(ListStyle.Get())
			.ListItemsSource(&Entries)
			.SelectionMode(ESelectionMode::Single)
			.OnGenerateRow(this, &SSimCopterSaveGamePicker::MakeRow)
			.OnMouseButtonDoubleClick_Lambda(
				[this](TSharedPtr<FSimCopterSaveSummary>) { Accept(); }));
		ListView->SetSelection(Entries[0]);
	}
	else
	{
		AddAtPage(ListRect,
			SNew(STextBlock)
			.Text(LOCTEXT("NoSaves", "No compatible saved games were found."))
			.Justification(ETextJustify::Center)
			.AutoWrapText(true)
			.Font(PageFont(17))
			.ColorAndOpacity(FSlateColor(ListText)));
	}

	AddAtPage(FRect{ AcceptButtonX, ButtonY, AcceptButtonX + ButtonWidth, ButtonY + ButtonHeight },
		MakeButton(
			ArtObject,
			LOCTEXT("Open", "Open"),
			ButtonFontHeight,
			FOnClicked::CreateLambda([this]() { Accept(); return FReply::Handled(); }),
			ButtonStyles));
	AddAtPage(FRect{ CancelButtonX, ButtonY, CancelButtonX + ButtonWidth, ButtonY + ButtonHeight },
		MakeButton(
			ArtObject,
			LOCTEXT("Cancel", "Cancel"),
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

TSharedRef<ITableRow> SSimCopterSaveGamePicker::MakeRow(
	TSharedPtr<FSimCopterSaveSummary> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	FString Label;
	if (Item.IsValid())
	{
		Label = FString::Printf(
			TEXT("%s  |  %s  |  $%d  |  %d pts  |  %s UTC"),
			*Item->DisplayName,
			*Item->CityName,
			Item->Cash,
			Item->Score,
			*Item->SavedAtUtc.ToString(TEXT("%Y-%m-%d %H:%M")));
	}

	return SNew(STableRow<TSharedPtr<FSimCopterSaveSummary>>, OwnerTable)
		.Style(RowStyle.Get())
		.Padding(FMargin(6.0f, 2.0f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(Label))
			.Font(PageFont(ListFontHeight))
			.ColorAndOpacity(FSlateColor(ListText))
		];
}

void SSimCopterSaveGamePicker::Accept()
{
	if (!ListView.IsValid())
	{
		return;
	}
	const TArray<TSharedPtr<FSimCopterSaveSummary>> Selected = ListView->GetSelectedItems();
	if (Selected.Num() > 0 && Selected[0].IsValid())
	{
		OnAccepted.ExecuteIfBound(Selected[0]->SlotName);
	}
}

FReply SSimCopterSaveGamePicker::OnKeyDown(
	const FGeometry& MyGeometry,
	const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		OnCancelled.ExecuteIfBound();
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey() == EKeys::Enter)
	{
		Accept();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE

