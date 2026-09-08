#include "SSimCopterNavigableMenu.h"

#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Layout/ArrangedChildren.h"
#include "Rendering/DrawElements.h"
#include "SSimCopterCheckupSlider.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SScrollBox.h"

namespace
{
FVector2D DirectionForKey(const FKey Key)
{
	if (Key == EKeys::Gamepad_DPad_Left) return FVector2D(-1, 0);
	if (Key == EKeys::Gamepad_DPad_Right) return FVector2D(1, 0);
	if (Key == EKeys::Gamepad_DPad_Up) return FVector2D(0, -1);
	if (Key == EKeys::Gamepad_DPad_Down) return FVector2D(0, 1);
	return FVector2D::ZeroVector;
}
FKey ArrowForDirection(FVector2D Direction)
{
	return Direction.X < 0 ? EKeys::Left : Direction.X > 0 ? EKeys::Right
		: Direction.Y < 0 ? EKeys::Up : EKeys::Down;
}
FKeyEvent KeyEvent(FKey Key, uint32 UserIndex)
{
	return FKeyEvent(Key, FModifierKeysState(), UserIndex, false, 0, 0);
}
}

int32 SSimCopterNavigableMenu::FindSpatialTarget(const TArray<FSlateRect>& Rects,
	int32 Current, FVector2D Direction)
{
	if (!Rects.IsValidIndex(Current)) return Rects.IsEmpty() ? INDEX_NONE : 0;
	const FSlateRect& From = Rects[Current];
	const FVector2D Centre((From.Left + From.Right) * 0.5, (From.Top + From.Bottom) * 0.5);
	double BestScore = TNumericLimits<double>::Max();
	int32 Best = Current;
	for (int32 Index = 0; Index < Rects.Num(); ++Index)
	{
		if (Index == Current) continue;
		const FSlateRect& To = Rects[Index];
		const FVector2D Delta = FVector2D((To.Left + To.Right) * 0.5, (To.Top + To.Bottom) * 0.5) - Centre;
		const double Forward = FVector2D::DotProduct(Delta, Direction);
		if (Forward <= 1.0) continue;
		// Prefer controls sharing a visual row/column; only cross a diagonal gap when necessary.
		const double Gap = Direction.X != 0
			? FMath::Max(0.0f, FMath::Max(To.Top - From.Bottom, From.Top - To.Bottom))
			: FMath::Max(0.0f, FMath::Max(To.Left - From.Right, From.Left - To.Right));
		const double Side = FMath::Abs(Delta.X * Direction.Y - Delta.Y * Direction.X);
		const double Score = Forward + Side * 0.25 + Gap * 4.0;
		if (Score < BestScore) { BestScore = Score; Best = Index; }
	}
	return Best;
}

void SSimCopterNavigableMenu::CollectControls(const TSharedRef<SWidget>& Widget,
	TArray<TSharedRef<SWidget>>& Out, const FGeometry& Geometry) const
{
	if (!Widget->GetVisibility().IsVisible() || !Widget->IsEnabled()) return;
	const FString Type = Widget->GetTypeAsString();
	const bool bSlider = Type == TEXT("SSimCopterCheckupSlider");
	if (bSlider && !StaticCastSharedRef<SSimCopterCheckupSlider>(Widget)->CanAdjust()) return;
	if (Type == TEXT("SButton") || Type == TEXT("SCheckBox") || bSlider ||
		Type.StartsWith(TEXT("SComboBox")) || Type.StartsWith(TEXT("SListView")) ||
		Type == TEXT("SEditableTextBox"))
	{
		const FVector2D Size = Geometry.GetLocalSize();
		if (Size.X > 0 && Size.Y > 0)
		{
			Out.Add(Widget);
			ControlGeometries.Add(&Widget.Get(), Geometry);
		}
		return;
	}
	FArrangedChildren Children(EVisibility::Visible);
	Widget->ArrangeChildren(Geometry, Children);
	for (int32 Index = 0; Index < Children.Num(); ++Index)
		CollectControls(Children[Index].Widget, Out, Children[Index].Geometry);
}

TSharedPtr<SWidget> SSimCopterNavigableMenu::ResolveSelection()
{
	TArray<TSharedRef<SWidget>> Controls;
	ControlGeometries.Reset();
	CollectControls(SharedThis(this), Controls, GetCachedGeometry());
	const TSharedPtr<SWidget> Selected = SelectedControl.Pin();
	for (const TSharedRef<SWidget>& Control : Controls)
		if (Control == Selected)
		{
			if (bEditingControl && Control->GetTypeAsString().StartsWith(TEXT("SComboBox")) &&
				!StaticCastSharedRef<SComboButton>(Control)->IsOpen()) bEditingControl = false;
			return Selected;
		}
	bEditingControl = false;
	SelectedControl = Controls.IsEmpty() ? TSharedPtr<SWidget>() : Controls[0];
	if (!Controls.IsEmpty()) OnControllerSelectionChanged(0);
	return SelectedControl.Pin();
}

void SSimCopterNavigableMenu::MoveSelection(FVector2D Direction, uint32 UserIndex)
{
	const TSharedPtr<SWidget> Selected = ResolveSelection();
	if (!Selected) return;
	if (bEditingControl)
	{
		if (Selected->GetTypeAsString() == TEXT("SSimCopterCheckupSlider"))
			StaticCastSharedPtr<SSimCopterCheckupSlider>(Selected)->AdjustForController(Direction);
		else
			ForwardControlKey(ArrowForDirection(Direction), UserIndex);
		return;
	}
	TArray<TSharedRef<SWidget>> Controls;
	ControlGeometries.Reset();
	CollectControls(SharedThis(this), Controls, GetCachedGeometry());
	TArray<FSlateRect> Rects;
	int32 Current = INDEX_NONE;
	for (int32 Index = 0; Index < Controls.Num(); ++Index)
	{
		Rects.Add(ControlGeometries.FindChecked(&Controls[Index].Get()).GetLayoutBoundingRect());
		if (Controls[Index] == Selected) Current = Index;
	}
	const int32 Target = FindSpatialTarget(Rects, Current, Direction);
	if (Controls.IsValidIndex(Target))
	{
		SelectedControl = Controls[Target];
		OnControllerSelectionChanged(Target);
		// Scroll ancestors reveal the new control, including settings below the fold.
		for (TSharedPtr<SWidget> Parent = Controls[Target]->GetParentWidget(); Parent; Parent = Parent->GetParentWidget())
			if (Parent->GetTypeAsString() == TEXT("SScrollBox"))
				StaticCastSharedPtr<SScrollBox>(Parent)->ScrollDescendantIntoView(Controls[Target], false);
		FSlateApplication::Get().SetUserFocus(UserIndex, SharedThis(this), EFocusCause::Navigation);
	}
}

void SSimCopterNavigableMenu::ForwardControlKey(FKey Key, uint32 UserIndex)
{
	TGuardValue<bool> Forwarding(bForwardingControlKey, true);
	FSlateApplication::Get().ProcessKeyDownEvent(KeyEvent(Key, UserIndex));
	FSlateApplication::Get().ProcessKeyUpEvent(KeyEvent(Key, UserIndex));
}

void SSimCopterNavigableMenu::Activate(uint32 UserIndex)
{
	const TSharedPtr<SWidget> Selected = ResolveSelection();
	if (!Selected) return;
	const FString Type = Selected->GetTypeAsString();
	if (Type == TEXT("SButton"))
	{
		StaticCastSharedPtr<SButton>(Selected)->SimulateClick();
	}
	else if (Type == TEXT("SCheckBox"))
	{
		Selected->OnKeyDown(Selected->GetCachedGeometry(), KeyEvent(EKeys::SpaceBar, UserIndex));
		Selected->OnKeyUp(Selected->GetCachedGeometry(), KeyEvent(EKeys::SpaceBar, UserIndex));
	}
	else
	{
		if (bEditingControl && Type.StartsWith(TEXT("SListView")))
		{
			OnKeyDown(GetCachedGeometry(), KeyEvent(EKeys::Enter, UserIndex));
			return;
		}
		const bool bWasEditing = bEditingControl;
		bEditingControl = !bWasEditing;
		if (Type.StartsWith(TEXT("SComboBox")) || Type == TEXT("SEditableTextBox"))
		{
			FSlateApplication::Get().SetUserFocus(UserIndex, Selected, EFocusCause::Navigation);
			if (bWasEditing || Type.StartsWith(TEXT("SComboBox")))
				ForwardControlKey(EKeys::Enter, UserIndex);
			if (bWasEditing)
				FSlateApplication::Get().SetUserFocus(UserIndex, SharedThis(this), EFocusCause::Navigation);
		}
		else
			FSlateApplication::Get().SetUserFocus(UserIndex,
				bEditingControl && Type.StartsWith(TEXT("SListView")) ? Selected.ToSharedRef() : SharedThis(this),
				EFocusCause::Navigation);
	}
}

FReply SSimCopterNavigableMenu::OnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (!bForwardingControlKey) bShowControllerSelection = Event.GetKey().IsGamepadKey();
	if (!Event.GetKey().IsGamepadKey()) return FReply::Unhandled();
	if (IsCapturingControllerBinding())
		return OnKeyDown(Geometry, Event.GetKey() == EKeys::Gamepad_FaceButton_Right
			? KeyEvent(EKeys::Escape, Event.GetUserIndex()) : Event);
	const FVector2D Direction = DirectionForKey(Event.GetKey());
	if (!Direction.IsZero())
	{
		MoveSelection(Direction, Event.GetUserIndex());
		return FReply::Handled();
	}
	if (Event.GetKey() == EKeys::Gamepad_FaceButton_Bottom)
	{
		if (!Event.IsRepeat()) Activate(Event.GetUserIndex());
		return FReply::Handled();
	}
	if (Event.GetKey() == EKeys::Gamepad_FaceButton_Right)
	{
		if (Event.IsRepeat()) return FReply::Handled();
		ResolveSelection(); // A native dropdown may already have confirmed and closed.
		if (bEditingControl)
		{
			FSlateApplication::Get().DismissAllMenus();
			bEditingControl = false;
			FSlateApplication::Get().SetUserFocus(Event.GetUserIndex(), SharedThis(this), EFocusCause::Navigation);
		}
		else if (GetTypeAsString() != TEXT("SSimCopterMainMenu"))
			OnKeyDown(Geometry, KeyEvent(EKeys::Escape, Event.GetUserIndex()));
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SSimCopterNavigableMenu::OnMouseMove(const FGeometry&, const FPointerEvent& Event)
{
	if (Event.GetCursorDelta().SizeSquared() > 4.0f) bShowControllerSelection = false;
	return FReply::Unhandled();
}

FReply SSimCopterNavigableMenu::OnPreviewMouseButtonDown(const FGeometry&, const FPointerEvent&)
{
	bShowControllerSelection = false;
	bEditingControl = false;
	return FReply::Unhandled();
}

FNavigationReply SSimCopterNavigableMenu::OnNavigation(const FGeometry& Geometry, const FNavigationEvent& Event)
{
	if (Event.GetNavigationGenesis() != ENavigationGenesis::Controller)
		return SCompoundWidget::OnNavigation(Geometry, Event);
	bShowControllerSelection = true;
	FVector2D Direction = FVector2D::ZeroVector;
	switch (Event.GetNavigationType())
	{
	case EUINavigation::Left: Direction.X = -1; break;
	case EUINavigation::Right: Direction.X = 1; break;
	case EUINavigation::Up: Direction.Y = -1; break;
	case EUINavigation::Down: Direction.Y = 1; break;
	default: break;
	}
	if (!Direction.IsZero()) MoveSelection(Direction, Event.GetUserIndex());
	return FNavigationReply::Stop();
}

int32 SSimCopterNavigableMenu::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& Culling, FSlateWindowElementList& Elements, int32 Layer,
	const FWidgetStyle& Style, bool bEnabled) const
{
	Layer = SCompoundWidget::OnPaint(Args, Geometry, Culling, Elements, Layer, Style, bEnabled);
	const TSharedPtr<SWidget> Selected = SelectedControl.Pin();
	if (!bShowControllerSelection || !Selected || !Selected->GetVisibility().IsVisible()) return Layer;
	const FGeometry& Target = Selected->GetCachedGeometry();
	const FVector2D TL = Geometry.AbsoluteToLocal(Target.LocalToAbsolute(FVector2D::ZeroVector));
	const FVector2D BR = Geometry.AbsoluteToLocal(Target.LocalToAbsolute(Target.GetLocalSize()));
	TArray<FVector2D> Points{TL, FVector2D(BR.X, TL.Y), BR, FVector2D(TL.X, BR.Y), TL};
	FSlateDrawElement::MakeLines(Elements, ++Layer, Geometry.ToPaintGeometry(), Points,
		ESlateDrawEffect::None, bEditingControl ? FLinearColor(0.2f, 1, 0.8f) : FLinearColor(1, 0.8f, 0.15f), true, 3.0f);
	const FString Hint = bEditingControl
		? TEXT("LS / D-pad  Adjust     A  Confirm     B  Back")
		: TEXT("LS / D-pad  Move     A  Confirm     B  Back");
	const FVector2f HintPosition(16, FMath::Max(0.0f, Geometry.GetLocalSize().Y - 28.0f));
	FSlateDrawElement::MakeBox(Elements, ++Layer,
		Geometry.ToPaintGeometry(FVector2f(410, 24), FSlateLayoutTransform(HintPosition - FVector2f(6, 3))),
		FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.85f));
	FSlateDrawElement::MakeText(Elements, ++Layer,
		Geometry.ToPaintGeometry(FVector2f(410, 24), FSlateLayoutTransform(HintPosition)),
		Hint, FCoreStyle::GetDefaultFontStyle("Regular", 11), ESlateDrawEffect::None, FLinearColor::White);
	return Layer;
}
