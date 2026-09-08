#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/SSimCopterNavigableMenu.h"
#include "UI/SSimCopterCheckupSlider.h"
#include "UI/SSimCopterMainMenu.h"
#include "UI/SSimCopterMessageBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/HittestGrid.h"
#include "Types/PaintArgs.h"
#include "Widgets/SVirtualWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"

namespace
{
// Real Slate arrangement and paint populate geometry without opening an OS window or a city.
TSharedRef<SVirtualWindow> ArrangeMenu(const TSharedRef<SWidget>& Menu)
{
	TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow).Size(FVector2D(640, 480));
	Window->SetContent(Menu);
	Window->SlatePrepass();
	FHittestGrid Grid;
	Grid.SetHittestArea(FVector2D::ZeroVector, FVector2D(640, 480));
	FSlateWindowElementList Elements(Window);
	FPaintArgs Args(nullptr, Grid, FVector2D::ZeroVector, 0, 0);
	Window->Paint(Args, FGeometry::MakeRoot(FVector2D(640, 480), FSlateLayoutTransform()),
		FSlateRect(0, 0, 640, 480), Elements, 0, FWidgetStyle(), true);
	return Window;
}

FReply Press(const TSharedRef<SSimCopterNavigableMenu>& Menu, FKey Key, bool bRepeat = false)
{
	return Menu->OnPreviewKeyDown(Menu->GetCachedGeometry(),
		FKeyEvent(Key, FModifierKeysState(), 0, bRepeat, 0, 0));
}

class SNavigationTestMenu : public SSimCopterNavigableMenu
{
public:
	SLATE_BEGIN_ARGS(SNavigationTestMenu) {} SLATE_END_ARGS()
	void Construct(const FArguments&)
	{
		TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);
		// Deliberately shuffled construction order; the next item is spatial, never index + 1.
		const FVector2D Positions[] = {{20, 20}, {20, 180}, {200, 20}, {110, 20}};
		for (int32 Index = 0; Index < 4; ++Index)
			Canvas->AddSlot().Offset(FMargin(Positions[Index].X, Positions[Index].Y, 70, 40)).Alignment(FVector2D::ZeroVector)
			[
				SNew(SButton).IsFocusable(false).IsEnabled(Index != 3)
				.OnClicked_Lambda([this, Index] { Clicked = Index; ++Clicks; return FReply::Handled(); })
			];
		Canvas->AddSlot().Offset(FMargin(200, 180, 100, 30)).Alignment(FVector2D::ZeroVector)
		[
			SAssignNew(Slider, SSimCopterCheckupSlider).Orientation(Orient_Horizontal)
		];
		ChildSlot[Canvas];
	}
	int32 Clicked = INDEX_NONE;
	int32 Clicks = 0;
	int32 Cancels = 0;
	TSharedPtr<SSimCopterCheckupSlider> Slider;
	virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& Event) override
	{
		if (Event.GetKey() == EKeys::Escape) { ++Cancels; return FReply::Handled(); }
		return FReply::Unhandled();
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterMenuSpatialTest, "SimCopter.Controller.MenuSpatialNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterMenuSpatialTest::RunTest(const FString&)
{
	TSharedRef<SNavigationTestMenu> Menu = SNew(SNavigationTestMenu);
	ArrangeMenu(Menu);
	Press(Menu, EKeys::Gamepad_DPad_Right);
	Press(Menu, EKeys::Gamepad_FaceButton_Bottom);
	TestEqual(TEXT("Right chooses the right-hand control, skipping disabled and construction order"), Menu->Clicked, 2);
	Press(Menu, EKeys::Gamepad_FaceButton_Bottom, true);
	TestEqual(TEXT("Held A cannot confirm twice"), Menu->Clicks, 1);
	Menu->OnNavigation(Menu->GetCachedGeometry(), FNavigationEvent(FModifierKeysState(), 0,
		EUINavigation::Down, ENavigationGenesis::Controller));
	Press(Menu, EKeys::Gamepad_FaceButton_Bottom);
	Press(Menu, EKeys::Gamepad_DPad_Right);
	TestEqual(TEXT("A enters slider adjustment"), Menu->Slider->GetValue(), 0.05f);
	Press(Menu, EKeys::Gamepad_FaceButton_Right);
	TestEqual(TEXT("B exits adjustment without cancelling the page"), Menu->Cancels, 0);
	Press(Menu, EKeys::Gamepad_FaceButton_Right);
	TestEqual(TEXT("A second B cancels the page"), Menu->Cancels, 1);
	const TArray<FSlateRect> Rects{FSlateRect(0, 0, 50, 50), FSlateRect(100, 0, 150, 50), FSlateRect(0, 100, 50, 150)};
	TestEqual(TEXT("At the edge focus stays put"), SSimCopterNavigableMenu::FindSpatialTarget(Rects, 0, {-1, 0}), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterMenuConfirmTest, "SimCopter.Controller.MenuConfirmCancel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterMenuConfirmTest::RunTest(const FString&)
{
	int32 Chosen = INDEX_NONE;
	TSharedRef<SSimCopterMainMenu> Menu = SNew(SSimCopterMainMenu)
		.OnItemChosen_Lambda([&Chosen](ESimCopterMainMenuItem Item) { Chosen = static_cast<int32>(Item); });
	ArrangeMenu(Menu);
	Press(Menu, EKeys::Gamepad_FaceButton_Right);
	TestEqual(TEXT("B at the root does not quit the application"), Chosen, INDEX_NONE);
	Press(Menu, EKeys::Gamepad_DPad_Down);
	Press(Menu, EKeys::Gamepad_FaceButton_Bottom);
	TestEqual(TEXT("Main menu A invokes the spatially selected original action"), Chosen, 1);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterMenuRoutedInputTest, "SimCopter.Controller.MenuRoutedInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterMenuRoutedInputTest::RunTest(const FString&)
{
	FSlateApplication& Slate = FSlateApplication::Get();
	const TSharedPtr<SWidget> PreviousFocus = Slate.GetUserFocusedWidget(0);
	TSharedRef<SNavigationTestMenu> Menu = SNew(SNavigationTestMenu);
	TSharedRef<SVirtualWindow> Window = ArrangeMenu(Menu);
	Slate.RegisterVirtualWindow(Window);
	Slate.SetUserFocus(0, Menu, EFocusCause::SetDirectly);
	TestTrue(TEXT("Menu acquires actual Slate user focus"), Slate.GetUserFocusedWidget(0) == Menu);
	Slate.ProcessAnalogInputEvent(FAnalogInputEvent(EKeys::Gamepad_LeftX, FModifierKeysState(), 0, false, 0, 0, 1.0f));
	Slate.ProcessKeyDownEvent(FKeyEvent(EKeys::Gamepad_FaceButton_Bottom, FModifierKeysState(), 0, false, 0, 0));
	TestEqual(TEXT("Actual left-stick event and A route to the right-hand button"), Menu->Clicked, 2);
	Slate.ProcessAnalogInputEvent(FAnalogInputEvent(EKeys::Gamepad_LeftX, FModifierKeysState(), 0, false, 0, 0, 0.0f));
	Slate.ProcessKeyDownEvent(FKeyEvent(EKeys::Gamepad_DPad_Left, FModifierKeysState(), 0, false, 0, 0));
	Slate.ProcessKeyDownEvent(FKeyEvent(EKeys::Gamepad_FaceButton_Bottom, FModifierKeysState(), 0, false, 0, 0));
	TestEqual(TEXT("Actual D-pad shares spatial routing"), Menu->Clicked, 0);
	Slate.SetUserFocus(0, PreviousFocus, EFocusCause::SetDirectly);
	Slate.UnregisterVirtualWindow(Window);
	return true;
}
#endif
