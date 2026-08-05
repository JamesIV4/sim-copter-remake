// Copyright Epic Games, Inc. All Rights Reserved.

#include "SimCopterFrontEndPage.h"

#include "Audio/SimCopterAudioSubsystem.h"
#include "InputCoreTypes.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Text/STextBlock.h"

namespace SimCopterFrontEnd
{
namespace
{
const TCHAR* const ButtonStrip = TEXT("BUTTON.BMP");
constexpr int32 ButtonFrameCount = 3;

// Text printed into the original's dark wells.
const FLinearColor ButtonText(0.94f, 0.94f, 0.90f, 1.0f);

USimCopterAudioSubsystem* GetAudio()
{
	const UWorld* World = (GEngine != nullptr && GEngine->GameViewport != nullptr)
		? GEngine->GameViewport->GetWorld()
		: nullptr;
	return World != nullptr ? World->GetSubsystem<USimCopterAudioSubsystem>() : nullptr;
}
}

FSlateFontInfo PageFont(const int32 WindowsHeight, const bool bBold)
{
	return FCoreStyle::GetDefaultFontStyle(
		bBold ? TEXT("Bold") : TEXT("Regular"),
		WindowsHeightToSlatePoints(WindowsHeight));
}

void AddAt(const TSharedRef<SConstraintCanvas>& Canvas, const FRect& Rect, TSharedRef<SWidget> Widget)
{
	Canvas->AddSlot()
		.Offset(FMargin(Rect.Left, Rect.Top, Rect.Width(), Rect.Height()))
		.Anchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f))
		.Alignment(FVector2D::ZeroVector)
		[
			Widget
		];
}

TSharedRef<SWidget> MakePageImage(USimCopterHangarArt* Art, const FString& FileName)
{
	// Page backgrounds load opaque; only the original's sprite bitmaps are colour keyed. main1.bmp
	// is the exception - its corner notch and the space around the panel are palette index 254 -
	// so the caller passes the page through GetBitmap with the key on where it needs it.
	const FSlateBrush* Brush = Art != nullptr ? Art->GetBitmap(FileName, /*bColorKeyed=*/true) : nullptr;
	if (Brush != nullptr)
	{
		return SNew(SImage).Image(Brush);
	}

	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(FLinearColor(0.13f, 0.14f, 0.15f, 1.0f));
}

TSharedRef<SWidget> MakeScaledScreen(TSharedRef<SWidget> PageContent)
{
	return SNew(SScaleBox)
		.Stretch(EStretch::ScaleToFit)
		[
			SNew(SBox)
			.WidthOverride(ScreenWidth)
			.HeightOverride(ScreenHeight)
			[
				PageContent
			]
		];
}

TSharedRef<SWidget> MakeButton(
	USimCopterHangarArt* Art,
	const FText& Label,
	const int32 WindowsFontHeight,
	FOnClicked OnClicked,
	TArray<TSharedRef<FButtonStyle>>& StyleKeepAlive)
{
	TSharedRef<SButton> Button = SNew(SButton)
		.OnClicked(OnClicked)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.ContentPadding(FMargin(0.0f))
		[
			SNew(STextBlock)
			.Text(Label)
			.Justification(ETextJustify::Center)
			.Font(PageFont(WindowsFontHeight, /*bBold=*/true))
			.ColorAndOpacity(FSlateColor(ButtonText))
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f))
		];

	const FSlateBrush* Normal = Art != nullptr ? Art->GetStripFrame(ButtonStrip, 2, ButtonFrameCount) : nullptr;
	if (Normal != nullptr)
	{
		const FSlateBrush* Hovered = Art->GetStripFrame(ButtonStrip, 1, ButtonFrameCount);
		const FSlateBrush* Pressed = Art->GetStripFrame(ButtonStrip, 0, ButtonFrameCount);

		TSharedRef<FButtonStyle> Style = MakeShared<FButtonStyle>();
		Style->SetNormal(*Normal);
		Style->SetHovered(Hovered != nullptr ? *Hovered : *Normal);
		Style->SetPressed(Pressed != nullptr ? *Pressed : *Normal);
		Style->SetDisabled(*Normal);
		Style->SetNormalPadding(FMargin(0.0f));
		Style->SetPressedPadding(FMargin(0.0f));
		StyleKeepAlive.Add(Style);
		Button->SetButtonStyle(&Style.Get());
	}

	return Button;
}

TSharedRef<SWidget> MakeInvisibleHitButton(
	FOnClicked OnClicked,
	FSimpleDelegate OnHovered,
	TArray<TSharedRef<FButtonStyle>>& StyleKeepAlive)
{
	TSharedRef<FButtonStyle> Style = MakeShared<FButtonStyle>();
	Style->SetNormal(FSlateNoResource());
	Style->SetHovered(FSlateNoResource());
	Style->SetPressed(FSlateNoResource());
	Style->SetDisabled(FSlateNoResource());
	Style->SetNormalPadding(FMargin(0.0f));
	Style->SetPressedPadding(FMargin(0.0f));
	StyleKeepAlive.Add(Style);

	return SNew(SButton)
		.ButtonStyle(&Style.Get())
		.ContentPadding(FMargin(0.0f))
		.OnClicked(OnClicked)
		.OnHovered(OnHovered);
}

int32 GetNavigationTarget(const ENavigation Navigation, const int32 Selected, const int32 Count)
{
	if (Count <= 0)
	{
		return INDEX_NONE;
	}

	switch (Navigation)
	{
	case ENavigation::Next:
		// FUN_0045f040: `if (count - 1 > selected) selected + 1 else 0`.
		return (Count - 1 > Selected) ? Selected + 1 : 0;

	case ENavigation::Previous:
		// `if (selected == 0) count - 1 else selected - 1`.
		return (Selected == 0) ? Count - 1 : Selected - 1;

	case ENavigation::PreviousNoWrap:
		// Page Up only moves while the selection is above the first item; the original falls
		// straight through to the Home/End tests otherwise, which do not match, so nothing happens.
		return (Selected > 0) ? Selected - 1 : INDEX_NONE;

	case ENavigation::First:
		return 0;

	case ENavigation::Last:
		return Count - 1;

	default:
		return INDEX_NONE;
	}
}

ENavigation GetNavigationForKey(const FKey& Key)
{
	// FUN_0045f040's tests, in its own order.
	if (Key == EKeys::Down || Key == EKeys::PageDown) { return ENavigation::Next; }
	if (Key == EKeys::Up)                             { return ENavigation::Previous; }
	if (Key == EKeys::PageUp)                         { return ENavigation::PreviousNoWrap; }
	if (Key == EKeys::Home)                           { return ENavigation::First; }
	if (Key == EKeys::End)                            { return ENavigation::Last; }
	return ENavigation::None;
}

void PlayScreenSound(const TCHAR* WavName)
{
	if (USimCopterAudioSubsystem* Audio = GetAudio())
	{
		Audio->PlayFile2D(WavName, SimCopterSound::ESoundDir::Root);
	}
}

void PlayScreenMusic(const TCHAR* WavName)
{
	if (USimCopterAudioSubsystem* Audio = GetAudio())
	{
		Audio->PlayMusicFile2D(WavName, SimCopterSound::ESoundDir::Root);
	}
}

void StopScreenMusic()
{
	if (USimCopterAudioSubsystem* Audio = GetAudio())
	{
		Audio->StopMusic();
	}
}
}
