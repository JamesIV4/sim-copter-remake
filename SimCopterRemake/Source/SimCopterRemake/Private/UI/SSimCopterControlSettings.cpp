// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSimCopterControlSettings.h"

#include "Brushes/SlateColorBrush.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Styling/SlateBrush.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SimCopterControlSettings"

using namespace SimCopterFrontEnd;
using namespace SimCopterControlSettingsLayout;

namespace
{
const TCHAR* const ControlsPage = TEXT("INPUT.BMP");

// The instruction well is dark; the list stands on the bare metal above it.
const FLinearColor InstructionText(0.86f, 0.90f, 0.76f, 1.0f);
const FLinearColor RowText(0.10f, 0.11f, 0.10f, 1.0f);

// STRINGTABLE 6's colour scheme, applied to the list instead of to a keyboard picture: green is
// the row being edited, red a key another command already owns.
const FLinearColor ArmedText(0.10f, 0.42f, 0.10f, 1.0f);
const FLinearColor ConflictText(0.60f, 0.08f, 0.08f, 1.0f);

const TCHAR* const InputSection = TEXT("/Script/Engine.InputSettings");
}

FText FSimCopterBinding::GetDisplayLabel() const
{
	return SSimCopterControlSettings::MakeDisplayLabel(Name, bIsAxis, Scale);
}

FText SSimCopterControlSettings::MakeDisplayLabel(const FName MappingName, const bool bIsAxis, const float Scale)
{
	FString Raw = MappingName.ToString();
	Raw.RemoveFromStart(TEXT("SimCopter"));

	// Space the camel case so "ControllerRightTrigger" reads as words.
	FString Spaced;
	Spaced.Reserve(Raw.Len() + 8);
	for (int32 Index = 0; Index < Raw.Len(); ++Index)
	{
		const TCHAR Character = Raw[Index];
		if (Index > 0 && FChar::IsUpper(Character) && !FChar::IsUpper(Raw[Index - 1]))
		{
			Spaced.AppendChar(TEXT(' '));
		}
		Spaced.AppendChar(Character);
	}

	if (!bIsAxis)
	{
		return FText::FromString(Spaced);
	}

	// An axis has one row per direction, so the sign has to be on the label or the two rows look
	// like duplicates.
	return FText::Format(
		LOCTEXT("AxisFormat", "{0} ({1})"),
		FText::FromString(Spaced),
		Scale >= 0.0f ? LOCTEXT("AxisPositive", "+") : LOCTEXT("AxisNegative", "-"));
}

void SSimCopterControlSettings::ReadBindings(TArray<FSimCopterBinding>& OutBindings)
{
	OutBindings.Reset();

	const UInputSettings* Settings = UInputSettings::GetInputSettings();
	if (Settings == nullptr)
	{
		return;
	}

	for (const FInputActionKeyMapping& Mapping : Settings->GetActionMappings())
	{
		OutBindings.Add(FSimCopterBinding{ Mapping.ActionName, /*bIsAxis=*/false, 1.0f, Mapping.Key });
	}
	for (const FInputAxisKeyMapping& Mapping : Settings->GetAxisMappings())
	{
		OutBindings.Add(FSimCopterBinding{ Mapping.AxisName, /*bIsAxis=*/true, Mapping.Scale, Mapping.Key });
	}
}

void SSimCopterControlSettings::WriteBindings(const TArray<FSimCopterBinding>& Bindings)
{
	UInputSettings* Settings = UInputSettings::GetInputSettings();
	if (Settings == nullptr)
	{
		return;
	}

	// Clearing and refilling is the only way to reorder or replace wholesale; the mapping structs
	// are matched by value, so an edited key can no longer find its old entry.
	for (const FInputActionKeyMapping Mapping : TArray<FInputActionKeyMapping>(Settings->GetActionMappings()))
	{
		Settings->RemoveActionMapping(Mapping, /*bForceRebuildKeymaps=*/false);
	}
	for (const FInputAxisKeyMapping Mapping : TArray<FInputAxisKeyMapping>(Settings->GetAxisMappings()))
	{
		Settings->RemoveAxisMapping(Mapping, /*bForceRebuildKeymaps=*/false);
	}

	for (const FSimCopterBinding& Binding : Bindings)
	{
		if (Binding.bIsAxis)
		{
			FInputAxisKeyMapping Mapping;
			Mapping.AxisName = Binding.Name;
			Mapping.Key = Binding.Key;
			Mapping.Scale = Binding.Scale;
			Settings->AddAxisMapping(Mapping, /*bForceRebuildKeymaps=*/false);
		}
		else
		{
			FInputActionKeyMapping Mapping;
			Mapping.ActionName = Binding.Name;
			Mapping.Key = Binding.Key;
			Settings->AddActionMapping(Mapping, /*bForceRebuildKeymaps=*/false);
		}
	}

	Settings->ForceRebuildKeymaps();
	Settings->SaveKeyMappings();
}

bool SSimCopterControlSettings::ReadDefaultBindings(TArray<FSimCopterBinding>& OutBindings)
{
	OutBindings.Reset();

	// Deliberately the single file, not FConfigCacheIni's hierarchy: the hierarchy already has the
	// player's saved Input.ini layered on top, which is exactly what "Defaults" has to ignore.
	FConfigFile DefaultIni;
	DefaultIni.Read(FPaths::ProjectConfigDir() / TEXT("DefaultInput.ini"));

	const FConfigSection* Section = DefaultIni.FindSection(InputSection);
	if (Section == nullptr)
	{
		return false;
	}

	TArray<FConfigValue> Values;
	Section->MultiFind(FName(TEXT("ActionMappings")), Values, /*bMaintainOrder=*/true);
	for (const FConfigValue& Value : Values)
	{
		FInputActionKeyMapping Mapping;
		if (FInputActionKeyMapping::StaticStruct()->ImportText(
				*Value.GetValue(), &Mapping, nullptr, PPF_None, nullptr, TEXT("ActionMappings")) != nullptr)
		{
			OutBindings.Add(FSimCopterBinding{ Mapping.ActionName, /*bIsAxis=*/false, 1.0f, Mapping.Key });
		}
	}

	Values.Reset();
	Section->MultiFind(FName(TEXT("AxisMappings")), Values, /*bMaintainOrder=*/true);
	for (const FConfigValue& Value : Values)
	{
		FInputAxisKeyMapping Mapping;
		if (FInputAxisKeyMapping::StaticStruct()->ImportText(
				*Value.GetValue(), &Mapping, nullptr, PPF_None, nullptr, TEXT("AxisMappings")) != nullptr)
		{
			OutBindings.Add(FSimCopterBinding{ Mapping.AxisName, /*bIsAxis=*/true, Mapping.Scale, Mapping.Key });
		}
	}

	return OutBindings.Num() > 0;
}

void SSimCopterControlSettings::Construct(const FArguments& InArgs)
{
	Art = InArgs._Art;
	OnAccepted = InArgs._OnAccepted;
	OnCancelled = InArgs._OnCancelled;

	ReadBindings(Bindings);
	Entered = Bindings;

	USimCopterHangarArt* ArtObject = Art;
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);

	const float PageX = FMath::RoundToFloat((ScreenWidth - PageWidth) * 0.5f);
	const float PageY = FMath::RoundToFloat((ScreenHeight - PageHeight) * 0.5f);
	const auto AddAtPage = [&Canvas, PageX, PageY](const FRect& Rect, TSharedRef<SWidget> Widget)
	{
		AddAt(Canvas, FRect{ PageX + Rect.Left, PageY + Rect.Top, PageX + Rect.Right, PageY + Rect.Bottom }, Widget);
	};

	AddAt(Canvas, FRect{ PageX, PageY, PageX + PageWidth, PageY + PageHeight },
		MakePageImage(ArtObject, ControlsPage));

	// The page prints bare metal here, so the list gets its own dark panel and transparent rows -
	// Slate's defaults would paint an opaque grey slab.
	ListStyle = MakeShared<FTableViewStyle>();
	ListStyle->SetBackgroundBrush(FSlateColorBrush(FLinearColor(0.72f, 0.74f, 0.72f, 0.82f)));

	const FSlateColorBrush Highlight(FLinearColor(0.20f, 0.34f, 0.55f, 0.45f));
	RowStyle = MakeShared<FTableRowStyle>();
	RowStyle->SetEvenRowBackgroundBrush(FSlateNoResource());
	RowStyle->SetEvenRowBackgroundHoveredBrush(FSlateColorBrush(FLinearColor(0.0f, 0.0f, 0.0f, 0.12f)));
	RowStyle->SetOddRowBackgroundBrush(FSlateColorBrush(FLinearColor(0.0f, 0.0f, 0.0f, 0.06f)));
	RowStyle->SetOddRowBackgroundHoveredBrush(FSlateColorBrush(FLinearColor(0.0f, 0.0f, 0.0f, 0.12f)));
	RowStyle->SetActiveBrush(Highlight);
	RowStyle->SetActiveHoveredBrush(Highlight);
	RowStyle->SetInactiveBrush(Highlight);
	RowStyle->SetInactiveHoveredBrush(Highlight);
	RowStyle->SetSelectorFocusedBrush(FSlateNoResource());
	RowStyle->SetTextColor(FSlateColor(RowText));
	RowStyle->SetSelectedTextColor(FSlateColor(RowText));

	RebuildRows();

	AddAtPage(ListRect,
		SAssignNew(ListView, SListView<TSharedPtr<int32>>)
		.ListViewStyle(ListStyle.Get())
		.ListItemsSource(&Rows)
		.SelectionMode(ESelectionMode::Single)
		.OnGenerateRow(this, &SSimCopterControlSettings::MakeRow)
		.OnMouseButtonDoubleClick_Lambda([this](TSharedPtr<int32> Item)
		{
			if (Item.IsValid())
			{
				BeginRebind(*Item);
			}
		}));

	AddAtPage(InstructionRect,
		SAssignNew(Instructions, STextBlock)
		.AutoWrapText(true)
		.Visibility(EVisibility::HitTestInvisible)
		.Font(PageFont(InstructionFontHeight))
		.ColorAndOpacity(FSlateColor(InstructionText)));

	// STRINGTABLE 8 "List All", 5 "Defaults", 20 "OK", 21 "Cancel" - the original's four, in the
	// original's four positions.
	AddAtPage(FRect{ LeftButtonX, TopButtonY, LeftButtonX + ButtonWidth, TopButtonY + ButtonHeight },
		MakeButton(
			ArtObject,
			LOCTEXT("ListAll", "List All"),
			ButtonFontHeight,
			FOnClicked::CreateLambda([this]()
			{
				// The original's List All opens a scrolling dialog of every binding on menu4.bmp.
				// The remake's page IS that list, so the button just clears the arming state and
				// puts the selection back at the top.
				RebindIndex = INDEX_NONE;
				if (ListView.IsValid() && Rows.Num() > 0)
				{
					ListView->ScrollToTop();
					ListView->SetSelection(Rows[0]);
				}
				RefreshInstructions();
				return FReply::Handled();
			}),
			ButtonStyles));

	AddAtPage(FRect{ RightButtonX, TopButtonY, RightButtonX + ButtonWidth, TopButtonY + ButtonHeight },
		MakeButton(
			ArtObject,
			LOCTEXT("Defaults", "Defaults"),
			ButtonFontHeight,
			FOnClicked::CreateLambda([this]() { RestoreDefaults(); return FReply::Handled(); }),
			ButtonStyles));

	AddAtPage(FRect{ LeftButtonX, BottomButtonY, LeftButtonX + ButtonWidth, BottomButtonY + ButtonHeight },
		MakeButton(
			ArtObject,
			LOCTEXT("Ok", "OK"),
			ButtonFontHeight,
			FOnClicked::CreateLambda([this]() { Accept(); return FReply::Handled(); }),
			ButtonStyles));

	AddAtPage(FRect{ RightButtonX, BottomButtonY, RightButtonX + ButtonWidth, BottomButtonY + ButtonHeight },
		MakeButton(
			ArtObject,
			LOCTEXT("Cancel", "Cancel"),
			ButtonFontHeight,
			FOnClicked::CreateLambda([this]() { Cancel(); return FReply::Handled(); }),
			ButtonStyles));

	ChildSlot
	[
		MakeScaledScreen(Canvas)
	];

	RefreshInstructions();
}

void SSimCopterControlSettings::RebuildRows()
{
	Rows.Reset();
	for (int32 Index = 0; Index < Bindings.Num(); ++Index)
	{
		Rows.Add(MakeShared<int32>(Index));
	}
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

void SSimCopterControlSettings::RefreshInstructions()
{
	if (!Instructions.IsValid())
	{
		return;
	}

	Instructions->SetText(RebindIndex != INDEX_NONE
		? FText::Format(
			LOCTEXT(
				"RebindArmed",
				"Press a key or button for {0}, or Escape to leave it alone."),
			Bindings.IsValidIndex(RebindIndex) ? Bindings[RebindIndex].GetDisplayLabel() : FText::GetEmpty())
		// The remake's wording of STRINGTABLE 6, which describes the keyboard picture this page
		// does not have.
		: LOCTEXT(
			"Instructions",
			"Instructions:\nClick a command to change it, then press the key or button you want. "
			"A binding another command already owns is shown in red. Defaults puts back the "
			"bindings the game shipped with."));
}

void SSimCopterControlSettings::BeginRebind(const int32 Index)
{
	if (!Bindings.IsValidIndex(Index))
	{
		return;
	}
	RebindIndex = Index;
	RefreshInstructions();
}

void SSimCopterControlSettings::ApplyRebind(const FKey& Key)
{
	if (!Bindings.IsValidIndex(RebindIndex))
	{
		return;
	}

	Bindings[RebindIndex].Key = Key;
	RebindIndex = INDEX_NONE;
	RefreshInstructions();

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

void SSimCopterControlSettings::RestoreDefaults()
{
	TArray<FSimCopterBinding> Defaults;
	if (ReadDefaultBindings(Defaults))
	{
		Bindings = MoveTemp(Defaults);
		RebindIndex = INDEX_NONE;
		RebuildRows();
		RefreshInstructions();
	}
}

void SSimCopterControlSettings::Accept()
{
	WriteBindings(Bindings);
	OnAccepted.ExecuteIfBound();
}

void SSimCopterControlSettings::Cancel()
{
	// Nothing has been written yet - the page edits its own copy - so Cancel only has to drop it.
	Bindings = Entered;
	OnCancelled.ExecuteIfBound();
}

TSharedRef<ITableRow> SSimCopterControlSettings::MakeRow(
	TSharedPtr<int32> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const int32 Index = Item.IsValid() ? *Item : INDEX_NONE;

	return SNew(STableRow<TSharedPtr<int32>>, OwnerTable)
		.Style(RowStyle.Get())
		.Padding(FMargin(8.0f, 1.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.62f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Bindings.IsValidIndex(Index) ? Bindings[Index].GetDisplayLabel() : FText::GetEmpty())
				.Font(PageFont(RowFontHeight))
				.ColorAndOpacity(FSlateColor(RowText))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.38f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(PageFont(RowFontHeight, /*bBold=*/true))
				.Text_Lambda([this, Index]()
				{
					if (RebindIndex == Index)
					{
						return LOCTEXT("PressAKey", "press a key...");
					}
					return Bindings.IsValidIndex(Index) ? Bindings[Index].Key.GetDisplayName() : FText::GetEmpty();
				})
				.ColorAndOpacity_Lambda([this, Index]()
				{
					if (RebindIndex == Index)
					{
						return FSlateColor(ArmedText);
					}
					if (!Bindings.IsValidIndex(Index))
					{
						return FSlateColor(RowText);
					}

					// Red when another command owns the same key, which is STRINGTABLE 6's rule.
					const FKey& Key = Bindings[Index].Key;
					for (int32 Other = 0; Other < Bindings.Num(); ++Other)
					{
						if (Other != Index && Bindings[Other].Key == Key && Bindings[Other].Name != Bindings[Index].Name)
						{
							return FSlateColor(ConflictText);
						}
					}
					return FSlateColor(RowText);
				})
			]
		];
}

FReply SSimCopterControlSettings::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// While a row is armed, any button press is the new binding - that is the point of arming.
	if (RebindIndex != INDEX_NONE)
	{
		ApplyRebind(MouseEvent.GetEffectingButton());
		return FReply::Handled();
	}

	return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SSimCopterControlSettings::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	if (RebindIndex != INDEX_NONE)
	{
		if (Key == EKeys::Escape)
		{
			RebindIndex = INDEX_NONE;
			RefreshInstructions();
		}
		else
		{
			ApplyRebind(Key);
		}
		return FReply::Handled();
	}

	if (Key == EKeys::Escape)
	{
		Cancel();
		return FReply::Handled();
	}
	if (Key == EKeys::Enter)
	{
		// Enter arms the selected row rather than accepting, so a keyboard-only player can rebind
		// without a mouse; OK is reachable with Tab.
		if (ListView.IsValid())
		{
			const TArray<TSharedPtr<int32>> Selected = ListView->GetSelectedItems();
			if (Selected.Num() > 0 && Selected[0].IsValid())
			{
				BeginRebind(*Selected[0]);
				return FReply::Handled();
			}
		}
		Accept();
		return FReply::Handled();
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE
