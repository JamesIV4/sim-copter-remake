// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSimCopterGraphicsSettings.h"

#include "Brushes/SlateColorBrush.h"
#include "City/SimCopterDayNight.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Game/SimCopterSettings.h"
#include "GameFramework/GameUserSettings.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetSystemLibrary.h"
#include "SSimCopterCheckupSlider.h"
#include "Styling/CoreStyle.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimCopterGraphicsSettings"

using namespace SimCopterFrontEnd;
using namespace SimCopterGraphicsSettingsLayout;

namespace
{
const TCHAR* const GraphicsPage = TEXT("RENDER.BMP");
const TCHAR* const ThumbBitmap = TEXT("SLIDERTH.BMP");
const TCHAR* const FallbackThumbBitmap = TEXT("SLIDERTV.BMP");

// The list sits on its own backing panel: render.bmp prints five separate wells and eighteen rows
// laid straight over them would band. Near-opaque, or the printed wells read through the labels.
const FLinearColor PanelColor(0.05f, 0.06f, 0.06f, 0.97f);
const FLinearColor RowText(0.82f, 0.87f, 0.72f, 1.0f);
const FLinearColor HeadingText(0.92f, 0.96f, 0.60f, 1.0f);
const FLinearColor DisabledText(0.40f, 0.43f, 0.38f, 1.0f);

USimCopterSettings* GetSettings(const SWidget* Widget)
{
	const UWorld* World = (GEngine != nullptr && GEngine->GameViewport != nullptr)
		? GEngine->GameViewport->GetWorld()
		: nullptr;
	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	return GameInstance != nullptr ? GameInstance->GetSubsystem<USimCopterSettings>() : nullptr;
}

UGameUserSettings* GetUserSettings()
{
	return GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
}
}

FText SSimCopterGraphicsSettings::GetQualityLevelLabel(const int32 Level)
{
	switch (Level)
	{
	case 0:  return LOCTEXT("QualityLow", "Low");
	case 1:  return LOCTEXT("QualityMedium", "Medium");
	case 2:  return LOCTEXT("QualityHigh", "High");
	case 3:  return LOCTEXT("QualityEpic", "Epic");
	case 4:  return LOCTEXT("QualityCinematic", "Cinematic");
	default: return LOCTEXT("QualityCustom", "Custom");
	}
}

void SSimCopterGraphicsSettings::GetFrameRateOptions(TArray<float>& OutRates)
{
	OutRates = { 0.0f, 30.0f, 60.0f, 90.0f, 120.0f, 144.0f, 165.0f, 240.0f, 360.0f };
}

void SSimCopterGraphicsSettings::Construct(const FArguments& InArgs)
{
	Art = InArgs._Art;
	OnAccepted = InArgs._OnAccepted;
	OnCancelled = InArgs._OnCancelled;

	CaptureEnteredState();

	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
	if (Resolutions.Num() == 0)
	{
		// Headless and some virtual displays report none; the current mode is still a valid choice.
		if (const UGameUserSettings* UserSettings = GetUserSettings())
		{
			Resolutions.Add(UserSettings->GetScreenResolution());
		}
	}

	USimCopterHangarArt* ArtObject = Art;
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);

	const float PageX = FMath::RoundToFloat((ScreenWidth - PageWidth) * 0.5f);
	const float PageY = FMath::RoundToFloat((ScreenHeight - PageHeight) * 0.5f);
	const auto AddAtPage = [&Canvas, PageX, PageY](const FRect& Rect, TSharedRef<SWidget> Widget)
	{
		AddAt(Canvas, FRect{ PageX + Rect.Left, PageY + Rect.Top, PageX + Rect.Right, PageY + Rect.Bottom }, Widget);
	};

	AddAt(Canvas, FRect{ PageX, PageY, PageX + PageWidth, PageY + PageHeight },
		MakePageImage(ArtObject, GraphicsPage));

	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	PopulateRows(Rows);

	AddAtPage(ListRect,
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(PanelColor)
		.Padding(FMargin(8.0f, 6.0f))
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				Rows
			]
		]);

	AddAtPage(FRect{ OkButtonX, ButtonY, OkButtonX + ButtonWidth, ButtonY + ButtonHeight },
		MakeButton(
			ArtObject,
			LOCTEXT("Ok", "OK"), // STRINGTABLE 81
			ButtonFontHeight,
			FOnClicked::CreateLambda([this]() { Accept(); return FReply::Handled(); }),
			ButtonStyles));

	AddAtPage(FRect{ CancelButtonX, ButtonY, CancelButtonX + ButtonWidth, ButtonY + ButtonHeight },
		MakeButton(
			ArtObject,
			LOCTEXT("Cancel", "Cancel"), // STRINGTABLE 82
			ButtonFontHeight,
			FOnClicked::CreateLambda([this]() { Cancel(); return FReply::Handled(); }),
			ButtonStyles));

	ChildSlot
	[
		MakeScaledScreen(Canvas)
	];
}

TSharedRef<SWidget> SSimCopterGraphicsSettings::BuildHeading(const FText& Text)
{
	return SNew(SBox)
		.HeightOverride(RowHeight)
		.Padding(FMargin(0.0f, 6.0f, 0.0f, 0.0f))
		[
			SNew(STextBlock)
			.Text(Text)
			.Font(PageFont(HeadingFontHeight, /*bBold=*/true))
			.ColorAndOpacity(FSlateColor(HeadingText))
		];
}

TSharedRef<SWidget> SSimCopterGraphicsSettings::BuildDropdownRow(const FText& Label, FRowBinding Binding)
{
	// SComboBox keeps a raw pointer to its items source, so the array has to outlive Construct.
	TSharedRef<TArray<TSharedPtr<int32>>> Options = MakeShared<TArray<TSharedPtr<int32>>>();
	const int32 Count = Binding.GetCount ? Binding.GetCount() : 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Options->Add(MakeShared<int32>(Index));
	}

	const int32 Current = Binding.GetIndex ? Binding.GetIndex() : 0;
	TSharedPtr<int32> Initial = Options->IsValidIndex(Current) ? (*Options)[Current] : nullptr;

	// Copied by value into every lambda below, so the row keeps working after Construct returns.
	const TFunction<FText(int32)> LabelFor = Binding.GetOptionLabel;
	const TFunction<int32()> GetIndex = Binding.GetIndex;
	const TFunction<void(int32)> SetIndex = Binding.SetIndex;
	const TFunction<bool()> IsEnabled = Binding.IsEnabled;

	TSharedRef<SComboBox<TSharedPtr<int32>>> Combo = SNew(SComboBox<TSharedPtr<int32>>)
		.OptionsSource(&Options.Get())
		.InitiallySelectedItem(Initial)
		.IsEnabled_Lambda([IsEnabled]() { return !IsEnabled || IsEnabled(); })
		.OnGenerateWidget_Lambda([LabelFor](TSharedPtr<int32> Item)
		{
			return SNew(STextBlock)
				.Text(Item.IsValid() && LabelFor ? LabelFor(*Item) : FText::GetEmpty())
				.Font(PageFont(RowFontHeight));
		})
		.OnSelectionChanged_Lambda([SetIndex](TSharedPtr<int32> Item, ESelectInfo::Type)
		{
			if (Item.IsValid() && SetIndex)
			{
				SetIndex(*Item);
			}
		})
		[
			SNew(STextBlock)
			.Text_Lambda([LabelFor, GetIndex]()
			{
				return (LabelFor && GetIndex) ? LabelFor(GetIndex()) : FText::GetEmpty();
			})
			.Font(PageFont(RowFontHeight))
		];

	ComboOptionSources.Add(Options);

	return SNew(SBox)
		.HeightOverride(RowHeight)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(RowLabelWidth)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(PageFont(RowFontHeight))
					.ColorAndOpacity_Lambda([IsEnabled]()
					{
						return FSlateColor((!IsEnabled || IsEnabled()) ? RowText : DisabledText);
					})
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				Combo
			]
		];
}

TSharedRef<SWidget> SSimCopterGraphicsSettings::BuildSliderRow(
	const FText& Label,
	TFunction<float()> GetAlpha,
	TFunction<void(float)> SetAlpha,
	TFunction<FText()> GetText,
	TFunction<bool()> IsEnabled)
{
	USimCopterHangarArt* ArtObject = Art;
	const FSlateBrush* Thumb = ArtObject != nullptr ? ArtObject->GetBitmap(ThumbBitmap, /*bColorKeyed=*/false) : nullptr;
	if (Thumb == nullptr && ArtObject != nullptr)
	{
		Thumb = ArtObject->GetBitmap(FallbackThumbBitmap, /*bColorKeyed=*/false);
	}

	TSharedRef<SSimCopterCheckupSlider> Slider = SNew(SSimCopterCheckupSlider)
		.ThumbBrush(Thumb)
		.Orientation(Orient_Horizontal)
		.IsEnabled_Lambda([IsEnabled]() { return !IsEnabled || IsEnabled(); })
		.OnValueChanged_Lambda([SetAlpha](const float Alpha)
		{
			if (SetAlpha)
			{
				SetAlpha(Alpha);
			}
		});
	Slider->SetValue(GetAlpha ? GetAlpha() : 0.0f);

	return SNew(SBox)
		.HeightOverride(RowHeight)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(RowLabelWidth)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(PageFont(RowFontHeight))
					.ColorAndOpacity_Lambda([IsEnabled]()
					{
						return FSlateColor((!IsEnabled || IsEnabled()) ? RowText : DisabledText);
					})
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.0f, 4.0f))
			[
				Slider
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
			[
				SNew(SBox)
				.WidthOverride(64.0f)
				[
					SNew(STextBlock)
					.Justification(ETextJustify::Right)
					.Text_Lambda([GetText]() { return GetText ? GetText() : FText::GetEmpty(); })
					.Font(PageFont(RowFontHeight))
					.ColorAndOpacity_Lambda([IsEnabled]()
					{
						return FSlateColor((!IsEnabled || IsEnabled()) ? RowText : DisabledText);
					})
				]
			]
		];
}

TSharedRef<SWidget> SSimCopterGraphicsSettings::BuildCheckboxRow(
	const FText& Label,
	TFunction<bool()> IsChecked,
	TFunction<void(bool)> SetChecked,
	TFunction<bool()> IsEnabled)
{
	return SNew(SBox)
		.HeightOverride(RowHeight)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(RowLabelWidth)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(PageFont(RowFontHeight))
					.ColorAndOpacity_Lambda([IsEnabled]()
					{
						return FSlateColor((!IsEnabled || IsEnabled()) ? RowText : DisabledText);
					})
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsEnabled_Lambda([IsEnabled]() { return !IsEnabled || IsEnabled(); })
				.IsChecked_Lambda([IsChecked]()
				{
					return (IsChecked && IsChecked()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([SetChecked](const ECheckBoxState State)
				{
					if (SetChecked)
					{
						SetChecked(State == ECheckBoxState::Checked);
					}
				})
			]
		];
}

TSharedRef<SWidget> SSimCopterGraphicsSettings::BuildNote(const FText& Text)
{
	return SNew(SBox)
		.Padding(FMargin(0.0f, 0.0f, 8.0f, 4.0f))
		[
			SNew(STextBlock)
			.Text(Text)
			.Font(PageFont(RowFontHeight - 3))
			.ColorAndOpacity(FSlateColor(DisabledText))
			.AutoWrapText(true)
		];
}

void SSimCopterGraphicsSettings::PopulateRows(const TSharedRef<SVerticalBox>& Rows)
{
	const auto AddRow = [&Rows](TSharedRef<SWidget> Row)
	{
		Rows->AddSlot().AutoHeight().Padding(FMargin(0.0f, 1.0f))[Row];
	};

	// ---------------------------------------------------------------------------------------
	// Performance. First on the page because it moves most of the rows below it: a machine that
	// needs this needs it before it needs a DLSS quality mode.
	// ---------------------------------------------------------------------------------------

	AddRow(BuildHeading(LOCTEXT("HeadingPerformance", "Performance")));

	AddRow(BuildCheckboxRow(
		LOCTEXT("LowPowerMode", "Low Power Graphics"),
		[this]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			return Settings != nullptr && Settings->IsLowPowerMode();
		},
		[this](const bool bEnabled)
		{
			if (USimCopterSettings* Settings = GetSettings(this))
			{
				Settings->SetLowPowerMode(bEnabled);
				Settings->ApplyAll(nullptr);
			}
		}));

	AddRow(BuildNote(LOCTEXT("LowPowerModeNote",
		"Simple lighting, no shadows, 75% resolution. Performance over visuals.")));

	// ---------------------------------------------------------------------------------------
	// Upscaling and frame generation
	// ---------------------------------------------------------------------------------------

	AddRow(BuildHeading(LOCTEXT("HeadingUpscaling", "NVIDIA DLSS")));

	TArray<ESimCopterDlssQuality> Qualities;
	USimCopterSettings::GetAvailableDlssQualities(Qualities);
	TArray<int32> Multiples;
	USimCopterSettings::GetAvailableFrameGenMultiples(Multiples);

	const bool bDlssAvailable = USimCopterSettings::IsDlssAvailable() && Qualities.Num() > 0;
	const bool bFrameGenAvailable = USimCopterSettings::IsFrameGenAvailable();

	if (!bDlssAvailable && !bFrameGenAvailable)
	{
		// Say so rather than showing four dead dropdowns.
		AddRow(SNew(SBox)
			.HeightOverride(RowHeight)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoDlss", "Not available on this GPU or driver."))
				.Font(PageFont(RowFontHeight))
				.ColorAndOpacity(FSlateColor(DisabledText))
			]);
	}

	if (bDlssAvailable)
	{
		FRowBinding Enable;
		Enable.GetCount = []() { return 2; };
		Enable.GetOptionLabel = [](const int32 Index)
		{
			return Index == 1 ? LOCTEXT("On", "On") : LOCTEXT("Off", "Off");
		};
		Enable.GetIndex = [this]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			return (Settings != nullptr && Settings->IsDlssEnabled()) ? 1 : 0;
		};
		Enable.SetIndex = [this](const int32 Index)
		{
			if (USimCopterSettings* Settings = GetSettings(this))
			{
				Settings->SetDlssEnabled(Index == 1);
				Settings->ApplyAll(nullptr);
			}
		};
		AddRow(BuildDropdownRow(LOCTEXT("Dlss", "Super Resolution"), Enable));

		FRowBinding Quality;
		Quality.GetCount = [Qualities]() { return Qualities.Num(); };
		Quality.GetOptionLabel = [Qualities](const int32 Index)
		{
			return Qualities.IsValidIndex(Index)
				? USimCopterSettings::GetDlssQualityLabel(Qualities[Index])
				: FText::GetEmpty();
		};
		Quality.GetIndex = [this, Qualities]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			return Settings != nullptr ? FMath::Max(Qualities.IndexOfByKey(Settings->GetDlssQuality()), 0) : 0;
		};
		Quality.SetIndex = [this, Qualities](const int32 Index)
		{
			if (USimCopterSettings* Settings = GetSettings(this); Settings != nullptr && Qualities.IsValidIndex(Index))
			{
				Settings->SetDlssQuality(Qualities[Index]);
				Settings->ApplyAll(nullptr);
			}
		};
		// Greyed while the upscaler is off, because SetDLSSMode does nothing then.
		Quality.IsEnabled = [this]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			return Settings != nullptr && Settings->IsDlssEnabled();
		};
		AddRow(BuildDropdownRow(LOCTEXT("DlssQuality", "Quality Mode"), Quality));
	}

	if (bFrameGenAvailable)
	{
		FRowBinding Mode;
		Mode.GetCount = []() { return 3; };
		Mode.GetOptionLabel = [](const int32 Index)
		{
			return USimCopterSettings::GetFrameGenModeLabel(static_cast<ESimCopterFrameGenMode>(Index));
		};
		Mode.GetIndex = [this]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			return Settings != nullptr ? static_cast<int32>(Settings->GetFrameGenMode()) : 0;
		};
		Mode.SetIndex = [this](const int32 Index)
		{
			if (USimCopterSettings* Settings = GetSettings(this))
			{
				Settings->SetFrameGenMode(static_cast<ESimCopterFrameGenMode>(Index));
				Settings->ApplyAll(nullptr);
			}
		};
		AddRow(BuildDropdownRow(LOCTEXT("FrameGen", "Frame Generation"), Mode));

		FRowBinding Multiple;
		Multiple.GetCount = [Multiples]() { return Multiples.Num(); };
		Multiple.GetOptionLabel = [Multiples](const int32 Index)
		{
			return Multiples.IsValidIndex(Index)
				? USimCopterSettings::GetFrameGenMultipleLabel(Multiples[Index])
				: FText::GetEmpty();
		};
		Multiple.GetIndex = [this, Multiples]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			return Settings != nullptr ? FMath::Max(Multiples.IndexOfByKey(Settings->GetFrameGenMultiple()), 0) : 0;
		};
		Multiple.SetIndex = [this, Multiples](const int32 Index)
		{
			if (USimCopterSettings* Settings = GetSettings(this); Settings != nullptr && Multiples.IsValidIndex(Index))
			{
				Settings->SetFrameGenMultiple(Multiples[Index]);
				Settings->ApplyAll(nullptr);
			}
		};
		// Only "On" carries a fixed multiple - Off generates nothing and Auto lets the driver pick.
		Multiple.IsEnabled = [this]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			return Settings != nullptr && Settings->GetFrameGenMode() == ESimCopterFrameGenMode::On;
		};
		AddRow(BuildDropdownRow(LOCTEXT("FrameGenMultiple", "Generated Frames"), Multiple));
	}

	// ---------------------------------------------------------------------------------------
	// Reflex. Its own heading rather than a row under DLSS: it is a separate Streamline feature
	// and it is offered on cards that have no DLSS at all.
	// ---------------------------------------------------------------------------------------

	if (USimCopterSettings::IsReflexAvailable())
	{
		AddRow(BuildHeading(LOCTEXT("HeadingReflex", "NVIDIA Reflex")));

		// Boost is not offered on every card that has Reflex, so the list is what this GPU answers
		// to rather than all three unconditionally.
		TArray<ESimCopterReflexMode> Modes;
		for (const ESimCopterReflexMode Mode : { ESimCopterReflexMode::Off, ESimCopterReflexMode::On, ESimCopterReflexMode::OnBoost })
		{
			if (Mode == ESimCopterReflexMode::Off || USimCopterSettings::IsReflexModeAvailable(Mode))
			{
				Modes.Add(Mode);
			}
		}

		FRowBinding Reflex;
		Reflex.GetCount = [Modes]() { return Modes.Num(); };
		Reflex.GetOptionLabel = [Modes](const int32 Index)
		{
			return Modes.IsValidIndex(Index)
				? USimCopterSettings::GetReflexModeLabel(Modes[Index])
				: FText::GetEmpty();
		};
		Reflex.GetIndex = [this, Modes]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			return Settings != nullptr ? FMath::Max(Modes.IndexOfByKey(Settings->GetReflexMode()), 0) : 0;
		};
		Reflex.SetIndex = [this, Modes](const int32 Index)
		{
			if (USimCopterSettings* Settings = GetSettings(this); Settings != nullptr && Modes.IsValidIndex(Index))
			{
				Settings->SetReflexMode(Modes[Index]);
				Settings->ApplyAll(nullptr);
			}
		};
		AddRow(BuildDropdownRow(LOCTEXT("Reflex", "Low Latency"), Reflex));
	}

	// ---------------------------------------------------------------------------------------
	// Display
	// ---------------------------------------------------------------------------------------

	AddRow(BuildHeading(LOCTEXT("HeadingDisplay", "Display")));

	{
		static const EWindowMode::Type Modes[] = { EWindowMode::Fullscreen, EWindowMode::WindowedFullscreen, EWindowMode::Windowed };

		FRowBinding Window;
		Window.GetCount = []() { return UE_ARRAY_COUNT(Modes); };
		Window.GetOptionLabel = [](const int32 Index)
		{
			switch (Index)
			{
			case 0:  return LOCTEXT("Fullscreen", "Fullscreen");
			case 1:  return LOCTEXT("Borderless", "Borderless Window");
			default: return LOCTEXT("Windowed", "Windowed");
			}
		};
		Window.GetIndex = []()
		{
			const UGameUserSettings* UserSettings = GetUserSettings();
			if (UserSettings == nullptr)
			{
				return 0;
			}
			const EWindowMode::Type Current = UserSettings->GetFullscreenMode();
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Modes); ++Index)
			{
				if (Modes[Index] == Current)
				{
					return Index;
				}
			}
			return 0;
		};
		Window.SetIndex = [](const int32 Index)
		{
			if (UGameUserSettings* UserSettings = GetUserSettings(); UserSettings != nullptr && Index >= 0 && Index < UE_ARRAY_COUNT(Modes))
			{
				UserSettings->SetFullscreenMode(Modes[Index]);
				UserSettings->ApplyResolutionSettings(/*bCheckForCommandLineOverrides=*/false);
			}
		};
		AddRow(BuildDropdownRow(LOCTEXT("WindowMode", "Window Mode"), Window));
	}

	{
		const TArray<FIntPoint> Modes = Resolutions;

		FRowBinding Resolution;
		Resolution.GetCount = [Modes]() { return Modes.Num(); };
		Resolution.GetOptionLabel = [Modes](const int32 Index)
		{
			if (!Modes.IsValidIndex(Index))
			{
				return FText::GetEmpty();
			}
			return FText::Format(
				LOCTEXT("ResolutionFormat", "{0} x {1}"),
				FText::AsNumber(Modes[Index].X),
				FText::AsNumber(Modes[Index].Y));
		};
		Resolution.GetIndex = [Modes]()
		{
			const UGameUserSettings* UserSettings = GetUserSettings();
			return UserSettings != nullptr ? FMath::Max(Modes.IndexOfByKey(UserSettings->GetScreenResolution()), 0) : 0;
		};
		Resolution.SetIndex = [Modes](const int32 Index)
		{
			if (UGameUserSettings* UserSettings = GetUserSettings(); UserSettings != nullptr && Modes.IsValidIndex(Index))
			{
				UserSettings->SetScreenResolution(Modes[Index]);
				UserSettings->ApplyResolutionSettings(/*bCheckForCommandLineOverrides=*/false);
			}
		};
		AddRow(BuildDropdownRow(LOCTEXT("Resolution", "Resolution"), Resolution));
	}

	{
		FRowBinding VSync;
		VSync.GetCount = []() { return 2; };
		VSync.GetOptionLabel = [](const int32 Index)
		{
			return Index == 1 ? LOCTEXT("On", "On") : LOCTEXT("Off", "Off");
		};
		VSync.GetIndex = []()
		{
			const UGameUserSettings* UserSettings = GetUserSettings();
			return (UserSettings != nullptr && UserSettings->IsVSyncEnabled()) ? 1 : 0;
		};
		VSync.SetIndex = [](const int32 Index)
		{
			if (UGameUserSettings* UserSettings = GetUserSettings())
			{
				UserSettings->SetVSyncEnabled(Index == 1);
				UserSettings->ApplyNonResolutionSettings();
			}
		};
		AddRow(BuildDropdownRow(LOCTEXT("VSync", "V-Sync"), VSync));
	}

	{
		TArray<float> Rates;
		GetFrameRateOptions(Rates);

		FRowBinding Limit;
		Limit.GetCount = [Rates]() { return Rates.Num(); };
		Limit.GetOptionLabel = [Rates](const int32 Index)
		{
			if (!Rates.IsValidIndex(Index))
			{
				return FText::GetEmpty();
			}
			return Rates[Index] <= 0.0f
				? LOCTEXT("Unlimited", "Unlimited")
				: FText::Format(LOCTEXT("FpsFormat", "{0} fps"), FText::AsNumber(FMath::RoundToInt(Rates[Index])));
		};
		Limit.GetIndex = [Rates]()
		{
			const UGameUserSettings* UserSettings = GetUserSettings();
			if (UserSettings == nullptr)
			{
				return 0;
			}
			const float Current = UserSettings->GetFrameRateLimit();
			for (int32 Index = 0; Index < Rates.Num(); ++Index)
			{
				if (FMath::IsNearlyEqual(Rates[Index], Current))
				{
					return Index;
				}
			}
			return 0;
		};
		Limit.SetIndex = [Rates](const int32 Index)
		{
			if (UGameUserSettings* UserSettings = GetUserSettings(); UserSettings != nullptr && Rates.IsValidIndex(Index))
			{
				UserSettings->SetFrameRateLimit(Rates[Index]);
				UserSettings->ApplyNonResolutionSettings();
			}
		};
		AddRow(BuildDropdownRow(LOCTEXT("FrameRateLimit", "Frame Rate Limit"), Limit));
	}

	AddRow(BuildSliderRow(
		LOCTEXT("ResolutionScale", "Resolution Scale"),
		[]()
		{
			const UGameUserSettings* UserSettings = GetUserSettings();
			return UserSettings != nullptr ? UserSettings->GetResolutionScaleNormalized() : 1.0f;
		},
		[](const float Alpha)
		{
			if (UGameUserSettings* UserSettings = GetUserSettings())
			{
				UserSettings->SetResolutionScaleNormalized(Alpha);
				UserSettings->ApplyNonResolutionSettings();
			}
		},
		[]()
		{
			const UGameUserSettings* UserSettings = GetUserSettings();
			// Normalized is 0..1 over the platform's own min..100 range, so show the real percentage.
			float Normalized = 1.0f;
			float Current = 100.0f;
			float MinScale = 100.0f;
			float MaxScale = 100.0f;
			if (UserSettings != nullptr)
			{
				UserSettings->GetResolutionScaleInformationEx(Normalized, Current, MinScale, MaxScale);
			}
			return FText::Format(LOCTEXT("PercentFormat", "{0}%"), FText::AsNumber(FMath::RoundToInt(Current)));
		},
		// Greyed while Super Resolution is on: DLSS owns the scale for its quality mode (see
		// ApplyGraphics), and setting a resolution scale DLSS did not ask for is what crashed inside
		// NGX_D3D12_EVALUATE_DLSS_EXT with a source/dest rect DLSS could not evaluate.
		[this]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			return Settings == nullptr || !Settings->IsDlssEnabled();
		}));

	if (bDlssAvailable)
	{
		AddRow(BuildNote(LOCTEXT("ResolutionScaleDlssNote",
			"Resolution Scale is set by the Super Resolution quality mode while Super Resolution is on.")));
	}

	{
		// MSAA is left off the list - see ESimCopterAntiAliasingMethod's comment - so the indices
		// walked here are the enum's own values, not a dense 0..N-1 range.
		static const ESimCopterAntiAliasingMethod Methods[] = {
			ESimCopterAntiAliasingMethod::None,
			ESimCopterAntiAliasingMethod::Fxaa,
			ESimCopterAntiAliasingMethod::TemporalAA,
			ESimCopterAntiAliasingMethod::Tsr,
			ESimCopterAntiAliasingMethod::Smaa,
		};

		FRowBinding AntiAliasing;
		AntiAliasing.GetCount = []() { return UE_ARRAY_COUNT(Methods); };
		AntiAliasing.GetOptionLabel = [](const int32 Index)
		{
			return (Index >= 0 && Index < UE_ARRAY_COUNT(Methods))
				? USimCopterSettings::GetAntiAliasingMethodLabel(Methods[Index])
				: FText::GetEmpty();
		};
		AntiAliasing.GetIndex = [this]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			const ESimCopterAntiAliasingMethod Current =
				Settings != nullptr ? Settings->GetAntiAliasingMethod() : ESimCopterAntiAliasingMethod::Tsr;
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Methods); ++Index)
			{
				if (Methods[Index] == Current)
				{
					return Index;
				}
			}
			return 0;
		};
		AntiAliasing.SetIndex = [this](const int32 Index)
		{
			if (USimCopterSettings* Settings = GetSettings(this); Settings != nullptr && Index >= 0 && Index < UE_ARRAY_COUNT(Methods))
			{
				Settings->SetAntiAliasingMethod(Methods[Index]);
				Settings->ApplyAll(nullptr);
			}
		};
		// Same reason as Resolution Scale above: DLSS hooks the TAA/TSR upsample pass itself and
		// picking a different method out from under it is unsupported.
		AntiAliasing.IsEnabled = [this]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			return Settings == nullptr || !Settings->IsDlssEnabled();
		};
		AddRow(BuildDropdownRow(LOCTEXT("AntiAliasing", "Anti-Aliasing"), AntiAliasing));
	}

	// ---------------------------------------------------------------------------------------
	// Lighting - the two the city actually notices. Lumen carries the night, because the window
	// lights and the street lamps are emissive rather than light components.
	// ---------------------------------------------------------------------------------------

	AddRow(BuildHeading(LOCTEXT("HeadingLighting", "Lighting")));

	{
		TArray<ESimCopterLumenMode> Modes;
		if (USimCopterSettings::IsHardwareRayTracingAvailable())
		{
			Modes.Add(ESimCopterLumenMode::HardwareRayTracing);
		}
		Modes.Add(ESimCopterLumenMode::Software);
		Modes.Add(ESimCopterLumenMode::Off);

		FRowBinding Lumen;
		Lumen.GetCount = [Modes]() { return Modes.Num(); };
		Lumen.GetOptionLabel = [Modes](const int32 Index)
		{
			return Modes.IsValidIndex(Index)
				? USimCopterSettings::GetLumenModeLabel(Modes[Index])
				: FText::GetEmpty();
		};
		Lumen.GetIndex = [this, Modes]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			return Settings != nullptr ? FMath::Max(Modes.IndexOfByKey(Settings->GetLumenMode()), 0) : 0;
		};
		Lumen.SetIndex = [this, Modes](const int32 Index)
		{
			if (USimCopterSettings* Settings = GetSettings(this); Settings != nullptr && Modes.IsValidIndex(Index))
			{
				Settings->SetLumenMode(Modes[Index]);
				Settings->ApplyAll(nullptr);
			}
		};
		// Greyed in Low Power, which forces the GI method off and would make this row lie. The stored
		// value is left alone, so it comes back exactly as it was when the mode is switched off.
		Lumen.IsEnabled = [this]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			return Settings == nullptr || !Settings->IsLowPowerMode();
		};
		AddRow(BuildDropdownRow(LOCTEXT("Lumen", "Lumen"), Lumen));
	}

	// One knob over everything the remake draws as emissive - the fire and effect cards, the people
	// sprites, the night window lights. They all derive their brightness from the sun rather than
	// carrying an authored value, so scaling them together keeps their relationship to each other.
	AddRow(BuildSliderRow(
		LOCTEXT("EmissiveBrightness", "Emissive Brightness"),
		[this]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			const float Scale = Settings != nullptr ? Settings->GetEmissiveBrightness() : 1.0f;
			return (Scale - USimCopterSettings::EmissiveBrightnessMin)
				/ (USimCopterSettings::EmissiveBrightnessMax - USimCopterSettings::EmissiveBrightnessMin);
		},
		[this](const float Alpha)
		{
			if (USimCopterSettings* Settings = GetSettings(this))
			{
				Settings->SetEmissiveBrightness(FMath::Lerp(
					USimCopterSettings::EmissiveBrightnessMin,
					USimCopterSettings::EmissiveBrightnessMax,
					Alpha));
			}
		},
		[this]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			const float Scale = Settings != nullptr ? Settings->GetEmissiveBrightness() : 1.0f;
			return FText::Format(
				LOCTEXT("PercentFormat", "{0}%"), FText::AsNumber(FMath::RoundToInt(Scale * 100.0f)));
		}));

	AddRow(BuildCheckboxRow(
		LOCTEXT("VolumetricFog", "Volumetric Fog"),
		[this]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			return Settings == nullptr || (Settings->IsVolumetricFogEnabled() && !Settings->IsLowPowerMode());
		},
		[this](const bool bEnabled)
		{
			if (USimCopterSettings* Settings = GetSettings(this))
			{
				Settings->SetVolumetricFogEnabled(bEnabled);
				Settings->ApplyAll(nullptr);
			}
		},
		// Greyed and shown clear in Low Power, which forces r.VolumetricFog 0. The stored value is
		// untouched and comes back when the mode is switched off.
		[this]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			return Settings == nullptr || !Settings->IsLowPowerMode();
		}));

	// ---------------------------------------------------------------------------------------
	// World. The original had no equivalent - a city's time of day came from career.twk's
	// Day/Night column and never moved - so this is entirely the remake's.
	// ---------------------------------------------------------------------------------------

	AddRow(BuildHeading(LOCTEXT("HeadingWorld", "World")));

	{
		FRowBinding TimeOfDay;
		TimeOfDay.GetCount = []() { return 2; };
		TimeOfDay.GetOptionLabel = [](const int32 Index)
		{
			return USimCopterSettings::GetTimeOfDayModeLabel(static_cast<ESimCopterTimeOfDayMode>(Index));
		};
		TimeOfDay.GetIndex = [this]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			return Settings != nullptr ? static_cast<int32>(Settings->GetTimeOfDayMode()) : 0;
		};
		TimeOfDay.SetIndex = [this](const int32 Index)
		{
			if (USimCopterSettings* Settings = GetSettings(this))
			{
				Settings->SetTimeOfDayMode(static_cast<ESimCopterTimeOfDayMode>(Index));
				ApplyTimeOfDay();
			}
		};
		AddRow(BuildDropdownRow(LOCTEXT("TimeOfDayMode", "Time of Day"), TimeOfDay));
	}

	// Greyed in Dynamic, where the clock is the day sequence's to move and this would do nothing.
	AddRow(BuildSliderRow(
		LOCTEXT("StaticTimeOfDay", "Static Time"),
		[this]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			const float Hours = Settings != nullptr ? Settings->GetStaticTimeOfDayHours() : 12.0f;
			return Hours / USimCopterSettings::StaticTimeOfDayMaxHours;
		},
		[this](const float Alpha)
		{
			if (USimCopterSettings* Settings = GetSettings(this))
			{
				Settings->SetStaticTimeOfDayHours(Alpha * USimCopterSettings::StaticTimeOfDayMaxHours);
				ApplyTimeOfDay();
			}
		},
		[this]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			return USimCopterSettings::FormatTimeOfDay(
				Settings != nullptr ? Settings->GetStaticTimeOfDayHours() : 12.0f);
		},
		[this]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			return Settings != nullptr && Settings->GetTimeOfDayMode() == ESimCopterTimeOfDayMode::Static;
		}));

	// The two halves of the cycle get their own real-world durations, which is the whole point of
	// USimCopterDayNightLengthComponent - a day sequence otherwise runs one clock at one speed and
	// splits a 10 minute cycle evenly. Both are greyed in Static, where nothing is moving.
	{
		const auto AddCycleLengthRow = [this, &AddRow](
			const FText& Label,
			TFunction<float(const USimCopterSettings*)> Getter,
			TFunction<void(USimCopterSettings*, float)> Setter,
			const float Fallback)
		{
			// The slider runs 0..1 over the whole allowed range rather than 0..Max, so the bottom of
			// the track is the shortest legal cycle instead of a frozen one.
			const auto ToAlpha = [](const float Minutes)
			{
				return (Minutes - USimCopterSettings::CycleLengthMinMinutes)
					/ (USimCopterSettings::CycleLengthMaxMinutes - USimCopterSettings::CycleLengthMinMinutes);
			};

			AddRow(BuildSliderRow(
				Label,
				[this, Getter, Fallback, ToAlpha]()
				{
					const USimCopterSettings* Settings = GetSettings(this);
					return ToAlpha(Settings != nullptr ? Getter(Settings) : Fallback);
				},
				[this, Setter](const float Alpha)
				{
					if (USimCopterSettings* Settings = GetSettings(this))
					{
						Setter(Settings, FMath::Lerp(
							USimCopterSettings::CycleLengthMinMinutes,
							USimCopterSettings::CycleLengthMaxMinutes,
							Alpha));
						ApplyTimeOfDay();
					}
				},
				[this, Getter, Fallback]()
				{
					const USimCopterSettings* Settings = GetSettings(this);
					return USimCopterSettings::FormatMinutes(Settings != nullptr ? Getter(Settings) : Fallback);
				},
				[this]()
				{
					const USimCopterSettings* Settings = GetSettings(this);
					return Settings != nullptr && Settings->GetTimeOfDayMode() == ESimCopterTimeOfDayMode::Dynamic;
				}));
		};

		AddCycleLengthRow(
			LOCTEXT("DayLength", "Daytime Length"),
			[](const USimCopterSettings* S) { return S->GetDayRealMinutes(); },
			[](USimCopterSettings* S, const float V) { S->SetDayRealMinutes(V); },
			7.0f);

		AddCycleLengthRow(
			LOCTEXT("NightLength", "Nighttime Length"),
			[](const USimCopterSettings* S) { return S->GetNightRealMinutes(); },
			[](USimCopterSettings* S, const float V) { S->SetNightRealMinutes(V); },
			3.0f);
	}

	// ---------------------------------------------------------------------------------------
	// Interface
	// ---------------------------------------------------------------------------------------

	AddRow(BuildHeading(LOCTEXT("HeadingInterface", "Interface")));

	AddRow(BuildSliderRow(
		LOCTEXT("HudScale", "HUD Scale"),
		[this]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			const float Scale = Settings != nullptr ? Settings->GetHudScale() : 1.0f;
			return (Scale - USimCopterSettings::HudScaleMin)
				/ (USimCopterSettings::HudScaleMax - USimCopterSettings::HudScaleMin);
		},
		[this](const float Alpha)
		{
			if (USimCopterSettings* Settings = GetSettings(this))
			{
				Settings->SetHudScale(FMath::Lerp(
					USimCopterSettings::HudScaleMin, USimCopterSettings::HudScaleMax, Alpha));
			}
		},
		[this]()
		{
			const USimCopterSettings* Settings = GetSettings(this);
			const float Scale = Settings != nullptr ? Settings->GetHudScale() : 1.0f;
			return FText::Format(
				LOCTEXT("PercentFormat", "{0}%"), FText::AsNumber(FMath::RoundToInt(Scale * 100.0f)));
		}));

	// ---------------------------------------------------------------------------------------
	// Quality - every scalability group Unreal exposes, plus the overall preset above them.
	// ---------------------------------------------------------------------------------------

	AddRow(BuildHeading(LOCTEXT("HeadingQuality", "Quality")));

	const auto AddQualityRow = [this, &AddRow](
		const FText& Label,
		TFunction<int32(const UGameUserSettings*)> Getter,
		TFunction<void(UGameUserSettings*, int32)> Setter)
	{
		FRowBinding Binding;
		Binding.GetCount = []() { return 5; };   // Low, Medium, High, Epic, Cinematic
		Binding.GetOptionLabel = [](const int32 Index) { return GetQualityLevelLabel(Index); };
		Binding.GetIndex = [Getter]()
		{
			const UGameUserSettings* UserSettings = GetUserSettings();
			return UserSettings != nullptr ? FMath::Clamp(Getter(UserSettings), 0, 4) : 0;
		};
		Binding.SetIndex = [Setter](const int32 Index)
		{
			if (UGameUserSettings* UserSettings = GetUserSettings())
			{
				Setter(UserSettings, Index);
				UserSettings->ApplyNonResolutionSettings();
			}
		};
		AddRow(BuildDropdownRow(Label, Binding));
	};

	// The overall preset drives all ten at once; it reads back as Custom (-1) once one is moved,
	// which is why its own row allows a sixth label the others do not.
	{
		FRowBinding Overall;
		Overall.GetCount = []() { return 5; };
		Overall.GetOptionLabel = [](const int32 Index) { return GetQualityLevelLabel(Index); };
		Overall.GetIndex = []()
		{
			const UGameUserSettings* UserSettings = GetUserSettings();
			return UserSettings != nullptr ? FMath::Clamp(UserSettings->GetOverallScalabilityLevel(), 0, 4) : 0;
		};
		Overall.SetIndex = [](const int32 Index)
		{
			if (UGameUserSettings* UserSettings = GetUserSettings())
			{
				UserSettings->SetOverallScalabilityLevel(Index);
				UserSettings->ApplyNonResolutionSettings();
			}
		};
		AddRow(BuildDropdownRow(LOCTEXT("OverallQuality", "Overall Quality"), Overall));
	}

	AddQualityRow(LOCTEXT("ViewDistance", "View Distance"),
		[](const UGameUserSettings* S) { return S->GetViewDistanceQuality(); },
		[](UGameUserSettings* S, const int32 V) { S->SetViewDistanceQuality(V); });
	AddQualityRow(LOCTEXT("AntiAliasing", "Anti-Aliasing"),
		[](const UGameUserSettings* S) { return S->GetAntiAliasingQuality(); },
		[](UGameUserSettings* S, const int32 V) { S->SetAntiAliasingQuality(V); });
	AddQualityRow(LOCTEXT("PostProcessing", "Post Processing"),
		[](const UGameUserSettings* S) { return S->GetPostProcessingQuality(); },
		[](UGameUserSettings* S, const int32 V) { S->SetPostProcessingQuality(V); });
	AddQualityRow(LOCTEXT("Shadows", "Shadows"),
		[](const UGameUserSettings* S) { return S->GetShadowQuality(); },
		[](UGameUserSettings* S, const int32 V) { S->SetShadowQuality(V); });
	AddQualityRow(LOCTEXT("GlobalIllumination", "Global Illumination"),
		[](const UGameUserSettings* S) { return S->GetGlobalIlluminationQuality(); },
		[](UGameUserSettings* S, const int32 V) { S->SetGlobalIlluminationQuality(V); });
	AddQualityRow(LOCTEXT("Reflections", "Reflections"),
		[](const UGameUserSettings* S) { return S->GetReflectionQuality(); },
		[](UGameUserSettings* S, const int32 V) { S->SetReflectionQuality(V); });
	AddQualityRow(LOCTEXT("Textures", "Textures"),
		[](const UGameUserSettings* S) { return S->GetTextureQuality(); },
		[](UGameUserSettings* S, const int32 V) { S->SetTextureQuality(V); });
	AddQualityRow(LOCTEXT("Effects", "Effects"),
		[](const UGameUserSettings* S) { return S->GetVisualEffectQuality(); },
		[](UGameUserSettings* S, const int32 V) { S->SetVisualEffectQuality(V); });
	AddQualityRow(LOCTEXT("Foliage", "Foliage"),
		[](const UGameUserSettings* S) { return S->GetFoliageQuality(); },
		[](UGameUserSettings* S, const int32 V) { S->SetFoliageQuality(V); });
	AddQualityRow(LOCTEXT("Shading", "Shading"),
		[](const UGameUserSettings* S) { return S->GetShadingQuality(); },
		[](UGameUserSettings* S, const int32 V) { S->SetShadingQuality(V); });
}

void SSimCopterGraphicsSettings::ApplyTimeOfDay()
{
	const UWorld* World = (GEngine != nullptr && GEngine->GameViewport != nullptr)
		? GEngine->GameViewport->GetWorld()
		: nullptr;
	if (USimCopterDayNightSubsystem* DayNight = World != nullptr
		? World->GetSubsystem<USimCopterDayNightSubsystem>()
		: nullptr)
	{
		DayNight->ApplyTimeOfDaySettings();
	}
}

void SSimCopterGraphicsSettings::Accept()
{
	if (UGameUserSettings* UserSettings = GetUserSettings())
	{
		// The rows already applied as they were changed; OK is what makes it permanent. Confirming
		// the video mode is what stops a later RevertVideoMode snapping back to whatever the window
		// happened to be when the page opened.
		UserSettings->ConfirmVideoMode();
		UserSettings->SaveSettings();
	}
	if (USimCopterSettings* Settings = GetSettings(this))
	{
		Settings->ApplyAll(nullptr);
		Settings->Save();
	}

	OnAccepted.ExecuteIfBound();
}

void SSimCopterGraphicsSettings::CaptureEnteredState()
{
	if (const USimCopterSettings* Settings = GetSettings(this))
	{
		Entered.bLowPowerMode = Settings->IsLowPowerMode();
		Entered.bDlssEnabled = Settings->IsDlssEnabled();
		Entered.DlssQuality = static_cast<uint8>(Settings->GetDlssQuality());
		Entered.FrameGenMode = static_cast<uint8>(Settings->GetFrameGenMode());
		Entered.FrameGenMultiple = Settings->GetFrameGenMultiple();
		Entered.ReflexMode = static_cast<uint8>(Settings->GetReflexMode());
		Entered.LumenMode = static_cast<uint8>(Settings->GetLumenMode());
		Entered.AntiAliasingMethod = static_cast<uint8>(Settings->GetAntiAliasingMethod());
		Entered.bVolumetricFog = Settings->IsVolumetricFogEnabled();
		Entered.EmissiveBrightness = Settings->GetEmissiveBrightness();
		Entered.TimeOfDayMode = static_cast<uint8>(Settings->GetTimeOfDayMode());
		Entered.StaticTimeOfDayHours = Settings->GetStaticTimeOfDayHours();
		Entered.DayRealMinutes = Settings->GetDayRealMinutes();
		Entered.NightRealMinutes = Settings->GetNightRealMinutes();
		Entered.HudScale = Settings->GetHudScale();
	}

	const UGameUserSettings* UserSettings = GetUserSettings();
	if (UserSettings == nullptr)
	{
		return;
	}

	Entered.Resolution = UserSettings->GetScreenResolution();
	Entered.WindowMode = static_cast<int32>(UserSettings->GetFullscreenMode());
	Entered.bVSync = UserSettings->IsVSyncEnabled();
	Entered.FrameRateLimit = UserSettings->GetFrameRateLimit();
	Entered.ResolutionScale = UserSettings->GetResolutionScaleNormalized();
	Entered.Quality[0] = UserSettings->GetViewDistanceQuality();
	Entered.Quality[1] = UserSettings->GetAntiAliasingQuality();
	Entered.Quality[2] = UserSettings->GetPostProcessingQuality();
	Entered.Quality[3] = UserSettings->GetShadowQuality();
	Entered.Quality[4] = UserSettings->GetGlobalIlluminationQuality();
	Entered.Quality[5] = UserSettings->GetReflectionQuality();
	Entered.Quality[6] = UserSettings->GetTextureQuality();
	Entered.Quality[7] = UserSettings->GetVisualEffectQuality();
	Entered.Quality[8] = UserSettings->GetFoliageQuality();
	Entered.Quality[9] = UserSettings->GetShadingQuality();
}

void SSimCopterGraphicsSettings::RestoreEnteredState()
{
	if (USimCopterSettings* Settings = GetSettings(this))
	{
		// Before the rest: ApplyAll below re-runs the mode's scalability transition, and the explicit
		// quality and resolution-scale restores after it are what settle the final state either way.
		Settings->SetLowPowerMode(Entered.bLowPowerMode);
		Settings->SetDlssEnabled(Entered.bDlssEnabled);
		Settings->SetDlssQuality(static_cast<ESimCopterDlssQuality>(Entered.DlssQuality));
		Settings->SetFrameGenMode(static_cast<ESimCopterFrameGenMode>(Entered.FrameGenMode));
		Settings->SetFrameGenMultiple(Entered.FrameGenMultiple);
		Settings->SetReflexMode(static_cast<ESimCopterReflexMode>(Entered.ReflexMode));
		Settings->SetLumenMode(static_cast<ESimCopterLumenMode>(Entered.LumenMode));
		Settings->SetAntiAliasingMethod(static_cast<ESimCopterAntiAliasingMethod>(Entered.AntiAliasingMethod));
		Settings->SetVolumetricFogEnabled(Entered.bVolumetricFog);
		Settings->SetEmissiveBrightness(Entered.EmissiveBrightness);
		Settings->SetTimeOfDayMode(static_cast<ESimCopterTimeOfDayMode>(Entered.TimeOfDayMode));
		Settings->SetStaticTimeOfDayHours(Entered.StaticTimeOfDayHours);
		Settings->SetDayRealMinutes(Entered.DayRealMinutes);
		Settings->SetNightRealMinutes(Entered.NightRealMinutes);
		Settings->ApplyAll(nullptr);
		ApplyTimeOfDay();
	}

	UGameUserSettings* UserSettings = GetUserSettings();
	if (UserSettings == nullptr)
	{
		return;
	}

	UserSettings->SetVSyncEnabled(Entered.bVSync);
	UserSettings->SetFrameRateLimit(Entered.FrameRateLimit);
	UserSettings->SetResolutionScaleNormalized(Entered.ResolutionScale);
	UserSettings->SetViewDistanceQuality(Entered.Quality[0]);
	UserSettings->SetAntiAliasingQuality(Entered.Quality[1]);
	UserSettings->SetPostProcessingQuality(Entered.Quality[2]);
	UserSettings->SetShadowQuality(Entered.Quality[3]);
	UserSettings->SetGlobalIlluminationQuality(Entered.Quality[4]);
	UserSettings->SetReflectionQuality(Entered.Quality[5]);
	UserSettings->SetTextureQuality(Entered.Quality[6]);
	UserSettings->SetVisualEffectQuality(Entered.Quality[7]);
	UserSettings->SetFoliageQuality(Entered.Quality[8]);
	UserSettings->SetShadingQuality(Entered.Quality[9]);
	UserSettings->ApplyNonResolutionSettings();

	// Only touch the window when the page actually moved it: re-applying a resolution the player
	// never changed resizes the window for nothing, and would undo a command-line override.
	const bool bResolutionMoved =
		UserSettings->GetScreenResolution() != Entered.Resolution ||
		static_cast<int32>(UserSettings->GetFullscreenMode()) != Entered.WindowMode;
	if (bResolutionMoved)
	{
		UserSettings->SetScreenResolution(Entered.Resolution);
		UserSettings->SetFullscreenMode(static_cast<EWindowMode::Type>(Entered.WindowMode));
		UserSettings->ApplyResolutionSettings(/*bCheckForCommandLineOverrides=*/false);
	}
}

void SSimCopterGraphicsSettings::Cancel()
{
	// The rows apply as they are changed so the player can see what a setting does, which means
	// Cancel has to put back what was there on entry.
	RestoreEnteredState();
	OnCancelled.ExecuteIfBound();
}

FReply SSimCopterGraphicsSettings::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
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
