#pragma once
#include "Types/ISlateMetaData.h"
#include "Layout/SlateRect.h"

// Visible artwork and generous mouse hit regions need not have the same bounds.
// Normalized coordinates preserve the authored outline through page/HUD scaling.
class FSimCopterMenuFocusOutline : public ISlateMetaData
{
public:
	SLATE_METADATA_TYPE(FSimCopterMenuFocusOutline, ISlateMetaData)
	FSimCopterMenuFocusOutline(const FSlateRect& Hit, const FSlateRect& Art, float Radius)
		: RadiusPerHeight(Radius / Hit.GetSize().Y)
		, Bounds((Art.Left - Hit.Left) / Hit.GetSize().X, (Art.Top - Hit.Top) / Hit.GetSize().Y,
			(Art.Right - Hit.Left) / Hit.GetSize().X, (Art.Bottom - Hit.Top) / Hit.GetSize().Y) {}
	FSlateRect GetLocalBounds(FVector2D Size) const
	{
		return FSlateRect(Bounds.Left * Size.X, Bounds.Top * Size.Y, Bounds.Right * Size.X, Bounds.Bottom * Size.Y);
	}
	float RadiusPerHeight;
private:
	FSlateRect Bounds;
};
