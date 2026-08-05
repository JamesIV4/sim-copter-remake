// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSimCopterCareerSelect.h"

#include "Game/SimCopterCareerProgression.h"
#include "InputCoreTypes.h"
#include "Styling/SlateBrush.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimCopterCareerSelect"

using namespace SimCopterFrontEnd;
using namespace SimCopterCareerSelectLayout;

namespace
{
const TCHAR* const CareerPage = TEXT("CAREER.BMP");
const TCHAR* const HighlightSheet = TEXT("CARSEL.BMP");

// FUN_00457c90 builds both of these as its own sound objects: career.wav once when the screen
// opens, carsel.wav on every selection move.
const TCHAR* const OpenSound = TEXT("career");
const TCHAR* const SelectionSound = TEXT("carsel");
}

namespace SimCopterCareerSelectLayout
{
int32 GetNavigationTarget(const EPanelNavigation Navigation, const int32 Selected, const int32 Count)
{
	if (Selected < 0 || Selected >= Count || Count < 2)
	{
		// FUN_00458a90 falls out of every branch when there is only one panel.
		return INDEX_NONE;
	}

	if (Count == 2)
	{
		// Every key toggles between the two.
		return 1 - Selected;
	}

	// Three panels. Left and Right are a clean ring; Up and Down are not, and both land on 0 from
	// panel 2, which is what the original's fall-through does.
	static constexpr int32 Wheel[4][3] = {
		/* Left  */ { 2, 0, 1 },
		/* Right */ { 1, 2, 0 },
		/* Up    */ { 2, 0, 0 },
		/* Down  */ { 2, 2, 0 },
	};

	return Wheel[static_cast<int32>(Navigation)][Selected];
}
}

void SSimCopterCareerSelect::Construct(const FArguments& InArgs)
{
	Art = InArgs._Art;
	Cities = InArgs._Cities;
	bAllowCancel = InArgs._AllowCancel;
	OnAccepted = InArgs._OnAccepted;
	OnCancelled = InArgs._OnCancelled;

	// FUN_00457c90 drops the -1 slots, so the screen shows one, two or three panels.
	Cities.RemoveAll([](const int32 City) { return City < 0 || City >= SimCopterCareerProgression::CityCount; });
	if (Cities.Num() > PanelCount)
	{
		Cities.SetNum(PanelCount);
	}

	USimCopterHangarArt* ArtObject = Art;
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);

	AddAt(Canvas, FRect{ 0.0f, 0.0f, PageWidth, PageHeight }, MakePageImage(ArtObject, CareerPage));

	for (int32 Panel = 0; Panel < Cities.Num(); ++Panel)
	{
		// The original plays city<N>_s.smk inside the frame; the remake has no Smacker decoder,
		// so the panel carries the city's name instead - otherwise all three frames are identical
		// empty plates. This is the one deliberate substitution on this screen. The plate is a
		// mid-orange, so the name is set light with a hard shadow to stay legible on it.
		AddAt(
			Canvas,
			FRect{
				PanelRect[Panel].Left + 8.0f,
				PanelRect[Panel].Top + PanelRect[Panel].Height() * 0.5f - 14.0f,
				PanelRect[Panel].Right - 8.0f,
				PanelRect[Panel].Bottom },
			SNew(STextBlock)
			.Text(FText::FromString(SimCopterCareerProgression::GetCityName(Cities[Panel])))
			.Justification(ETextJustify::Center)
			.Visibility(EVisibility::HitTestInvisible)
			.Font(PageFont(24, /*bBold=*/true))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.99f, 0.96f, 0.88f, 1.0f)))
			.ShadowOffset(FVector2D(1.5f, 1.5f))
			.ShadowColorAndOpacity(FLinearColor(0.06f, 0.03f, 0.0f, 0.85f)));

		// The four glowing border strips, fading smoothly on selection state changes.
		if (ArtObject != nullptr)
		{
			for (int32 Strip = 0; Strip < HighlightStripCount; ++Strip)
			{
				const FRect& Rect = HighlightStrip[Panel][Strip];
				const FSlateBrush* Brush = ArtObject->GetSubImage(
					HighlightSheet,
					FIntRect(
						FMath::RoundToInt(Rect.Left),
						FMath::RoundToInt(Rect.Top),
						FMath::RoundToInt(Rect.Right),
						FMath::RoundToInt(Rect.Bottom)),
					/*bColorKeyed=*/false);
				if (Brush == nullptr)
				{
					continue;
				}

				AddAt(
					Canvas,
					Rect,
					SNew(SImage)
					.Image(Brush)
					.ColorAndOpacity_Lambda([this, Panel]()
					{
						return FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, PanelOpacity[Panel]));
					})
					.Visibility_Lambda([this, Panel]()
					{
						return PanelOpacity[Panel] > 0.005f ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					}));
			}
		}

		AddAt(
			Canvas,
			PanelRect[Panel],
			MakeInvisibleHitButton(
				FOnClicked::CreateLambda([this, Panel]()
				{
					SetSelectedPanel(Panel);
					return FReply::Handled();
				}),
				FSimpleDelegate::CreateLambda([this, Panel]() { SetSelectedPanel(Panel); }),
				ButtonStyles));
	}

	AddAt(Canvas, CityNameRect,
		SAssignNew(CityNameText, STextBlock)
		.Justification(ETextJustify::Center)
		.Font(PageFont(ReadoutFontHeight, /*bBold=*/true))
		.ColorAndOpacity(FSlateColor(ReadoutColor)));
	AddAt(Canvas, LevelNameRect,
		SAssignNew(LevelNameText, STextBlock)
		.Justification(ETextJustify::Center)
		.Font(PageFont(ReadoutFontHeight, /*bBold=*/true))
		.ColorAndOpacity(FSlateColor(ReadoutColor)));

	const float AcceptX = bAllowCancel ? OkButtonX : OkOnlyButtonX;
	AddAt(Canvas, FRect{ AcceptX, ButtonY, AcceptX + ButtonWidth, ButtonY + ButtonHeight },
		MakeButton(
			ArtObject,
			LOCTEXT("Ok", "OK"), // STRINGTABLE 81
			ButtonFontHeight,
			FOnClicked::CreateLambda([this]() { Accept(); return FReply::Handled(); }),
			ButtonStyles));

	if (bAllowCancel)
	{
		AddAt(Canvas, FRect{ CancelButtonX, ButtonY, CancelButtonX + ButtonWidth, ButtonY + ButtonHeight },
			MakeButton(
				ArtObject,
				LOCTEXT("Cancel", "Cancel"), // STRINGTABLE 82
				ButtonFontHeight,
				FOnClicked::CreateLambda([this]() { Cancel(); return FReply::Handled(); }),
				ButtonStyles));
	}

	ChildSlot
	[
		MakeScaledScreen(Canvas)
	];

	RefreshReadouts();
	PlayScreenSound(OpenSound);
}

void SSimCopterCareerSelect::SetSelectedPanel(const int32 Panel)
{
	if (Panel < 0 || Panel >= Cities.Num() || Panel == SelectedPanel)
	{
		return;
	}

	SelectedPanel = Panel;
	RefreshReadouts();
	PlayScreenSound(SelectionSound);
}

void SSimCopterCareerSelect::RefreshReadouts()
{
	const int32 City = Cities.IsValidIndex(SelectedPanel) ? Cities[SelectedPanel] : INDEX_NONE;

	if (CityNameText.IsValid())
	{
		CityNameText->SetText(FText::FromString(SimCopterCareerProgression::GetCityName(City)));
	}
	if (LevelNameText.IsValid())
	{
		LevelNameText->SetText(FText::FromString(
			SimCopterCareerProgression::GetLevelName(SimCopterCareerProgression::GetLevel(City))));
	}
}

void SSimCopterCareerSelect::Accept()
{
	if (Cities.IsValidIndex(SelectedPanel))
	{
		OnAccepted.ExecuteIfBound(Cities[SelectedPanel]);
	}
}

void SSimCopterCareerSelect::Cancel()
{
	// FUN_00458a90 only posts 0x3ea when Cancel exists at all.
	if (bAllowCancel)
	{
		OnCancelled.ExecuteIfBound();
	}
}

FReply SSimCopterCareerSelect::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
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

	EPanelNavigation Navigation = EPanelNavigation::Left;
	bool bIsNavigation = true;
	if (Key == EKeys::Left)        { Navigation = EPanelNavigation::Left; }
	else if (Key == EKeys::Right)  { Navigation = EPanelNavigation::Right; }
	else if (Key == EKeys::Up)     { Navigation = EPanelNavigation::Up; }
	else if (Key == EKeys::Down)   { Navigation = EPanelNavigation::Down; }
	else                           { bIsNavigation = false; }

	if (bIsNavigation)
	{
		// The original plays carsel.wav for any of the four arrows, even when the wheel does not
		// move, so SetSelectedPanel's own tick would not be enough on its own.
		PlayScreenSound(SelectionSound);
		const int32 Target = GetNavigationTarget(Navigation, SelectedPanel, Cities.Num());
		if (Target != INDEX_NONE && Target != SelectedPanel)
		{
			SelectedPanel = Target;
			RefreshReadouts();
		}
		return FReply::Handled();
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SSimCopterCareerSelect::Tick(const FGeometry& AllottedGeometry, const double CurrentTime, const float DeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, CurrentTime, DeltaTime);

	for (int32 Panel = 0; Panel < PanelCount; ++Panel)
	{
		const float Target = (Panel == SelectedPanel) ? 1.0f : 0.0f;
		PanelOpacity[Panel] = FMath::FInterpTo(PanelOpacity[Panel], Target, static_cast<float>(DeltaTime), 14.0f);
	}
}

#undef LOCTEXT_NAMESPACE
