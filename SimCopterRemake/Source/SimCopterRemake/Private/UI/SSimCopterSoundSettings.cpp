// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSimCopterSoundSettings.h"

#include "Brushes/SlateColorBrush.h"
#include "InputCoreTypes.h"
#include "SSimCopterCheckupSlider.h"
#include "Styling/SlateBrush.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimCopterSoundSettings"

using namespace SimCopterFrontEnd;
using namespace SimCopterSoundSettingsLayout;

namespace
{
const TCHAR* const SoundPage = TEXT("SOUND.BMP");
const TCHAR* const VerticalThumb = TEXT("SLIDERTV.BMP");
const TCHAR* const HorizontalThumb = TEXT("SLIDERTH.BMP");

// The page's printed labels sit in dark wells and read as a pale green.
const FLinearColor LabelColor(0.71f, 0.94f, 0.0f, 1.0f);

// The toggle lamps. The original's sprite is one of the button strips; the remake tints a flat
// lamp instead, so a missing bitmap still shows state rather than nothing at all.
const FLinearColor LampOn(0.35f, 1.0f, 0.25f, 1.0f);
const FLinearColor LampOff(0.10f, 0.16f, 0.09f, 1.0f);
}

int32 SSimCopterSoundSettings::AlphaToVolume(const float Alpha)
{
	const float Clamped = FMath::Clamp(Alpha, 0.0f, 1.0f);
	return VolumeMin + FMath::RoundToInt(Clamped * static_cast<float>(VolumeMax - VolumeMin));
}

float SSimCopterSoundSettings::VolumeToAlpha(const int32 Volume)
{
	const int32 Clamped = FMath::Clamp(Volume, VolumeMin, VolumeMax);
	return static_cast<float>(Clamped - VolumeMin) / static_cast<float>(VolumeMax - VolumeMin);
}

int32 SSimCopterSoundSettings::GetTunerMax() const
{
	// The original hard-codes 0..2 because it shipped with three stations; the remake globs them
	// off disk, so the dial covers however many are really there.
	return StationCount > 0 ? StationCount - 1 : TunerMax;
}

void SSimCopterSoundSettings::Construct(const FArguments& InArgs)
{
	Art = InArgs._Art;
	Values = InArgs._Values;
	Entered = Values;
	StationCount = InArgs._StationCount;
	StationCallSigns = InArgs._StationCallSigns;
	OnPreviewChanged = InArgs._OnPreviewChanged;
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
		MakePageImage(ArtObject, SoundPage));

	const FSlateBrush* VerticalThumbBrush = ArtObject != nullptr
		? ArtObject->GetBitmap(VerticalThumb, /*bColorKeyed=*/false) : nullptr;
	// FUN_0040af00 picks SLIDERTH.BMP for a horizontal control; it is not in every install, so the
	// vertical cap stands in when it is missing rather than leaving the fader with no thumb.
	const FSlateBrush* HorizontalThumbBrush = ArtObject != nullptr
		? ArtObject->GetBitmap(HorizontalThumb, /*bColorKeyed=*/false) : nullptr;
	if (HorizontalThumbBrush == nullptr)
	{
		HorizontalThumbBrush = VerticalThumbBrush;
	}

	// --- Game Volume, command 6 ---
	AddAtPage(GameVolumeRect,
		SAssignNew(GameVolumeSlider, SSimCopterCheckupSlider)
		.ThumbBrush(HorizontalThumbBrush)
		.Orientation(Orient_Horizontal)
		.OnValueChanged_Lambda([this](const float Alpha)
		{
			Values.GameVolume = AlphaToVolume(Alpha);
			RefreshReadouts();
			Preview();
		}));
	GameVolumeSlider->SetValue(VolumeToAlpha(Values.GameVolume));

	AddAtPage(GameVolumeLabelRect,
		SAssignNew(GameVolumeLabel, STextBlock)
		.Justification(ETextJustify::Center)
		.Visibility(EVisibility::HitTestInvisible)
		.Font(PageFont(LabelFontHeight, /*bBold=*/true))
		.ColorAndOpacity(FSlateColor(LabelColor)));

	// --- radio volume, command 11 ---
	AddAtPage(RadioVolumeRect,
		SAssignNew(RadioVolumeSlider, SSimCopterCheckupSlider)
		.ThumbBrush(VerticalThumbBrush)
		.OnValueChanged_Lambda([this](const float Alpha)
		{
			Values.RadioVolume = AlphaToVolume(Alpha);
			Preview();
		}));
	RadioVolumeSlider->SetValue(VolumeToAlpha(Values.RadioVolume));

	AddAtPage(VolLabelRect,
		SNew(STextBlock)
		.Text(LOCTEXT("Vol", "Vol.")) // STRINGTABLE 140
		.Justification(ETextJustify::Center)
		.Visibility(EVisibility::HitTestInvisible)
		.Font(PageFont(LabelFontHeight, /*bBold=*/true))
		.ColorAndOpacity(FSlateColor(LabelColor)));

	// --- tuner, command 10 ---
	const int32 TunerRange = FMath::Max(GetTunerMax(), 1);
	AddAtPage(TunerRect,
		SAssignNew(TunerSlider, SSimCopterCheckupSlider)
		.ThumbBrush(VerticalThumbBrush)
		.OnValueChanged_Lambda([this, TunerRange](const float Alpha)
		{
			Values.RadioStation = FMath::Clamp(
				FMath::RoundToInt(Alpha * static_cast<float>(TunerRange)), 0, GetTunerMax());
			RefreshReadouts();
			Preview();
		}));
	TunerSlider->SetValue(static_cast<float>(FMath::Clamp(Values.RadioStation, 0, GetTunerMax()))
		/ static_cast<float>(TunerRange));

	AddAtPage(StationLabelRect,
		SAssignNew(StationLabel, STextBlock)
		.Justification(ETextJustify::Center)
		.Visibility(EVisibility::HitTestInvisible)
		.Font(PageFont(ReadoutFontHeight, /*bBold=*/true))
		.ColorAndOpacity(FSlateColor(LabelColor)));

	// --- the three toggles, commands 4, 3 and 5 ---
	AddAtPage(CommercialsToggleRect, BuildToggle(LOCTEXT("Commercials", "Commercials"), &Values.bCommercials));
	AddAtPage(DjToggleRect, BuildToggle(LOCTEXT("Dj", "DJ"), &Values.bDj));
	AddAtPage(AutoQuietToggleRect, BuildToggle(LOCTEXT("AutoQuiet", "Auto-Quiet"), &Values.bAutoQuiet));

	const auto AddLabel = [&AddAtPage](const FRect& Rect, const FText& Text, const ETextJustify::Type Justify)
	{
		AddAtPage(Rect,
			SNew(STextBlock)
			.Text(Text)
			.Justification(Justify)
			.Visibility(EVisibility::HitTestInvisible)
			.Font(PageFont(LabelFontHeight, /*bBold=*/true))
			.ColorAndOpacity(FSlateColor(LabelColor)));
	};

	AddLabel(CommercialsLabelRect, LOCTEXT("Commercials", "Commercials"), ETextJustify::Center); // 138
	AddLabel(DjLabelRect, LOCTEXT("Dj", "DJ"), ETextJustify::Center);                            // 137
	AddLabel(AutoQuietLabelRect, LOCTEXT("AutoQuiet", "Auto-Quiet"), ETextJustify::Right);       // 139

	// --- OK / Cancel, commands 1 and 2 ---
	AddAtPage(FRect{ ButtonX, OkButtonY, ButtonX + ButtonWidth, OkButtonY + ButtonHeight },
		MakeButton(
			ArtObject,
			LOCTEXT("Ok", "OK"), // STRINGTABLE 141
			ButtonFontHeight,
			FOnClicked::CreateLambda([this]() { Accept(); return FReply::Handled(); }),
			ButtonStyles));

	AddAtPage(FRect{ ButtonX, CancelButtonY, ButtonX + ButtonWidth, CancelButtonY + ButtonHeight },
		MakeButton(
			ArtObject,
			LOCTEXT("Cancel", "Cancel"), // STRINGTABLE 142
			ButtonFontHeight,
			FOnClicked::CreateLambda([this]() { Cancel(); return FReply::Handled(); }),
			ButtonStyles));

	ChildSlot
	[
		MakeScaledScreen(Canvas)
	];

	RefreshReadouts();
}

TSharedRef<SWidget> SSimCopterSoundSettings::BuildToggle(const FText& Label, bool* Flag)
{
	// A flat lamp that lights when the flag is set. `Flag` points into this widget's own Values,
	// which outlives every button it owns.
	static const FSlateColorBrush LampBrush(FLinearColor::White);

	TSharedRef<FButtonStyle> Style = MakeShared<FButtonStyle>();
	Style->SetNormal(LampBrush);
	Style->SetHovered(LampBrush);
	Style->SetPressed(LampBrush);
	Style->SetDisabled(LampBrush);
	Style->SetNormalPadding(FMargin(0.0f));
	Style->SetPressedPadding(FMargin(0.0f));
	ButtonStyles.Add(Style);

	return SNew(SButton)
		.ButtonStyle(&Style.Get())
		.ContentPadding(FMargin(0.0f))
		.ToolTipText(Label)
		.ButtonColorAndOpacity_Lambda([Flag]() { return FSlateColor(*Flag ? LampOn : LampOff); })
		.OnClicked_Lambda([this, Flag]()
		{
			*Flag = !*Flag;
			Preview();
			return FReply::Handled();
		});
}

void SSimCopterSoundSettings::RefreshReadouts()
{
	if (GameVolumeLabel.IsValid())
	{
		// STRINGTABLE 130 plus the remake's own percentage, because a fader with no scale printed
		// beside it gives the player nothing to aim at.
		GameVolumeLabel->SetText(FText::Format(
			LOCTEXT("GameVolumeFormat", "Game Volume  {0}%"),
			FText::AsNumber(FMath::RoundToInt(VolumeToAlpha(Values.GameVolume) * 100.0f))));
	}

	if (StationLabel.IsValid())
	{
		StationLabel->SetText(StationCallSigns.IsValidIndex(Values.RadioStation)
			? FText::FromString(StationCallSigns[Values.RadioStation].ToUpper())
			: FText::GetEmpty());
	}
}

void SSimCopterSoundSettings::Preview()
{
	OnPreviewChanged.ExecuteIfBound(Values);
}

void SSimCopterSoundSettings::Accept()
{
	OnAccepted.ExecuteIfBound(Values);
}

void SSimCopterSoundSettings::Cancel()
{
	// The page previews as it is dragged, so Cancel has to put back what was there on entry -
	// the original never has to, because it only pushes the sliders into the mixer on OK.
	Values = Entered;
	OnPreviewChanged.ExecuteIfBound(Values);
	OnCancelled.ExecuteIfBound();
}

FReply SSimCopterSoundSettings::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape)
	{
		Cancel();
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
