#include "Misc/AutomationTest.h"
#include "UI/SSimCopterIntro.h"
#include "InputCoreTypes.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterIntroInputTest, "SimCopter.FrontEnd.IntroInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterIntroInputTest::RunTest(const FString& Parameters)
{
	int32 Skips = 0;
	FSlateBrush Brush;
	const TSharedRef<SSimCopterIntro> Widget = SNew(SSimCopterIntro).MovieBrush(&Brush)
		.OnSkip(FSimpleDelegate::CreateLambda([&Skips]() { ++Skips; }));
	const FGeometry Geometry;
	for (const FKey Key : {EKeys::A, EKeys::Escape, EKeys::Enter, EKeys::Gamepad_FaceButton_Bottom,
		EKeys::Gamepad_Special_Right, EKeys::LeftShift})
	{
		const int32 Before = Skips;
		TestTrue(TEXT("Any key/button is consumed"), Widget->OnKeyDown(Geometry,
			FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0)).IsEventHandled());
		Widget->OnKeyDown(Geometry, FKeyEvent(Key, FModifierKeysState(), 0, true, 0, 0));
		Widget->OnKeyUp(Geometry, FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
		TestEqual(TEXT("A held button skips only once"), Skips, Before + 1);
	}
	for (const FKey Button : {EKeys::LeftMouseButton, EKeys::RightMouseButton, EKeys::MiddleMouseButton,
		EKeys::ThumbMouseButton, EKeys::ThumbMouseButton2})
	{
		const int32 Before = Skips;
		const FPointerEvent Event(0, FVector2D::ZeroVector, FVector2D::ZeroVector,
			TSet<FKey>{Button}, Button, 0, FModifierKeysState());
		TestTrue(TEXT("Every mouse button is consumed"), Widget->OnMouseButtonDown(Geometry, Event).IsEventHandled());
		Widget->OnMouseButtonUp(Geometry, Event);
		TestEqual(TEXT("Release does not skip again"), Skips, Before + 1);
	}
	return true;
}
#endif
