// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSimCopterMessageBox.h"

#include "InputCoreTypes.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimCopterMessageBox"

using namespace SimCopterFrontEnd;
using namespace SimCopterMessageBoxLayout;

namespace
{
const TCHAR* const MessageBoxPage = TEXT("MBOX.BMP");

// The well printed on MBox.bmp is a pale plate, so its text is dark.
const FLinearColor WellText(0.08f, 0.08f, 0.09f, 1.0f);
}

void SSimCopterMessageBox::Construct(const FArguments& InArgs)
{
	bConfirm = InArgs._Confirm;
	OnDismissed = InArgs._OnDismissed;
	OnConfirmed = InArgs._OnConfirmed;

	USimCopterHangarArt* ArtObject = InArgs._Art;
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);

	const float PageX = FMath::RoundToFloat((ScreenWidth - PageWidth) * 0.5f);
	const float PageY = FMath::RoundToFloat((ScreenHeight - PageHeight) * 0.5f);

	AddAt(Canvas, FRect{ PageX, PageY, PageX + PageWidth, PageY + PageHeight },
		MakePageImage(ArtObject, MessageBoxPage));

	AddAt(
		Canvas,
		FRect{ PageX + TextRect.Left, PageY + TextRect.Top, PageX + TextRect.Right, PageY + TextRect.Bottom },
		SNew(STextBlock)
		.Text(InArgs._Message)
		.Justification(ETextJustify::Center)
		.AutoWrapText(true)
		.Font(PageFont(TextFontHeight))
		.ColorAndOpacity(FSlateColor(WellText)));

	const auto AddButton = [&](const float X, const FText& Label, FSimpleDelegate* Delegate)
	{
		AddAt(
			Canvas,
			FRect{ PageX + X, PageY + ButtonY, PageX + X + ButtonWidth, PageY + ButtonY + ButtonHeight },
			MakeButton(
				ArtObject,
				Label,
				ButtonFontHeight,
				FOnClicked::CreateLambda([Delegate]()
				{
					Delegate->ExecuteIfBound();
					return FReply::Handled();
				}),
				ButtonStyles));
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

	ChildSlot
	[
		MakeScaledScreen(Canvas)
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
