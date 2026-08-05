// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SSimCopterMainMenu.h"

#include "InputCoreTypes.h"
#include "SimCopterFrontEndPage.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimCopterMainMenu"

using namespace SimCopterFrontEnd;
using namespace SimCopterMainMenuLayout;

namespace
{
const TCHAR* const MenuPage = TEXT("MAIN1.BMP");
const TCHAR* const HoseTop = TEXT("MAIN2.BMP");
const TCHAR* const HoseCorner = TEXT("MAIN3.BMP");
const TCHAR* const LampStrip = TEXT("MAIN4.BMP");
const TCHAR* const KeyStrip = TEXT("MAIN5.BMP");
const TCHAR* const FallbackCloudStrip = TEXT("SKYCOOL.BMP");

const TCHAR* const UpscaledMenuPage = TEXT("MAIN1-upscaled-rows-off.png");
const TCHAR* const UpscaledHoseTop = TEXT("MAIN2-upscaled.png");
const TCHAR* const UpscaledHoseCorner = TEXT("MAIN3-upscaled.png");

// The selection tick FUN_0045ed60 plays, and the looping backing track FUN_0045f3d0 starts.
const TCHAR* const SelectionSound = TEXT("menu");
const TCHAR* const MenuMusic = TEXT("menuback");

// SCHOOK: MainMenuSkyMovie 0x0044d070 / MENUSKY.SMK. The exact movie remains centred under the
// original 640x480 page. Its largest always-visible sky opening repeats behind it at the same
// live frame, extending the animation into aspect-ratio margins without stretching the original
// composition. SKYCOOL remains only as a missing-original-data fallback.
class SSimCopterMenuCloudBackdrop final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterMenuCloudBackdrop) {}
		SLATE_ARGUMENT(const FSlateBrush*, MovieBrush)
		SLATE_ARGUMENT(const FSlateBrush*, FallbackCloudBrush)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		MovieBrush = Args._MovieBrush;
		FallbackCloudBrush = Args._FallbackCloudBrush;
		if (MovieBrush != nullptr)
		{
			const float U0 = SimCopterMenuSkyLayout::ExtensionSource.Left / SimCopterMenuSkyLayout::MovieWidth;
			const float V0 = SimCopterMenuSkyLayout::ExtensionSource.Top / SimCopterMenuSkyLayout::MovieHeight;
			const float U1 = SimCopterMenuSkyLayout::ExtensionSource.Right / SimCopterMenuSkyLayout::MovieWidth;
			const float V1 = SimCopterMenuSkyLayout::ExtensionSource.Bottom / SimCopterMenuSkyLayout::MovieHeight;

			ExtensionBrush = *MovieBrush;
			ExtensionBrush.SetUVRegion(FBox2f(FVector2f(U0, V0), FVector2f(U1, V1)));

			FlippedExtensionBrush = *MovieBrush;
			FlippedExtensionBrush.SetUVRegion(FBox2f(FVector2f(U1, V0), FVector2f(U0, V1)));
		}
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double CurrentTime, const float DeltaTime) override
	{
		SLeafWidget::Tick(AllottedGeometry, CurrentTime, DeltaTime);
		if (MovieBrush == nullptr)
		{
			ScrollPixels = FMath::Fmod(ScrollPixels + DeltaTime * 5.0f, SimCopterFrontEnd::ScreenWidth);
		}
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		const int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		const bool bParentEnabled) const override
	{
		const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());
		const FSlateBrush* BaseBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform()),
			BaseBrush,
			ESlateDrawEffect::None,
			FLinearColor(0.28f, 0.58f, 0.72f, 1.0f));

		if (MovieBrush != nullptr)
		{
			const float TileWidth = FMath::Max(
				1.0f,
				SimCopterMenuSkyLayout::GetExtensionTileWidth(static_cast<float>(Size.Y)));
			const int32 TileCount = FMath::Max(1, FMath::CeilToInt(static_cast<float>(Size.X) / TileWidth));
			for (int32 Tile = 0; Tile < TileCount; ++Tile)
			{
				const FSlateBrush* BrushToUse = (Tile % 2 == 0) ? &ExtensionBrush : &FlippedExtensionBrush;
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					LayerId + 1,
					AllottedGeometry.ToPaintGeometry(
						FVector2f(TileWidth, Size.Y),
						FSlateLayoutTransform(FVector2f(static_cast<float>(Tile) * TileWidth, 0.0f))),
					BrushToUse,
					ESlateDrawEffect::None,
					InWidgetStyle.GetColorAndOpacityTint());
			}

			const FRect MovieRect = SimCopterMenuSkyLayout::GetCenteredMovieRect(Size.X, Size.Y);
			const float BlendWidth = FMath::Min(64.0f, MovieRect.Width() * 0.25f);

			if (MovieRect.Left > 0.0f && BlendWidth > 0.0f)
			{
				const int32 BlendSteps = 32;
				const float StepWidth = BlendWidth / static_cast<float>(BlendSteps);
				const FLinearColor BaseTint = InWidgetStyle.GetColorAndOpacityTint();

				// Left transition blend (Alpha 0 -> 1)
				for (int32 Step = 0; Step < BlendSteps; ++Step)
				{
					const float X0 = MovieRect.Left + static_cast<float>(Step) * StepWidth;
					const float X1 = X0 + StepWidth;
					const float Progress = (static_cast<float>(Step) + 0.5f) / static_cast<float>(BlendSteps);
					const float Alpha = 0.5f * (1.0f - FMath::Cos(UE_PI * Progress));

					FSlateBrush SliceBrush = *MovieBrush;
					const float U0 = (X0 - MovieRect.Left) / MovieRect.Width();
					const float U1 = (X1 - MovieRect.Left) / MovieRect.Width();
					SliceBrush.SetUVRegion(FBox2f(FVector2f(U0, 0.0f), FVector2f(U1, 1.0f)));

					FSlateDrawElement::MakeBox(
						OutDrawElements,
						LayerId + 2,
						AllottedGeometry.ToPaintGeometry(
							FVector2f(StepWidth, MovieRect.Height()),
							FSlateLayoutTransform(FVector2f(X0, MovieRect.Top))),
						&SliceBrush,
						ESlateDrawEffect::None,
						BaseTint * FLinearColor(1.0f, 1.0f, 1.0f, Alpha));
				}

				// Opaque center region
				const float CenterLeft = MovieRect.Left + BlendWidth;
				const float CenterRight = MovieRect.Right - BlendWidth;
				const float CenterWidth = CenterRight - CenterLeft;
				if (CenterWidth > 0.0f)
				{
					FSlateBrush CenterBrush = *MovieBrush;
					const float U0 = BlendWidth / MovieRect.Width();
					const float U1 = 1.0f - U0;
					CenterBrush.SetUVRegion(FBox2f(FVector2f(U0, 0.0f), FVector2f(U1, 1.0f)));

					FSlateDrawElement::MakeBox(
						OutDrawElements,
						LayerId + 2,
						AllottedGeometry.ToPaintGeometry(
							FVector2f(CenterWidth, MovieRect.Height()),
							FSlateLayoutTransform(FVector2f(CenterLeft, MovieRect.Top))),
						&CenterBrush,
						ESlateDrawEffect::None,
						BaseTint);
				}

				// Right transition blend (Alpha 1 -> 0)
				for (int32 Step = 0; Step < BlendSteps; ++Step)
				{
					const float X0 = MovieRect.Right - BlendWidth + static_cast<float>(Step) * StepWidth;
					const float X1 = X0 + StepWidth;
					const float Progress = (static_cast<float>(Step) + 0.5f) / static_cast<float>(BlendSteps);
					const float Alpha = 0.5f * (1.0f + FMath::Cos(UE_PI * Progress));

					FSlateBrush SliceBrush = *MovieBrush;
					const float U0 = (X0 - MovieRect.Left) / MovieRect.Width();
					const float U1 = (X1 - MovieRect.Left) / MovieRect.Width();
					SliceBrush.SetUVRegion(FBox2f(FVector2f(U0, 0.0f), FVector2f(U1, 1.0f)));

					FSlateDrawElement::MakeBox(
						OutDrawElements,
						LayerId + 2,
						AllottedGeometry.ToPaintGeometry(
							FVector2f(StepWidth, MovieRect.Height()),
							FSlateLayoutTransform(FVector2f(X0, MovieRect.Top))),
						&SliceBrush,
						ESlateDrawEffect::None,
						BaseTint * FLinearColor(1.0f, 1.0f, 1.0f, Alpha));
				}
			}
			else
			{
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					LayerId + 2,
					AllottedGeometry.ToPaintGeometry(
						FVector2f(MovieRect.Width(), MovieRect.Height()),
						FSlateLayoutTransform(FVector2f(MovieRect.Left, MovieRect.Top))),
					MovieBrush,
					ESlateDrawEffect::None,
					InWidgetStyle.GetColorAndOpacityTint());
			}

			return LayerId + 2;
		}

		if (FallbackCloudBrush != nullptr)
		{
			const float Width = FMath::Max(1.0f, static_cast<float>(Size.X));
			FSlateBrush FlippedFallback = *FallbackCloudBrush;
			FlippedFallback.SetUVRegion(FBox2f(FVector2f(1.0f, 0.0f), FVector2f(0.0f, 1.0f)));

			for (int32 Copy = -1; Copy <= 1; ++Copy)
			{
				const float X = static_cast<float>(Copy) * Width - ScrollPixels;
				const FSlateBrush* BrushToUse = (FMath::Abs(Copy) % 2 == 0) ? FallbackCloudBrush : &FlippedFallback;
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					LayerId + 1,
					AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(FVector2f(X, 0.0f))),
					BrushToUse,
					ESlateDrawEffect::None,
					InWidgetStyle.GetColorAndOpacityTint());
			}
			return LayerId + 1;
		}
		return LayerId;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(SimCopterFrontEnd::ScreenWidth, SimCopterFrontEnd::ScreenHeight);
	}

private:
	const FSlateBrush* MovieBrush = nullptr;
	const FSlateBrush* FallbackCloudBrush = nullptr;
	FSlateBrush ExtensionBrush;
	FSlateBrush FlippedExtensionBrush;
	float ScrollPixels = 0.0f;
};
}

void SSimCopterMainMenu::Construct(const FArguments& InArgs)
{
	Art = InArgs._Art;
	OnItemChosen = InArgs._OnItemChosen;

	USimCopterHangarArt* ArtObject = Art;
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);
	const FSlateBrush* MovieBrush = ArtObject != nullptr
		? ArtObject->GetMenuSkyMovieBrush()
		: nullptr;
	const FSlateBrush* FallbackCloudBrush = ArtObject != nullptr
		? ArtObject->GetBitmap(FallbackCloudStrip, /*bColorKeyed=*/false)
		: nullptr;

	const FSlateBrush* UpscaledPageBrush = ArtObject != nullptr
		? ArtObject->GetBundledSlateImage(UpscaledMenuPage)
		: nullptr;
	if (UpscaledPageBrush == nullptr && ArtObject != nullptr)
	{
		UpscaledPageBrush = ArtObject->GetBundledSlateImage(TEXT("MAIN1-upscaled.png"));
	}

	TArray<const FSlateBrush*> UpscaledRowOnBrushes;
	if (ArtObject != nullptr)
	{
		for (int32 Index = 0; Index < ItemCount; ++Index)
		{
			const FString RowFileName = FString::Printf(TEXT("MAIN1-upscaled-row%d-on.png"), Index + 1);
			if (const FSlateBrush* RowBrush = ArtObject->GetBundledSlateImage(RowFileName))
			{
				UpscaledRowOnBrushes.Add(RowBrush);
			}
		}
	}

	const bool bUseUpscaledArt = (UpscaledPageBrush != nullptr && UpscaledRowOnBrushes.Num() == ItemCount);

	if (bUseUpscaledArt)
	{
		AddAt(
			Canvas,
			FRect{ PageX, PageY, PageX + PageWidth, PageY + PageHeight },
			SNew(SImage).Image(UpscaledPageBrush));
	}
	else
	{
		AddAt(
			Canvas,
			FRect{ PageX, PageY, PageX + PageWidth, PageY + PageHeight },
			MakePageImage(ArtObject, MenuPage));
	}

	if (ArtObject != nullptr)
	{
		const FSlateBrush* HoseTopBrush = ArtObject->GetBundledSlateImage(UpscaledHoseTop);
		if (HoseTopBrush == nullptr)
		{
			HoseTopBrush = ArtObject->GetBitmap(HoseTop, /*bColorKeyed=*/true);
		}
		if (HoseTopBrush != nullptr)
		{
			AddAt(Canvas, FRect{ HoseTopX, HoseTopY, HoseTopX + HoseTopWidth, HoseTopY + HoseTopHeight },
				SNew(SImage).Image(HoseTopBrush));
		}

		const FSlateBrush* HoseCornerBrush = ArtObject->GetBundledSlateImage(UpscaledHoseCorner);
		if (HoseCornerBrush == nullptr)
		{
			HoseCornerBrush = ArtObject->GetBitmap(HoseCorner, /*bColorKeyed=*/true);
		}
		if (HoseCornerBrush != nullptr)
		{
			AddAt(
				Canvas,
				FRect{ HoseCornerX, HoseCornerY, HoseCornerX + HoseCornerWidth, HoseCornerY + HoseCornerHeight },
				SNew(SImage).Image(HoseCornerBrush));
		}
	}

	if (bUseUpscaledArt)
	{
		// Instead of MAIN4.BMP and MAIN5.BMP strips, overlay MAIN1-upscaled-row1-on.png .. row5-on.png
		// for whichever row is currently selected.
		AddAt(
			Canvas,
			FRect{ PageX, PageY, PageX + PageWidth, PageY + PageHeight },
			SNew(SImage)
			.Visibility(EVisibility::HitTestInvisible)
			.Image_Lambda([this, UpscaledRowOnBrushes]()
			{
				return (SelectedIndex >= 0 && SelectedIndex < UpscaledRowOnBrushes.Num())
					? UpscaledRowOnBrushes[SelectedIndex]
					: nullptr;
			}));
	}
	else
	{
		// Original fallback: lamp & key strips from MAIN4.BMP and MAIN5.BMP
		for (int32 Index = 0; Index < ItemCount; ++Index)
		{
			const auto AddStripCell = [&](
				const TCHAR* FileName,
				const float PageLeft,
				const float PageTop,
				const float ColumnWidth,
				const float SourceTop,
				const float SourceBottom)
			{
				if (ArtObject == nullptr)
				{
					return;
				}

				const int32 Top = FMath::RoundToInt(SourceTop);
				const int32 Bottom = FMath::RoundToInt(SourceBottom);
				const int32 Width = FMath::RoundToInt(ColumnWidth);
				const FSlateBrush* Off = ArtObject->GetSubImage(FileName, FIntRect(0, Top, Width, Bottom), /*bColorKeyed=*/false);
				const FSlateBrush* On = ArtObject->GetSubImage(FileName, FIntRect(Width, Top, Width * 2, Bottom), /*bColorKeyed=*/false);
				if (Off == nullptr)
				{
					return;
				}

				AddAt(
					Canvas,
					FRect{ PageX + PageLeft, PageY + PageTop, PageX + PageLeft + ColumnWidth, PageY + PageTop + (SourceBottom - SourceTop) },
					SNew(SImage)
					.Visibility(EVisibility::HitTestInvisible)
					.Image_Lambda([this, Index, Off, On]()
					{
						return (SelectedIndex == Index && On != nullptr) ? On : Off;
					}));
			};

			AddStripCell(KeyStrip, KeyX, KeyTop[Index], KeyColumnWidth, KeySourceTop[Index], KeySourceBottom[Index]);
			AddStripCell(LampStrip, LampX, LampTop[Index], LampColumnWidth, LampSourceTop[Index], LampSourceBottom[Index]);
		}
	}

	// The item labels, then a transparent hit row over each one. The label goes in first so the
	// hit row is on top and gets the pointer.
	for (int32 Index = 0; Index < ItemCount; ++Index)
	{
		const FRect Text = GetItemTextRect(Index);
		AddAt(
			Canvas,
			FRect{ PageX + Text.Left, PageY + Text.Top, PageX + Text.Right, PageY + Text.Bottom },
			SNew(STextBlock)
			.Text(GetItemLabel(Index))
			.Visibility(EVisibility::HitTestInvisible)
			.Font(PageFont(ItemFontHeight, /*bBold=*/true))
			.ColorAndOpacity_Lambda([this, Index]()
			{
				return FSlateColor(SelectedIndex == Index ? ItemSelectedColor : ItemColor);
			}));
	}

	for (int32 Index = 0; Index < ItemCount; ++Index)
	{
		const FRect Hit = GetItemHitRect(Index);
		AddAt(
			Canvas,
			FRect{ PageX + Hit.Left, PageY + Hit.Top, PageX + Hit.Right, PageY + Hit.Bottom },
			MakeInvisibleHitButton(
				FOnClicked::CreateLambda([this, Index]()
				{
					// FUN_0045f1a0: a click selects what is under the cursor and then activates it.
					SetSelectedIndex(Index);
					ActivateSelected();
					return FReply::Handled();
				}),
				FSimpleDelegate::CreateLambda([this, Index]()
				{
					// FUN_0045f210: moving over an item selects it.
					SetSelectedIndex(Index);
				}),
				ButtonStyles));
	}

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SSimCopterMenuCloudBackdrop)
			.MovieBrush(MovieBrush)
			.FallbackCloudBrush(FallbackCloudBrush)
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			MakeScaledScreen(Canvas)
		]
	];

	PlayScreenMusic(MenuMusic);
}

const FText& SSimCopterMainMenu::GetItemLabel(const int32 Index)
{
	// STRINGTABLE 55..59, English. Resources rather than .rdata, same as the rest of the shell.
	static const FText Labels[ItemCount] = {
		LOCTEXT("NewCareerGame", "New Career Game"),   // 55
		LOCTEXT("OpenCareerGame", "Open Career Game"), // 56
		LOCTEXT("NewUserGame", "New User Game"),       // 57
		LOCTEXT("OpenUserGame", "Open User Game"),     // 58
		LOCTEXT("Quit", "Quit"),                       // 59
	};

	static const FText Empty = FText::GetEmpty();
	return (Index >= 0 && Index < ItemCount) ? Labels[Index] : Empty;
}

void SSimCopterMainMenu::SetSelectedIndex(const int32 Index)
{
	if (Index < 0 || Index >= ItemCount || Index == SelectedIndex)
	{
		return;
	}

	SelectedIndex = Index;
	PlayScreenSound(SelectionSound);
}

void SSimCopterMainMenu::ActivateSelected()
{
	// FUN_0045f3d0's [vt+0xfc] stops menuback.wav as the page goes away; the host swaps the
	// screen out from under us in the delegate, so stop it first.
	StopScreenMusic();
	OnItemChosen.ExecuteIfBound(static_cast<ESimCopterMainMenuItem>(SelectedIndex));
}

void SSimCopterMainMenu::Navigate(const ENavigation Navigation)
{
	const int32 Target = GetNavigationTarget(Navigation, SelectedIndex, ItemCount);
	if (Target != INDEX_NONE)
	{
		SetSelectedIndex(Target);
	}
}

bool SSimCopterMainMenu::SelectByMnemonic(const TCHAR Character)
{
	// FUN_0045eed0 compares the key against the *first byte* of each item's text and stops at the
	// first match, so "New Career Game" always wins over "New User Game" on N.
	for (int32 Index = 0; Index < ItemCount; ++Index)
	{
		const FString Label = GetItemLabel(Index).ToString();
		if (Label.Len() > 0 && FChar::ToUpper(Label[0]) == FChar::ToUpper(Character))
		{
			SetSelectedIndex(Index);
			return true;
		}
	}

	return false;
}

FReply SSimCopterMainMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	if (const ENavigation Navigation = GetNavigationForKey(Key); Navigation != ENavigation::None)
	{
		Navigate(Navigation);
		return FReply::Handled();
	}
	if (Key == EKeys::Enter)
	{
		ActivateSelected();
		return FReply::Handled();
	}
	if (Key == EKeys::Escape)
	{
		// FUN_0045f040 posts 0x3ea and FUN_0044c710 rewrites it to item 4, so Escape on the main
		// menu is Quit outright - there is no confirmation here.
		StopScreenMusic();
		OnItemChosen.ExecuteIfBound(ESimCopterMainMenuItem::Quit);
		return FReply::Handled();
	}

	const FString KeyName = Key.GetFName().ToString();
	if (KeyName.Len() == 1 && SelectByMnemonic(KeyName[0]))
	{
		return FReply::Handled();
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE
