// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Game/SimCopterSaveSubsystem.h"
#include "SimCopterFrontEndPage.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class USimCopterHangarArt;
struct FButtonStyle;

namespace SimCopterSaveGamePickerLayout
{
using SimCopterFrontEnd::FRect;

constexpr float PageWidth = 510.0f;
constexpr float PageHeight = 436.0f;
constexpr FRect ListRect{ 64.0f, 95.0f, 450.0f, 333.0f };
constexpr float ButtonY = 366.0f;
constexpr float AcceptButtonX = 349.0f;
constexpr float CancelButtonX = 243.0f;
constexpr int32 ListFontHeight = 8;
constexpr int32 ButtonFontHeight = 14;
}

DECLARE_DELEGATE_OneParam(FOnSimCopterSaveGameChosen, const FString&);

// In-app replacement for the original Win32 *.scc/*.scu open dialog. The remake saves its own
// versioned archive, so this lists only compatible slots and keeps career/user files separate.
class SSimCopterSaveGamePicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterSaveGamePicker)
		: _Kind(ESimCopterSessionKind::Career)
	{}
		SLATE_ARGUMENT(TObjectPtr<USimCopterHangarArt>, Art)
		SLATE_ARGUMENT(ESimCopterSessionKind, Kind)
		SLATE_ARGUMENT(TArray<FSimCopterSaveSummary>, Saves)
		SLATE_EVENT(FOnSimCopterSaveGameChosen, OnAccepted)
		SLATE_EVENT(FSimpleDelegate, OnCancelled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	TArray<TSharedPtr<FSimCopterSaveSummary>> Entries;
	FOnSimCopterSaveGameChosen OnAccepted;
	FSimpleDelegate OnCancelled;

	TSharedPtr<SListView<TSharedPtr<FSimCopterSaveSummary>>> ListView;
	TArray<TSharedRef<FButtonStyle>> ButtonStyles;
	TSharedPtr<FTableViewStyle> ListStyle;
	TSharedPtr<FTableRowStyle> RowStyle;

	TSharedRef<ITableRow> MakeRow(
		TSharedPtr<FSimCopterSaveSummary> Item,
		const TSharedRef<STableViewBase>& OwnerTable);
	void Accept();
};
