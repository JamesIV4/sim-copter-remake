// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SimCopterFrontEndPage.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SCompoundWidget.h"

class USimCopterHangarArt;
struct FButtonStyle;

DECLARE_DELEGATE_OneParam(FOnSimCopterSaveNameAccepted, const FString&);

// In-app Save As name entry. The original used a Win32 SaveFile dialog; this stays within the
// same menu4.bmp shell used by the remake's other file pickers.
class SSimCopterSaveNameDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterSaveNameDialog) {}
		SLATE_ARGUMENT(TObjectPtr<USimCopterHangarArt>, Art)
		SLATE_ARGUMENT(FString, SuggestedName)
		SLATE_EVENT(FOnSimCopterSaveNameAccepted, OnAccepted)
		SLATE_EVENT(FSimpleDelegate, OnCancelled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	TSharedPtr<SWidget> GetInitialFocusWidget() const { return NameEntry; }

private:
	TSharedPtr<SEditableTextBox> NameEntry;
	FOnSimCopterSaveNameAccepted OnAccepted;
	FSimpleDelegate OnCancelled;
	TArray<TSharedRef<FButtonStyle>> ButtonStyles;

	void Accept();
};

