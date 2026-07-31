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
	OnDismissed = InArgs._OnDismissed;

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

	AddAt(
		Canvas,
		FRect{
			PageX + SingleButtonX,
			PageY + ButtonY,
			PageX + SingleButtonX + ButtonWidth,
			PageY + ButtonY + ButtonHeight },
		MakeButton(
			ArtObject,
			LOCTEXT("Ok", "OK"), // STRINGTABLE 20
			ButtonFontHeight,
			FOnClicked::CreateLambda([this]()
			{
				OnDismissed.ExecuteIfBound();
				return FReply::Handled();
			}),
			ButtonStyles));

	ChildSlot
	[
		MakeScaledScreen(Canvas)
	];
}

FReply SSimCopterMessageBox::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Enter || Key == EKeys::Escape || Key == EKeys::SpaceBar)
	{
		OnDismissed.ExecuteIfBound();
		return FReply::Handled();
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE
