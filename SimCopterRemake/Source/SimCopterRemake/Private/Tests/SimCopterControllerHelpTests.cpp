#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/SimCopterControllerHelp.h"
#include "UI/SSimCopterControllerHelp.h"
#include "UI/SSimCopterToolGlow.h"
#include "UI/SimCopterHangarArt.h"
#include "Game/SimCopterPlayerController.h"
#include "GenericPlatform/InputDeviceRegistry.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"

using namespace SimCopterControllerHelp;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterHelpContextsTest, "SimCopter.Controller.ContextualHelp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterHelpContextsTest::RunTest(const FString&)
{
	FState State;
	State.bHasTool = true;
	State.bMegaphone = true;
	State.Message = TEXT("Evacuate");
	for (EContext Context : {EContext::Flight, EContext::Camera, EContext::ToolWheel,
		EContext::DispatchWheel, EContext::Passengers, EContext::ConfirmPassenger, EContext::OnFoot})
	{
		State.Context = Context;
		const auto Rows = BuildPrompts(State);
		TestEqual(TEXT("Hidden help contains only its persistent toggle"), Rows.Num(), 1);
		TestEqual(TEXT("Toggle uses a stick-click icon"), Rows[0].Icon, FString(TEXT("l3")));
		TestEqual(TEXT("Toggle explains how to restore help"), Rows[0].Label, FString(TEXT("Show help")));
	}
	State.Context = EContext::Flight;
	State.bExpanded = true;
	auto Rows = BuildPrompts(State);
	TestEqual(TEXT("Megaphone broadcast displays the actual message"), Rows[0].Label, FString(TEXT("Broadcast: Evacuate")));
	TestEqual(TEXT("Message changes use the horizontal D-pad icon"), Rows[1].Icon, FString(TEXT("horizontal")));
	TestFalse(TEXT("Airborne help omits exit"), Rows.ContainsByPredicate([](const FPrompt& Row) { return Row.Icon == TEXT("y"); }));
	State.bCanExit = true;
	Rows = BuildPrompts(State);
	TestTrue(TEXT("Landed and safe-to-exit context shows exit"), Rows.ContainsByPredicate([](const FPrompt& Row) { return Row.Icon == TEXT("y") && Row.Label == TEXT("Exit helicopter"); }));
	State.bCanExit = false;
	State.Context = EContext::DispatchWheel;
	Rows = BuildPrompts(State);
	TestFalse(TEXT("Dispatch does not show unrelated broadcast controls"), Rows.ContainsByPredicate([](const FPrompt& Row) { return Row.Icon == TEXT("x"); }));
	State.Context = EContext::Camera;
	Rows = BuildPrompts(State);
	TestFalse(TEXT("Camera help has no D-pad spotlight aim"), Rows.ContainsByPredicate([](const FPrompt& Row) { return Row.Icon.Contains(TEXT("dpad")) || Row.Icon == TEXT("vertical"); }));
	TestEqual(TEXT("Expanded help still ends with the same toggle"), Rows.Last().Icon, FString(TEXT("l3")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterHelpDeviceTest, "SimCopter.Controller.HelpDeviceStyle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterHelpDeviceTest::RunTest(const FString&)
{
	for (const TCHAR* Name : {TEXT("DualSense"), TEXT("DualShock4"), TEXT("PlayStation5"), TEXT("HID VID_054C&PID_0CE6")})
		TestTrue(TEXT("Sony hardware uses PlayStation glyphs"), DetectStyle(Name) == EStyle::PlayStation);
	TestTrue(TEXT("XInput uses the advertised Xbox layout"), DetectStyle(TEXT("XInputInterface XboxOne")) == EStyle::Xbox);
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	auto* PC = World->SpawnActor<ASimCopterPlayerController>();
	const FInputDeviceId Device = FInputDeviceId::CreateFromInternalId(777);
	FInputDeviceDescriptor Descriptor;
	Descriptor.HardwareDeviceHandle = Device;
	Descriptor.HardwareDeviceIdentifier = TEXT("DualSense");
	FInputDeviceRegistry::SetSimulatedDescriptor(Device, Descriptor);
	PC->NoteGamepadDevice(Device);
	TestTrue(TEXT("Active device descriptor drives the UI style"), PC->GetControllerIconStyle() == EStyle::PlayStation);
	Descriptor.HardwareDeviceIdentifier = TEXT("XboxOne");
	FInputDeviceRegistry::SetSimulatedDescriptor(Device, Descriptor);
	PC->NoteGamepadDevice(Device);
	TestTrue(TEXT("Switching the active controller updates icons"), PC->GetControllerIconStyle() == EStyle::Xbox);
	TestFalse(TEXT("Help starts collapsed"), PC->IsControllerHelpExpanded());
	PC->ToggleControllerHelp();
	TestTrue(TEXT("Stick click expands help"), PC->IsControllerHelpExpanded());
	PC->ToggleControllerHelp();
	TestFalse(TEXT("Second click collapses help"), PC->IsControllerHelpExpanded());
	FInputDeviceRegistry::ClearSimulatedDescriptor(Device);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterHelpPresentationTest, "SimCopter.Controller.HelpPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterHelpPresentationTest::RunTest(const FString&)
{
	for (const TCHAR* Style : {TEXT("xbox"), TEXT("playstation")})
		for (const TCHAR* Icon : {TEXT("a"), TEXT("b"), TEXT("x"), TEXT("y"), TEXT("lb"), TEXT("rb"), TEXT("l3"), TEXT("r3"), TEXT("rs"), TEXT("vertical"), TEXT("horizontal")})
			TestTrue(TEXT("Runtime icon file is present"), FPaths::FileExists(FPaths::ProjectContentDir() /
				TEXT("Slate/ControllerIcons") / (FString(Style) + TEXT("_") + Icon + TEXT(".png"))));
	if (!FParse::Param(FCommandLine::Get(), TEXT("SimControllerHelpPreview"))) return true;
	TStrongObjectPtr<USimCopterHangarArt> Art(NewObject<USimCopterHangarArt>());
	Art->SetOriginalGameRoot(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reference/SimCopterOriginalGame")));
	const FSlateBrush* Card = Art->GetBitmap(TEXT("FLAP1.BMP"));
	const FSlateBrush* Map = Art->GetBitmap(TEXT("DASH5.BMP"));
	if (!TestNotNull(TEXT("Tool card artwork loaded"), Card) || !TestNotNull(TEXT("Map artwork loaded"), Map)) return false;
	for (int32 Variant = 0; Variant < 3; ++Variant)
	{
		FState State;
		State.bExpanded = Variant != 2;
		State.bHasTool = true;
		State.bMegaphone = true;
		State.Message = TEXT("Evacuate");
		State.bCanExit = true;
		TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);
		Canvas->AddSlot().Offset(FMargin(0, 0, 900, 650)).Alignment(FVector2D::ZeroVector)
		[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(0.06f, 0.08f, 0.10f))];
		Canvas->AddSlot().Anchors(FAnchors(0, 1)).Offset(FMargin(0, 0, 0, 0)).AutoSize(true).Alignment(FVector2D(0, 1))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(FMargin(16, 0, 0, 16))
			[SNew(SSimCopterControllerHelp).State(State).IconStyle(Variant == 1 ? EStyle::PlayStation : EStyle::Xbox)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
			[SNew(SBox).WidthOverride(370).HeightOverride(296)[SNew(SImage).Image(Map)]]
		];
		Canvas->AddSlot().Offset(FMargin(590, 34, 276, 116)).Alignment(FVector2D::ZeroVector)
		[SNew(SSimCopterToolGlow).Active(true)[SNew(SImage).Image(Card)]];
		FWidgetRenderer Renderer(true);
		UTextureRenderTarget2D* Target = Renderer.DrawWidget(Canvas, FVector2D(900, 650));
		if (!TestNotNull(TEXT("Offscreen help rendered"), Target)) return false;
		TArray<FColor> Pixels;
		FReadSurfaceDataFlags Flags;
		Flags.SetLinearToGamma(false);
		Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, Flags);
		int32 VisibleArtworkPixels = 0;
		for (int32 Y = 44; Y < 140; ++Y)
			for (int32 X = 600; X < 856; ++X)
			{
				const FColor& Pixel = Pixels[Y * 900 + X];
				if (FMath::Min3(Pixel.R, Pixel.G, Pixel.B) < 235) ++VisibleArtworkPixels;
			}
		TestTrue(TEXT("Glow leaves the tool artwork visible instead of filling the card"), VisibleArtworkPixels > 6000);
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(900, 650, Pixels, Png);
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Docs/scratchpad") /
			FString::Printf(TEXT("controller-help-%d.png"), Variant));
		TestTrue(TEXT("Preview saved"), FFileHelper::SaveArrayToFile(Png, *Path));
	}
	return true;
}
#endif
