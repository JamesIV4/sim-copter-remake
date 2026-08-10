// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSimCopterMessageBox.h"

#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimCopterMessageBox"

using namespace SimCopterFrontEnd;
using namespace SimCopterMessageBoxLayout;

namespace
{
const TCHAR* const MessageBoxPage = TEXT("MBOX.BMP");

// The well printed on MBox.bmp is a pale plate, so its text is dark.
const FLinearColor WellText(0.08f, 0.08f, 0.09f, 1.0f);

// Without MBox.bmp there is no printed page at all, and every rectangle below is a coordinate into
// a picture that is not there: dark text on nothing, buttons stranded halfway down an empty
// 465x353 rectangle. That is not a rare case - the message this box most often carries is "the
// original game files are missing", and the missing files are why the bitmap is missing too. So
// the no-artwork form is laid out properly instead, sized to its own content.
constexpr float PlateWidth = 430.0f;
constexpr float PlatePadding = 26.0f;
constexpr float PlateButtonGap = 14.0f;
}

void SSimCopterMessageBox::Construct(const FArguments& InArgs)
{
	bConfirm = InArgs._Confirm;
	OnDismissed = InArgs._OnDismissed;
	OnConfirmed = InArgs._OnConfirmed;

	USimCopterHangarArt* ArtObject = InArgs._Art;

	const auto MakeDialogButton = [&](const FText& Label, FSimpleDelegate* Delegate)
	{
		return MakeButton(
			ArtObject,
			Label,
			ButtonFontHeight,
			FOnClicked::CreateLambda([Delegate]()
			{
				Delegate->ExecuteIfBound();
				return FReply::Handled();
			}),
			ButtonStyles);
	};

	ChildSlot
	[
		MakeScaledScreen(
			HasPageBitmap(ArtObject, MessageBoxPage)
				? BuildPrintedPage(ArtObject, InArgs._Message, MakeDialogButton)
				: BuildFallbackPlate(InArgs._Message, MakeDialogButton))
	];
}

TSharedRef<SWidget> SSimCopterMessageBox::BuildPrintedPage(
	USimCopterHangarArt* Art,
	const FText& Message,
	const FMakeDialogButton& MakeDialogButton)
{
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);

	const float PageX = FMath::RoundToFloat((ScreenWidth - PageWidth) * 0.5f);
	const float PageY = FMath::RoundToFloat((ScreenHeight - PageHeight) * 0.5f);

	AddAt(Canvas, FRect{ PageX, PageY, PageX + PageWidth, PageY + PageHeight },
		MakePageImage(Art, MessageBoxPage));

	AddAt(
		Canvas,
		FRect{ PageX + TextRect.Left, PageY + TextRect.Top, PageX + TextRect.Right, PageY + TextRect.Bottom },
		SNew(STextBlock)
		.Text(Message)
		.Justification(ETextJustify::Center)
		.AutoWrapText(true)
		.Font(PageFont(TextFontHeight))
		.ColorAndOpacity(FSlateColor(WellText)));

	const auto AddButton = [&](const float X, const FText& Label, FSimpleDelegate* Delegate)
	{
		AddAt(
			Canvas,
			FRect{ PageX + X, PageY + ButtonY, PageX + X + ButtonWidth, PageY + ButtonY + ButtonHeight },
			MakeDialogButton(Label, Delegate));
	};

	if (bConfirm)
	{
		// FUN_0043d0c0's two-button form, at the two decoded positions.
		AddButton(LeftButtonX, LOCTEXT("Yes", "Yes"), &OnConfirmed); // STRINGTABLE 22
		AddButton(RightButtonX, LOCTEXT("No", "No"), &OnDismissed);  // STRINGTABLE 23
	}
	else
	{
		AddButton(SingleButtonX, LOCTEXT("Ok", "OK"), &OnDismissed); // STRINGTABLE 20
	}

	return Canvas;
}

TSharedRef<SWidget> SSimCopterMessageBox::BuildFallbackPlate(
	const FText& Message,
	const FMakeDialogButton& MakeDialogButton)
{
	TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);

	const auto AddButton = [&](const FText& Label, FSimpleDelegate* Delegate)
	{
		Buttons->AddSlot()
			.AutoWidth()
			.Padding(FMargin(PlateButtonGap * 0.5f, 0.0f))
			[
				SNew(SBox)
				.WidthOverride(ButtonWidth)
				.HeightOverride(ButtonHeight)
				[
					MakeDialogButton(Label, Delegate)
				]
			];
	};

	if (bConfirm)
	{
		AddButton(LOCTEXT("Yes", "Yes"), &OnConfirmed);
		AddButton(LOCTEXT("No", "No"), &OnDismissed);
	}
	else
	{
		AddButton(LOCTEXT("Ok", "OK"), &OnDismissed);
	}

	TSharedRef<SWidget> Body =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(PlatePadding, PlatePadding, PlatePadding, PlatePadding * 0.8f))
		[
			SNew(STextBlock)
			.Text(Message)
			.Justification(ETextJustify::Center)
			.AutoWrapText(true)
			// The missing-data message names an absolute folder, and a path has no spaces to break
			// at - word wrapping alone lets it run straight off the plate.
			.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
			.LineHeightPercentage(1.2f)
			.Font(PageFont(TextFontHeight))
			.ColorAndOpacity(FSlateColor(PlateTextColor))
		]
		// A rule above the button row. No words, and it is what stops the buttons reading as
		// floating in the middle of a dark rectangle.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(PlatePadding, 0.0f))
		[
			SNew(SBox)
			.HeightOverride(1.0f)
			[
				SNew(SImage)
				.Image(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
				.ColorAndOpacity(PlateBevelColor)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(FMargin(PlatePadding, PlateButtonGap, PlatePadding, PlatePadding * 0.7f))
		[
			Buttons
		];

	// Centred both ways and only as tall as its contents, so a one-line message gets a compact
	// dialog and a five-line one grows to fit instead of overflowing a fixed well.
	return SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(PlateWidth)
			[
				MakeFallbackPlate(Body)
			]
		];
}

FReply SSimCopterMessageBox::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	if (bConfirm && Key == EKeys::Enter)
	{
		// Enter is Yes on the two-button form; Escape stays No, so the safe answer is the one that
		// needs no thought.
		OnConfirmed.ExecuteIfBound();
		return FReply::Handled();
	}
	if (Key == EKeys::Enter || Key == EKeys::Escape || Key == EKeys::SpaceBar)
	{
		OnDismissed.ExecuteIfBound();
		return FReply::Handled();
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE
