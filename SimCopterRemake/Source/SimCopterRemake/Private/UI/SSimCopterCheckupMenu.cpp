// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSimCopterCheckupMenu.h"

#include "SSimCopterCheckupSlider.h"
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
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
using namespace SimCopterCheckupLayout;

const TCHAR* const CheckupPage = TEXT("CHECKUP.BMP");
const TCHAR* const ShellButtonStrip = TEXT("BUTTON.BMP");
constexpr int32 ButtonFrameCount = 3;

// FUN_0040af00's default thumb for a vertical slider. The Check-up dialog passes null for the
// thumb argument, so all three of its sliders land on this one - see SSimCopterCheckupSlider.h.
const TCHAR* const SliderThumbBitmap = TEXT("SLIDERTV.BMP");

// The wells printed on the page are near-black, so their text is the panel's pale label colour.
const FLinearColor WellText(0.94f, 0.94f, 0.90f, 1.0f);

FSlateFontInfo CheckupFont(const int32 Size, const bool bBold)
{
	return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size);
}

// The original prints its readouts as plain integers - no thousands separator (FUN_004447e0
// formats through one "%ld"-style sprintf).
FString FormatDollars(int32 Value)
{
	return FString::FromInt(Value);
}

// STRINGTABLE ids 590..598, the English block. They are resources rather than .rdata literals -
// see Docs/memory/simcopter-hangar-shell.md - and the remake has no resource reader, so the
// English text is inlined here with the id that owns it.
const FText& TitleLabel()   { static const FText T = FText::FromString(TEXT("Check-up"));   return T; } // 590
const FText& FundsLabel()   { static const FText T = FText::FromString(TEXT("Funds:"));     return T; } // 591
const FText& TotalLabel()   { static const FText T = FText::FromString(TEXT("Total Cost:")); return T; } // 592
const FText& DamageLabel()  { static const FText T = FText::FromString(TEXT("Damage"));     return T; } // 593
const FText& FuelLabel()    { static const FText T = FText::FromString(TEXT("Fuel"));       return T; } // 594
const FText& TearGasLabel() { static const FText T = FText::FromString(TEXT("Teargas"));    return T; } // 595
const FText& OkLabel()      { static const FText T = FText::FromString(TEXT("OK"));         return T; } // 596
const FText& CancelLabel()  { static const FText T = FText::FromString(TEXT("Cancel"));     return T; } // 597
const FText& CostLabel()    { static const FText T = FText::FromString(TEXT("Cost:"));      return T; } // 598

const FText& SliderLabel(int32 Index)
{
	switch (Index)
	{
	case 0: return DamageLabel();
	case 1: return FuelLabel();
	default: return TearGasLabel();
	}
}
}

void SSimCopterCheckupMenu::Construct(const FArguments& InArgs)
{
	State = InArgs._State;
	Art = InArgs._Art;
	OnAccepted = InArgs._OnAccepted;
	OnCancelled = InArgs._OnCancelled;

	SliderMaxima[0] = FSimCopterCheckup::GetDamageSliderMaxDollars(State);
	SliderMaxima[1] = FSimCopterCheckup::GetFuelSliderMaxDollars(State);
	SliderMaxima[2] = FSimCopterCheckup::GetTearGasSliderMaxRounds(State);

	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);

	// Places a widget at one of FUN_00443c20's rectangles, in CHECKUP.BMP pixel space.
	const auto AddAt = [&Canvas](const FCheckupRect& Rect, TSharedRef<SWidget> Widget)
	{
		Canvas->AddSlot()
			.Offset(FMargin(Rect.Left, Rect.Top, Rect.Width(), Rect.Height()))
			.Anchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f))
			.Alignment(FVector2D::ZeroVector)
			[
				Widget
			];
	};

	// The page art itself, or a plain dark panel when the BMP folder is not there.
	const FSlateBrush* PageBrush = Art != nullptr ? Art->GetBitmap(CheckupPage, /*bColorKeyed*/ true) : nullptr;
	AddAt(FCheckupRect{ 0.0f, 0.0f, PageWidth, PageHeight },
		PageBrush != nullptr
			? StaticCastSharedRef<SWidget>(SNew(SImage).Image(PageBrush))
			: StaticCastSharedRef<SWidget>(
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
				.BorderBackgroundColor(FLinearColor(0.16f, 0.17f, 0.18f, 1.0f))));

	// Text sits at the TOP of its rectangle, which is how the original draws it - that is what
	// keeps each slider's name plate and its cost readout apart where the two boxes overlap.
	const auto AddText = [&AddAt](
		const FCheckupRect& Rect,
		const FText& Text,
		ETextJustify::Type Justify,
		int32 FontSize,
		bool bBold,
		TSharedPtr<STextBlock>* OutBlock)
	{
		TSharedRef<STextBlock> Block = SNew(STextBlock)
			.Text(Text)
			.Justification(Justify)
			.Font(CheckupFont(FontSize, bBold))
			.ColorAndOpacity(FSlateColor(WellText));
		if (OutBlock != nullptr)
		{
			*OutBlock = Block;
		}
		AddAt(Rect, Block);
	};

	AddText(TitleRect, TitleLabel(), ETextJustify::Center, TitleFontSize, /*bBold*/ true, nullptr);

	// Funds and Total Cost share the second line of the top well, each number in its own box
	// immediately to the right of its label rather than pushed out to the panel edge.
	AddText(FundsLabelRect, FundsLabel(), ETextJustify::Center, BodyFontSize, false, nullptr);
	AddText(FundsValueRect, FText::GetEmpty(), ETextJustify::Left, BodyFontSize, false, &FundsText);
	AddText(TotalLabelRect, TotalLabel(), ETextJustify::Center, BodyFontSize, false, nullptr);
	AddText(TotalValueRect, FText::GetEmpty(), ETextJustify::Left, BodyFontSize, false, &TotalCostText);

	for (int32 Index = 0; Index < SliderCount; ++Index)
	{
		AddAt(SliderControlRect[Index], BuildSlider(Index));
		AddText(SliderLabelRect[Index], SliderLabel(Index), ETextJustify::Center, BodyFontSize, false, nullptr);
		AddText(SliderValueRect[Index], FText::GetEmpty(), ETextJustify::Center, BodyFontSize, false,
			&ValueTexts[Index]);
	}

	AddAt(FCheckupRect{ OkButtonX, ButtonY, OkButtonX + ButtonWidth, ButtonY + ButtonHeight },
		BuildButton(OkLabel(), FOnClicked::CreateLambda([this]()
		{
			OnAccepted.ExecuteIfBound(BuildOrder());
			return FReply::Handled();
		})));
	AddAt(FCheckupRect{ CancelButtonX, ButtonY, CancelButtonX + ButtonWidth, ButtonY + ButtonHeight },
		BuildButton(CancelLabel(), FOnClicked::CreateLambda([this]()
		{
			OnCancelled.ExecuteIfBound();
			return FReply::Handled();
		})));

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f))
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SScaleBox)
			.Stretch(EStretch::ScaleToFit)
			[
				SNew(SBox)
				.WidthOverride(PageWidth)
				.HeightOverride(PageHeight)
				[
					Canvas
				]
			]
		]
	];

	RefreshReadouts();
}

TSharedRef<SWidget> SSimCopterCheckupMenu::BuildSlider(int32 Index)
{
	// The original's tracks are vertical and read bottom-up: zero at the bottom, the full
	// repair / full tank / ten canisters at the top.
	TSharedRef<SSimCopterCheckupSlider> Slider = SNew(SSimCopterCheckupSlider)
		.ThumbBrush(Art != nullptr ? Art->GetBitmap(SliderThumbBitmap, /*bColorKeyed*/ false) : nullptr)
		.ThumbScale(SliderThumbScale)
		.Locked(SliderMaxima[Index] <= 0)
		.OnValueChanged_Lambda([this](float) { RefreshReadouts(); });

	Sliders[Index] = Slider;
	return Slider;
}

TSharedRef<SWidget> SSimCopterCheckupMenu::BuildButton(const FText& Label, FOnClicked OnClicked)
{
	USimCopterHangarArt* ArtObject = Art;
	const FSlateBrush* Normal = ArtObject != nullptr
		? ArtObject->GetStripFrame(ShellButtonStrip, 2, ButtonFrameCount) : nullptr;
	const FSlateBrush* Hovered = ArtObject != nullptr
		? ArtObject->GetStripFrame(ShellButtonStrip, 1, ButtonFrameCount) : nullptr;
	const FSlateBrush* Pressed = ArtObject != nullptr
		? ArtObject->GetStripFrame(ShellButtonStrip, 0, ButtonFrameCount) : nullptr;

	TSharedRef<SButton> Button = SNew(SButton)
		.OnClicked(OnClicked)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.ContentPadding(FMargin(0.0f))
		[
			SNew(STextBlock)
			.Text(Label)
			.Justification(ETextJustify::Center)
			.Font(CheckupFont(BodyFontSize, /*bBold*/ true))
			.ColorAndOpacity(FSlateColor(WellText))
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f))
		];

	// BUTTON.BMP's three frames are the same strip the hangar shell uses.
	if (Normal != nullptr)
	{
		TSharedRef<FButtonStyle> Style = MakeShared<FButtonStyle>();
		Style->SetNormal(*Normal);
		Style->SetHovered(Hovered != nullptr ? *Hovered : *Normal);
		Style->SetPressed(Pressed != nullptr ? *Pressed : *Normal);
		Style->SetDisabled(*Normal);
		Style->SetNormalPadding(FMargin(0.0f));
		Style->SetPressedPadding(FMargin(0.0f));
		ButtonStyles.Add(Style);
		Button->SetButtonStyle(&Style.Get());
	}

	return Button;
}

FSimCopterCheckupOrder SSimCopterCheckupMenu::BuildOrder() const
{
	const auto Read = [this](int32 Index)
	{
		const float Fraction = Sliders[Index].IsValid() ? Sliders[Index]->GetValue() : 0.0f;
		return FMath::RoundToInt(Fraction * static_cast<float>(SliderMaxima[Index]));
	};

	FSimCopterCheckupOrder Order;
	Order.DamageDollars = Read(0);
	Order.FuelDollars = Read(1);
	Order.TearGasRounds = Read(2);
	return FSimCopterCheckup::ClampOrder(State, Order);
}

void SSimCopterCheckupMenu::RefreshReadouts()
{
	const FSimCopterCheckupOrder Order = BuildOrder();

	if (ValueTexts[0].IsValid())
	{
		ValueTexts[0]->SetText(FText::FromString(FString::Printf(
			TEXT("%s %s"), *CostLabel().ToString(), *FormatDollars(Order.DamageDollars))));
	}
	if (ValueTexts[1].IsValid())
	{
		ValueTexts[1]->SetText(FText::FromString(FString::Printf(
			TEXT("%s %s"), *CostLabel().ToString(), *FormatDollars(Order.FuelDollars))));
	}
	if (ValueTexts[2].IsValid())
	{
		// The tear-gas slider counts canisters, so show both the count and what it costs.
		ValueTexts[2]->SetText(FText::FromString(FString::Printf(
			TEXT("%d  %s %s"),
			Order.TearGasRounds,
			*CostLabel().ToString(),
			*FormatDollars(FSimCopterCheckup::GetTearGasCostDollars(Order.TearGasRounds)))));
	}
	if (TotalCostText.IsValid())
	{
		TotalCostText->SetText(FText::FromString(FormatDollars(FSimCopterCheckup::GetTotalCostDollars(Order))));
	}
	if (FundsText.IsValid())
	{
		FundsText->SetText(FText::FromString(FormatDollars(State.Funds)));
	}
}
