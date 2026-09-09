#pragma once

#include "Widgets/SCompoundWidget.h"

// Remake-only controller navigation over the actual arranged artwork, independent of tab order.
class SSimCopterNavigableMenu : public SCompoundWidget
{
public:
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnPreviewKeyDown(const FGeometry&, const FKeyEvent&) override;
	virtual FReply OnMouseMove(const FGeometry&, const FPointerEvent&) override;
	virtual FReply OnPreviewMouseButtonDown(const FGeometry&, const FPointerEvent&) override;
	virtual FNavigationReply OnNavigation(const FGeometry&, const FNavigationEvent&) override;
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&,
		FSlateWindowElementList&, int32, const FWidgetStyle&, bool) const override;
	static int32 FindSpatialTarget(const TArray<FSlateRect>& Rects, int32 Current, FVector2D Direction);

protected:
	// This full-screen input host paints its own control-level highlight. Slate's default
	// focus brush would outline the entire viewport when navigation restores focus here.
	virtual const FSlateBrush* GetFocusBrush() const override { return nullptr; }
	virtual void OnControllerSelectionChanged(int32 Index) {}
	virtual bool IsCapturingControllerBinding() const { return false; }

private:
	TWeakPtr<SWidget> SelectedControl;
	mutable TMap<const SWidget*, FGeometry> ControlGeometries;
	bool bEditingControl = false;
	bool bShowControllerSelection = false;
	bool bForwardingControlKey = false;
	void ForwardControlKey(FKey Key, uint32 UserIndex);
	void CollectControls(const TSharedRef<SWidget>& Widget, TArray<TSharedRef<SWidget>>& Out, const FGeometry& Geometry) const;
	TSharedPtr<SWidget> ResolveSelection();
	void MoveSelection(FVector2D Direction, uint32 UserIndex);
	void Activate(uint32 UserIndex);
};
