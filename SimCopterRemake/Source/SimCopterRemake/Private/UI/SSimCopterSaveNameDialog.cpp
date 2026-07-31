// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SSimCopterSaveNameDialog.h"

#include "Game/SimCopterSaveSubsystem.h"
#include "InputCoreTypes.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimCopterSaveNameDialog"

using namespace SimCopterFrontEnd;

namespace
{
const TCHAR* const DialogPage = TEXT("MENU4.BMP");
constexpr float PageWidth = 510.0f;
constexpr float PageHeight = 436.0f;
constexpr FRect TitleRect{ 88.0f, 38.0f, 410.0f, 70.0f };
constexpr FRect PromptRect{ 82.0f, 130.0f, 428.0f, 160.0f };
constexpr FRect NameRect{ 92.0f, 178.0f, 418.0f, 216.0f };
constexpr float ButtonY = 366.0f;
constexpr float AcceptButtonX = 349.0f;
constexpr float CancelButtonX = 243.0f;
const FLinearColor TitleText(0.90f, 0.93f, 0.98f, 1.0f);
const FLinearColor PageText(0.08f, 0.08f, 0.09f, 1.0f);
}

void SSimCopterSaveNameDialog::Construct(const FArguments& InArgs)
{
	OnAccepted = InArgs._OnAccepted;
	OnCancelled = InArgs._OnCancelled;
	USimCopterHangarArt* ArtObject = InArgs._Art;

	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);
	const float PageX = FMath::RoundToFloat((ScreenWidth - PageWidth) * 0.5f);
	const float PageY = FMath::RoundToFloat((ScreenHeight - PageHeight) * 0.5f);
	const auto AddAtPage = [&Canvas, PageX, PageY](const FRect& Rect, TSharedRef<SWidget> Widget)
	{
		AddAt(Canvas, FRect{ PageX + Rect.Left, PageY + Rect.Top, PageX + Rect.Right, PageY + Rect.Bottom }, Widget);
	};

	AddAt(Canvas, FRect{ PageX, PageY, PageX + PageWidth, PageY + PageHeight },
		MakePageImage(ArtObject, DialogPage));
	AddAtPage(TitleRect,
		SNew(STextBlock)
		.Text(LOCTEXT("Title", "Save A SimCopter Game"))
		.Justification(ETextJustify::Center)
		.Font(PageFont(22, /*bBold=*/true))
		.ColorAndOpacity(FSlateColor(TitleText)));
	AddAtPage(PromptRect,
		SNew(STextBlock)
		.Text(LOCTEXT("Prompt", "Name this saved game:"))
		.Justification(ETextJustify::Center)
		.Font(PageFont(17))
		.ColorAndOpacity(FSlateColor(PageText)));
	AddAtPage(NameRect,
		SAssignNew(NameEntry, SEditableTextBox)
		.Text(FText::FromString(InArgs._SuggestedName))
		.SelectAllTextWhenFocused(true)
		.OnTextCommitted_Lambda([this](const FText&, const ETextCommit::Type CommitType)
		{
			if (CommitType == ETextCommit::OnEnter)
			{
				Accept();
			}
		}));

	AddAtPage(FRect{ AcceptButtonX, ButtonY, AcceptButtonX + ButtonWidth, ButtonY + ButtonHeight },
		MakeButton(
			ArtObject,
			LOCTEXT("Save", "Save"),
			14,
			FOnClicked::CreateLambda([this]() { Accept(); return FReply::Handled(); }),
			ButtonStyles));
	AddAtPage(FRect{ CancelButtonX, ButtonY, CancelButtonX + ButtonWidth, ButtonY + ButtonHeight },
		MakeButton(
			ArtObject,
			LOCTEXT("Cancel", "Cancel"),
			14,
			FOnClicked::CreateLambda([this]()
			{
				OnCancelled.ExecuteIfBound();
				return FReply::Handled();
			}),
			ButtonStyles));

	ChildSlot
	[
		MakeScaledScreen(Canvas)
	];
}

void SSimCopterSaveNameDialog::Accept()
{
	if (!NameEntry.IsValid())
	{
		return;
	}
	FString Error;
	const FString Name = NameEntry->GetText().ToString();
	if (!USimCopterSaveSubsystem::IsDisplayNameValid(Name, Error))
	{
		NameEntry->SetError(FText::FromString(Error));
		return;
	}
	NameEntry->SetError(FText::GetEmpty());
	OnAccepted.ExecuteIfBound(Name);
}

FReply SSimCopterSaveNameDialog::OnKeyDown(
	const FGeometry& MyGeometry,
	const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		OnCancelled.ExecuteIfBound();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE

