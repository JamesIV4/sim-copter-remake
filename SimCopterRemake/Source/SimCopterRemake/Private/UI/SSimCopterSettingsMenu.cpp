// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSimCopterSettingsMenu.h"

#include "InputCoreTypes.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimCopterSettingsMenu"

using namespace SimCopterFrontEnd;
using namespace SimCopterSettingsMenuLayout;

namespace
{
const TCHAR* const SettingsPage = TEXT("PLAYMENU.BMP");

// The page's own selection tick, the same standalone sound object the main menu builds.
const TCHAR* const SelectionSound = TEXT("menu");
}

ESimCopterSettingsItem SSimCopterSettingsMenu::GetItemForRow(const int32 Row, const bool bHasCitySettings)
{
	// Descriptor +0x24 is the command base: 0 with City Settings present, 1 without.
	const int32 Base = bHasCitySettings ? 0 : 1;
	return static_cast<ESimCopterSettingsItem>(
		FMath::Clamp(Row + Base, 0, FullItemCount - 1));
}

const FText& SSimCopterSettingsMenu::GetItemLabel(const ESimCopterSettingsItem Item)
{
	// STRINGTABLE 60..67, English.
	static const FText Labels[FullItemCount] = {
		LOCTEXT("CitySettings", "City Settings"), // 60
		LOCTEXT("Graphics", "Graphics"),          // 61
		LOCTEXT("Sound", "Sound"),                // 62
		LOCTEXT("Controls", "Controls"),          // 63
		LOCTEXT("SaveGame", "Save Game"),         // 64
		LOCTEXT("SaveGameAs", "Save Game As"),    // 65
		LOCTEXT("LeaveCity", "Leave City"),       // 66
		LOCTEXT("Continue", "Continue"),          // 67
	};

	static const FText Empty = FText::GetEmpty();
	const int32 Index = static_cast<int32>(Item);
	return (Index >= 0 && Index < FullItemCount) ? Labels[Index] : Empty;
}

void SSimCopterSettingsMenu::Construct(const FArguments& InArgs)
{
	Art = InArgs._Art;
	OnItemChosen = InArgs._OnItemChosen;
	bHasCitySettings = InArgs._AllowCitySettings;
	VisibleCount = bHasCitySettings ? FullItemCount : FullItemCount - 1;

	USimCopterHangarArt* ArtObject = Art;
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);

	AddAt(Canvas, FRect{ PageX, PageY, PageX + PageWidth, PageY + PageHeight },
		MakePageImage(ArtObject, SettingsPage));

	// Labels first, hit rows after, so the transparent rows sit on top and get the pointer.
	for (int32 Row = 0; Row < VisibleCount; ++Row)
	{
		const FRect Text = GetRowTextRect(Row, bHasCitySettings);
		AddAt(
			Canvas,
			FRect{ PageX + Text.Left, PageY + Text.Top, PageX + Text.Right, PageY + Text.Bottom },
			SNew(STextBlock)
			.Text(GetItemLabel(GetItemForRow(Row, bHasCitySettings)))
			.Visibility(EVisibility::HitTestInvisible)
			.Font(PageFont(ItemFontHeight, /*bBold=*/true))
			.ColorAndOpacity_Lambda([this, Row]()
			{
				return FSlateColor(SelectedRow == Row ? ItemSelectedColor : ItemColor);
			}));
	}

	for (int32 Row = 0; Row < VisibleCount; ++Row)
	{
		const FRect Hit = GetRowHitRect(Row, bHasCitySettings);
		AddAt(
			Canvas,
			FRect{ PageX + Hit.Left, PageY + Hit.Top, PageX + Hit.Right, PageY + Hit.Bottom },
			MakeInvisibleHitButton(
				FOnClicked::CreateLambda([this, Row]()
				{
					SetSelectedRow(Row);
					ActivateSelected();
					return FReply::Handled();
				}),
				FSimpleDelegate::CreateLambda([this, Row]() { SetSelectedRow(Row); }),
				ButtonStyles));
	}

	ChildSlot
	[
		MakeScaledScreen(Canvas)
	];
}

void SSimCopterSettingsMenu::SetSelectedRow(const int32 Row)
{
	if (Row < 0 || Row >= VisibleCount || Row == SelectedRow)
	{
		return;
	}

	SelectedRow = Row;
	PlayScreenSound(SelectionSound);
}

void SSimCopterSettingsMenu::ActivateSelected()
{
	OnItemChosen.ExecuteIfBound(GetItemForRow(SelectedRow, bHasCitySettings));
}

bool SSimCopterSettingsMenu::SelectByMnemonic(const TCHAR Character)
{
	// FUN_0045eed0 stops at the first item whose text starts with the key, so on S "Save Game"
	// wins over "Save Game As" - and over "Sound", which is above both.
	for (int32 Row = 0; Row < VisibleCount; ++Row)
	{
		const FString Label = GetItemLabel(GetItemForRow(Row, bHasCitySettings)).ToString();
		if (Label.Len() > 0 && FChar::ToUpper(Label[0]) == FChar::ToUpper(Character))
		{
			SetSelectedRow(Row);
			return true;
		}
	}

	return false;
}

FReply SSimCopterSettingsMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	if (const ENavigation Navigation = GetNavigationForKey(Key); Navigation != ENavigation::None)
	{
		const int32 Target = GetNavigationTarget(Navigation, SelectedRow, VisibleCount);
		if (Target != INDEX_NONE)
		{
			SetSelectedRow(Target);
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Enter)
	{
		ActivateSelected();
		return FReply::Handled();
	}
	if (Key == EKeys::Escape)
	{
		// FUN_0044c9e0's 0x3ea branch resumes the sim and closes the page - byte for byte what
		// item 7 does, so Escape here is Continue, not a cancel.
		OnItemChosen.ExecuteIfBound(ESimCopterSettingsItem::Continue);
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
