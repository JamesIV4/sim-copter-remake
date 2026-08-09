// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSimCopterCitySettings.h"

#include "InputCoreTypes.h"
#include "SSimCopterCheckupSlider.h"
#include "Styling/SlateBrush.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimCopterCitySettings"

using namespace SimCopterFrontEnd;
using namespace SimCopterCitySettingsLayout;

namespace
{
const TCHAR* const CitySettingsPage = TEXT("CITYSET.BMP");

// The loose track art. Unlike the Check-up dialog's SLIDCHK.BMP - which is 193 px against a 202 px
// rect, so drawing it would knock the printed rivets out of register - this one is exactly 26x202,
// the slider rect's own size, so it goes down over the printed trough as the original intends.
const TCHAR* const TrackBitmap = TEXT("SLIDCITY.BMP");
const TCHAR* const ThumbBitmap = TEXT("SLIDERTV.BMP");

// SLIDERTV.BMP is 22x18 against a 26 px wide trough, so it needs no scaling here (the Check-up
// dialog scales its own because that page's tracks are narrower).
constexpr float ThumbScale = 1.0f;

const FLinearColor LabelColor(0.71f, 0.94f, 0.0f, 1.0f);
}

int32 SSimCopterCitySettings::GetValueForSlider(const FSimCopterCitySettingsValues& Values, const int32 Index)
{
	if (Index == 0)
	{
		return Values.Difficulty;
	}
	return (Index >= 1 && Index < SliderCount) ? FMath::RoundToInt(Values.Weights[Index - 1]) : 0;
}

void SSimCopterCitySettings::SetValueForSlider(
	FSimCopterCitySettingsValues& Values,
	const int32 Index,
	const int32 Value)
{
	const int32 Clamped = FMath::Clamp(Value, 0, GetSliderMax(Index));
	if (Index == 0)
	{
		Values.Difficulty = Clamped;
	}
	else if (Index >= 1 && Index < SliderCount)
	{
		Values.Weights[Index - 1] = static_cast<float>(Clamped);
	}
}

const FText& SSimCopterCitySettings::GetLabel(const int32 Index)
{
	// STRINGTABLE 333..340, in construction order.
	static const FText Names[SliderCount] = {
		LOCTEXT("Difficulty", "Difficulty"), // 333
		LOCTEXT("Fire", "Fire"),             // 334
		LOCTEXT("Crime", "Crime"),           // 335
		LOCTEXT("Rescue", "Rescue"),         // 336
		LOCTEXT("Riot", "Riot"),             // 337
		LOCTEXT("Traffic", "Traffic"),       // 338
		LOCTEXT("Medical", "Medical"),       // 339
		LOCTEXT("Transport", "Transport"),   // 340
	};

	static const FText Empty = FText::GetEmpty();
	return (Index >= 0 && Index < SliderCount) ? Names[Index] : Empty;
}

void SSimCopterCitySettings::Construct(const FArguments& InArgs)
{
	Art = InArgs._Art;
	Values = InArgs._Values;
	OnAccepted = InArgs._OnAccepted;
	OnCancelled = InArgs._OnCancelled;

	USimCopterHangarArt* ArtObject = Art;
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);

	const float PageX = FMath::RoundToFloat((ScreenWidth - PageWidth) * 0.5f);
	const float PageY = FMath::RoundToFloat((ScreenHeight - PageHeight) * 0.5f);
	const auto AddAtPage = [&Canvas, PageX, PageY](const FRect& Rect, TSharedRef<SWidget> Widget)
	{
		AddAt(Canvas, FRect{ PageX + Rect.Left, PageY + Rect.Top, PageX + Rect.Right, PageY + Rect.Bottom }, Widget);
	};

	AddAt(Canvas, FRect{ PageX, PageY, PageX + PageWidth, PageY + PageHeight },
		MakePageImage(ArtObject, CitySettingsPage));

	for (int32 Index = 0; Index < SliderCount; ++Index)
	{
		const FRect Track{ SliderX[Index], SliderTop, SliderX[Index] + SliderWidth, SliderBottom };

		if (ArtObject != nullptr)
		{
			if (const FSlateBrush* Brush = ArtObject->GetBitmap(TrackBitmap, /*bColorKeyed=*/false))
			{
				AddAtPage(Track, SNew(SImage).Image(Brush).Visibility(EVisibility::HitTestInvisible));
			}
		}

		TSharedRef<SSimCopterCheckupSlider> Slider = SNew(SSimCopterCheckupSlider)
			.ThumbBrush(ArtObject != nullptr ? ArtObject->GetBitmap(ThumbBitmap, /*bColorKeyed=*/false) : nullptr)
			.ThumbScale(ThumbScale)
			.OnValueChanged_Lambda([this, Index](const float Alpha)
			{
				SetValueForSlider(Values, Index, FMath::RoundToInt(Alpha * static_cast<float>(GetSliderMax(Index))));
			});

		Sliders[Index] = Slider;
		Slider->SetValue(static_cast<float>(GetValueForSlider(Values, Index))
			/ static_cast<float>(GetSliderMax(Index)));

		AddAtPage(Track, Slider);

		// The original prints only the name in each well; the thumb position is the sole value display.
		const FRect& LabelRect = Labels[Index].Rect;

		AddAtPage(
			FRect{ LabelRect.Left, LabelRect.Top, LabelRect.Right, LabelRect.Bottom },
			SNew(STextBlock)
			.Text(GetLabel(Index))
			.Justification(ETextJustify::Center)
			.Visibility(EVisibility::HitTestInvisible)
			.Font(PageFont(LabelFontHeight, /*bBold=*/true))
			.ColorAndOpacity(FSlateColor(LabelColor)));
	}

	AddAtPage(FRect{ OkButtonX, ButtonY, OkButtonX + ButtonWidth, ButtonY + ButtonHeight },
		MakeButton(
			ArtObject,
			LOCTEXT("Ok", "OK"), // STRINGTABLE 331
			ButtonFontHeight,
			FOnClicked::CreateLambda([this]() { Accept(); return FReply::Handled(); }),
			ButtonStyles));

	AddAtPage(FRect{ CancelButtonX, ButtonY, CancelButtonX + ButtonWidth, ButtonY + ButtonHeight },
		MakeButton(
			ArtObject,
			LOCTEXT("Cancel", "Cancel"), // STRINGTABLE 332
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

void SSimCopterCitySettings::Accept()
{
	OnAccepted.ExecuteIfBound(Values);
}

FReply SSimCopterCitySettings::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
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
