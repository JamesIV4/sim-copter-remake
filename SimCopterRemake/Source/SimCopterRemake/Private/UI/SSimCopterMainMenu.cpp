// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SSimCopterMainMenu.h"

#include "InputCoreTypes.h"
#include "SimCopterFrontEndPage.h"
#include "Styling/SlateBrush.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SConstraintCanvas.h"
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

// The selection tick FUN_0045ed60 plays, and the looping backing track FUN_0045f3d0 starts.
const TCHAR* const SelectionSound = TEXT("menu");
const TCHAR* const MenuMusic = TEXT("menuback");
}

void SSimCopterMainMenu::Construct(const FArguments& InArgs)
{
	Art = InArgs._Art;
	OnItemChosen = InArgs._OnItemChosen;

	USimCopterHangarArt* ArtObject = Art;
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);

	// main1.bmp, then the two hose pieces that bridge it to the screen edges. The page falls back
	// to a plain panel without the artwork, but the hoses are pure decoration: draw them only when
	// they are really there, or the fallback leaves two grey blocks hanging off the panel.
	AddAt(Canvas, FRect{ PageX, PageY, PageX + PageWidth, PageY + PageHeight },
		MakePageImage(ArtObject, MenuPage));
	if (ArtObject != nullptr)
	{
		if (const FSlateBrush* Brush = ArtObject->GetBitmap(HoseTop, /*bColorKeyed=*/true))
		{
			AddAt(Canvas, FRect{ HoseTopX, HoseTopY, HoseTopX + HoseTopWidth, HoseTopY + HoseTopHeight },
				SNew(SImage).Image(Brush));
		}
		if (const FSlateBrush* Brush = ArtObject->GetBitmap(HoseCorner, /*bColorKeyed=*/true))
		{
			AddAt(
				Canvas,
				FRect{ HoseCornerX, HoseCornerY, HoseCornerX + HoseCornerWidth, HoseCornerY + HoseCornerHeight },
				SNew(SImage).Image(Brush));
		}
	}

	// The two sprite columns. Both strips are two columns wide; the row's brush is picked per
	// frame from the live selection, which is what FUN_0045fe10's `+= 0x3c` does to its source.
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
		MakeScaledScreen(Canvas)
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

