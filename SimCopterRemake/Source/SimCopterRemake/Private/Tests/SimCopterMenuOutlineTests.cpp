#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/SSimCopterMainMenu.h"
#include "UI/SSimCopterSettingsMenu.h"
#include "UI/SSimCopterMessageBox.h"
#include "UI/SimCopterHangarArt.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterMenuOutlineTest, "SimCopter.Controller.MenuOutlineArt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterMenuOutlineTest::RunTest(const FString&)
{
	using namespace SimCopterFrontEnd;
	for (int32 Row = 0; Row < SimCopterMainMenuLayout::ItemCount; ++Row)
	{
		const FRect Hit = SimCopterMainMenuLayout::GetItemHitRect(Row);
		const FRect Art = SimCopterMainMenuLayout::GetItemFocusRect(Row);
		const auto Outline = MakeFocusOutline(Hit, Art, 4);
		TestTrue(TEXT("Main-menu outline excludes lever and lamp"), Art.Left > Hit.Left && Art.Right < Hit.Right);
		TestTrue(TEXT("Outline spans the bezel, not the short text band"), Art.Top < Hit.Top && Art.Bottom > Hit.Bottom);
		for (float Scale : {0.75f, 1.0f, 2.5f})
		{
			const FSlateRect Scaled = Outline->GetLocalBounds(FVector2D(Hit.Width(), Hit.Height()) * Scale);
			TestTrue(TEXT("Art alignment survives viewport scaling"),
				FMath::IsNearlyEqual(Scaled.Left + Hit.Left * Scale, Art.Left * Scale) &&
				FMath::IsNearlyEqual(Scaled.Bottom + Hit.Top * Scale, Art.Bottom * Scale));
		}
	}
	for (bool bCity : {false, true})
	{
		const FRect Hit = SimCopterSettingsMenuLayout::GetRowHitRect(0, bCity);
		const FRect Art = SimCopterSettingsMenuLayout::GetRowFocusRect(0, bCity);
		TestTrue(TEXT("Both pause-menu variants exclude the lever strip"), Art.Left > Hit.Left && Art.Right < Hit.Right);
	}
	if (!FParse::Param(FCommandLine::Get(), TEXT("SimMenuOutlinePreview"))) return true;
	TStrongObjectPtr<USimCopterHangarArt> Art(NewObject<USimCopterHangarArt>());
	Art->SetOriginalGameRoot(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reference/SimCopterOriginalGame")));
	for (int32 Screen = 0; Screen < 4; ++Screen)
	{
		TSharedPtr<SSimCopterNavigableMenu> Menu;
		if (Screen == 0) Menu = SNew(SSimCopterMainMenu).Art(Art.Get());
		else if (Screen < 3) Menu = SNew(SSimCopterSettingsMenu).Art(Art.Get()).AllowCitySettings(Screen == 1);
		else Menu = SNew(SSimCopterMessageBox).Art(Art.Get()).Confirm(true).Message(FText::FromString(TEXT("Return to the main menu?")));
		FWidgetRenderer Renderer(true);
		const FVector2D Size(1280, 960);
		Renderer.DrawWidget(Menu.ToSharedRef(), Size);
		Menu->OnPreviewKeyDown(Menu->GetCachedGeometry(), FKeyEvent(EKeys::Gamepad_DPad_Down, FModifierKeysState(), 0, false, 0, 0));
		UTextureRenderTarget2D* Target = Renderer.DrawWidget(Menu.ToSharedRef(), Size);
		if (!TestNotNull(TEXT("Menu preview rendered"), Target)) return false;
		TArray<FColor> Pixels;
		FReadSurfaceDataFlags Flags;
		Flags.SetLinearToGamma(false);
		Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, Flags);
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(1280, 960, Pixels, Png);
		TestTrue(TEXT("Preview saved"), FFileHelper::SaveArrayToFile(Png,
			*(FPaths::ProjectDir() / TEXT("../Docs/scratchpad") / FString::Printf(TEXT("menu-outline-%d.png"), Screen))));
	}
	return true;
}
#endif
