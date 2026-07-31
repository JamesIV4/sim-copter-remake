// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "SimCopterFrontEndPage.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class STextBlock;
class USimCopterHangarArt;
struct FButtonStyle;

// SCHOOK: ControlsDialog 0x00417cd0
//
// The Settings screen's "Controls" sub-dialog - control id 0x7d4, drawn on input.bmp. Reached from
// Settings item 3.
//
// REMAKE DIVERGENCE in one respect, marked below: the original's centrepiece is a picture of a
// keyboard (keyboard.bmp, 506x188) that it hit-tests key by key and tints with keylight.bmp -
// green for the command being edited, red for keys another command already owns, dark grey for
// reserved ones (STRINGTABLE 6). That per-key rectangle table is not in the Ghidra exports; it
// sits in an unanalysed gap the same way the cockpit flap click-boxes do, and recovering it needs
// a byte scan rather than a decompile. The page therefore keeps input.bmp, its four buttons at the
// decoded positions and the instruction well, and lists the bindings instead of drawing them on a
// keyboard - and it rebinds the remake's own UInputSettings mappings, which is the functionality
// the original page delivers.
//
// Decode with citations: Docs/scratchpad/settings-DECODED.md.
namespace SimCopterControlSettingsLayout
{
using SimCopterFrontEnd::FRect;

// FUN_00437f30 gives the dialog a degenerate (0,0,1,1) rect, so input.bmp lands at its own size.
constexpr float PageWidth = 594.0f;
constexpr float PageHeight = 435.0f;

// The blank metal above the instruction well, where keyboard.bmp is composited: 506 px wide
// centred on a 594 px page, i.e. x 44..550. The binding list stands in its place.
constexpr FRect ListRect{ 44.0f, 36.0f, 550.0f, 224.0f };

// The left of FUN_00417cd0's two instruction panels, (32,240)-(294,334), stretched across the
// printed well because the remake shows one block of text rather than a keyboard legend and a
// joystick one.
constexpr FRect InstructionRect{ 40.0f, 244.0f, 556.0f, 332.0f };
constexpr int32 InstructionFontHeight = 16;

// The four buttons, all degenerate so button.bmp's own 100x28 applies.
constexpr float LeftButtonX = 326.0f;   // commands 9 (List All) and 1 (OK)
constexpr float RightButtonX = 444.0f;  // commands 3 (Defaults) and 2 (Cancel)
constexpr float TopButtonY = 346.0f;
constexpr float BottomButtonY = 380.0f;
constexpr int32 ButtonFontHeight = 16;

constexpr int32 RowFontHeight = 15;
constexpr float RowHeight = 22.0f;
}

/** One rebindable mapping, flattened from UInputSettings' two lists. */
struct FSimCopterBinding
{
	FName Name;
	/** Axis mappings carry a scale; action mappings do not. */
	bool bIsAxis = false;
	float Scale = 1.0f;
	FKey Key;

	/** "SimCopterEngineStart" -> "Engine Start". */
	FText GetDisplayLabel() const;
};

DECLARE_DELEGATE(FOnSimCopterControlSettingsClosed);

class SSimCopterControlSettings : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterControlSettings) {}
		SLATE_ARGUMENT(TObjectPtr<USimCopterHangarArt>, Art)
		SLATE_EVENT(FOnSimCopterControlSettingsClosed, OnAccepted)
		SLATE_EVENT(FOnSimCopterControlSettingsClosed, OnCancelled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/** Reads UInputSettings into the flat list the page edits. */
	static void ReadBindings(TArray<FSimCopterBinding>& OutBindings);

	/** Writes the list back and rebuilds every player's key map so the change is live at once. */
	static void WriteBindings(const TArray<FSimCopterBinding>& Bindings);

	/**
	 * The shipped bindings, parsed out of Config/DefaultInput.ini rather than out of the live
	 * UInputSettings - which by then already has the player's saved Input.ini layered on top, so
	 * it is not "defaults" at all. Returns false when the file is missing or has no mappings.
	 */
	static bool ReadDefaultBindings(TArray<FSimCopterBinding>& OutBindings);

	/** Strips the project prefix and spaces the camel case: "SimCopterLookYaw" -> "Look Yaw". */
	static FText MakeDisplayLabel(FName MappingName, bool bIsAxis, float Scale);

private:
	TObjectPtr<USimCopterHangarArt> Art;
	FOnSimCopterControlSettingsClosed OnAccepted;
	FOnSimCopterControlSettingsClosed OnCancelled;

	TArray<FSimCopterBinding> Bindings;
	TArray<FSimCopterBinding> Entered;
	TArray<TSharedPtr<int32>> Rows;

	TSharedPtr<SListView<TSharedPtr<int32>>> ListView;
	TSharedPtr<STextBlock> Instructions;
	TArray<TSharedRef<FButtonStyle>> ButtonStyles;
	TSharedPtr<struct FTableViewStyle> ListStyle;
	TSharedPtr<struct FTableRowStyle> RowStyle;

	/** Index of the row waiting for a key, or INDEX_NONE. */
	int32 RebindIndex = INDEX_NONE;

	void RebuildRows();
	void RefreshInstructions();
	void BeginRebind(int32 Index);
	void ApplyRebind(const FKey& Key);
	void RestoreDefaults();
	void Accept();
	void Cancel();

	TSharedRef<class ITableRow> MakeRow(TSharedPtr<int32> Item, const TSharedRef<class STableViewBase>& OwnerTable);
};
