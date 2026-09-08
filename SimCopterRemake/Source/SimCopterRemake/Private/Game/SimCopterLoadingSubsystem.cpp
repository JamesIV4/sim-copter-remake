#include "Game/SimCopterLoadingSubsystem.h"

#include "Brushes/SlateDynamicImageBrush.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Misc/Paths.h"
#include "MoviePlayer.h"
#include "Kismet/GameplayStatics.h"
#include <atomic>
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/SLeafWidget.h"

namespace
{
// SCHOOK: CityLoadingScreen 0x00410e90. HRGLASS.SMK is centred on black;
// the original status rectangle is (width/2-180,height-40,width/2+180,height-20).
// Requested remake layout: smaller text centred just beneath the animation.
// Resource 630 is the first status. The executable advances through 630..644
// using worker milestones (DAT_005039d0), not an invented percentage timer.
class SSimCopterLoadingScreen final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterLoadingScreen) {}
	SLATE_END_ARGS()
	std::atomic<int32> Stage{0};
	void Construct(const FArguments& Args)
	{
		ForceVolatile(true); // frame UVs advance even while the game thread is blocked
		const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Generated/Loading/HRGLASS.png"));
		if (FPaths::FileExists(Path))
		{
			Atlas = MakeShared<FSlateDynamicImageBrush>(FName(*Path), FVector2D(3000, 1600));
			// Prepare the resource before the loading thread starts. No UObject/media
			// decoder access is needed while the game thread is blocked loading a map.
			Atlas->GetRenderingResource();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Original loading animation missing: run Tools/Unreal/BakeLoadingScreen.py."));
		}
		StartTime = FPlatformTime::Seconds();
	}
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(640, 480); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
		const FSlateRect& Culling, FSlateWindowElementList& Elements, int32 Layer,
		const FWidgetStyle& Style, bool bEnabled) const override
	{
		FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, FLinearColor::Black);
		const FVector2D Size = Geometry.GetLocalSize();
		const float Scale = FMath::Min(Size.X / 640.0, Size.Y / 480.0);
		if (Atlas.IsValid())
		{
			// SMK2 header: 75 frames, -10000 hundred-thousandths = 100 ms/frame.
			const int32 Frame = static_cast<int32>((FPlatformTime::Seconds() - StartTime) * 10.0) % 75;
			FSlateBrush Brush = *Atlas;
			Brush.SetUVRegion(FBox2f(FVector2f((Frame % 10) / 10.0f, (Frame / 10) / 8.0f),
				FVector2f((Frame % 10 + 1) / 10.0f, (Frame / 10 + 1) / 8.0f)));
			const FVector2D MovieSize(300 * Scale, 200 * Scale);
			FSlateDrawElement::MakeBox(Elements, Layer + 1,
				Geometry.ToPaintGeometry(MovieSize, FSlateLayoutTransform((Size - MovieSize) * 0.5)), &Brush);
		}
		static const TCHAR* Status[] = {
			TEXT("Calibrating lag-lead hinges"), TEXT("Balancing swashplate"), TEXT("Balancing swashplate"),
			TEXT("Tuning rotor tracking"), TEXT("Inspecting ground resonance damper"), TEXT("Inspecting ground resonance damper"),
			TEXT("Testing coning angle"), TEXT("Testing coning angle"), TEXT("Testing coning angle"),
			TEXT("Rectifying blade loading"), TEXT("Rectifying blade loading"), TEXT("Cleaning elastomeric bearings"),
			TEXT("Adjusting density altitude"), TEXT("Reticulating splines"), TEXT("Lubricating freewheel unit") };
		const FString Text = Status[FMath::Clamp(Stage.load(), 0, 14)];
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 10);
		const FVector2D TextSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font);
		const float TextScale = Scale / 2.0f;
		const FVector2D TextPosition((Size.X - TextSize.X * TextScale) * 0.5,
			Size.Y * 0.5 + (100.0f + 12.0f) * Scale);
		FSlateDrawElement::MakeText(Elements, Layer + 2,
			Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(TextScale, TextPosition)),
			Text, Font,
			ESlateDrawEffect::None, FLinearColor::White);
		return Layer + 2;
	}
private:
	TSharedPtr<FSlateDynamicImageBrush> Atlas;
	double StartTime = 0;
};
}

void USimCopterLoadingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &USimCopterLoadingSubsystem::BeforeMapLoad);
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &USimCopterLoadingSubsystem::AfterMapLoad);
}

void USimCopterLoadingSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PreLoadMap.RemoveAll(this);
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
	Finish();
	Super::Deinitialize();
}

void USimCopterLoadingSubsystem::BeforeMapLoad(const FString& MapName)
{
	if (MapName.Contains(TEXT("CityRender"))) Show();
	else Finish();
}

void USimCopterLoadingSubsystem::AfterMapLoad(UWorld* World)
{
	// Failed travel must not strand a manual-stop loading screen.
	if (World == nullptr || !World->GetMapName().Contains(TEXT("CityRender"))) Finish();
}

void USimCopterLoadingSubsystem::Show()
{
	if (LoadingWidget.IsValid() || !FSlateApplication::IsInitialized() || IsRunningDedicatedServer()) return;
	LoadingWidget = SNew(SSimCopterLoadingScreen);
	if (IsMoviePlayerEnabled())
	{
		FLoadingScreenAttributes Attributes;
		Attributes.WidgetLoadingScreen = LoadingWidget;
		Attributes.bAutoCompleteWhenLoadingCompletes = false;
		Attributes.bWaitForManualStop = true;
		Attributes.bMoviesAreSkippable = false;
		Attributes.bAllowEngineTick = true; // airport placement is a next-tick callback
		GetMoviePlayer()->SetupLoadingScreen(Attributes);
		bPlayingMovie = GetMoviePlayer()->PlayMovie();
	}
	if (!bPlayingMovie && GetGameInstance()->GetGameViewportClient() != nullptr)
	{
		GetGameInstance()->GetGameViewportClient()->AddViewportWidgetContent(LoadingWidget.ToSharedRef(), 10000);
		bInViewport = true;
	}
}

void USimCopterLoadingSubsystem::Finish()
{
	if (bPlayingMovie && IsMoviePlayerEnabled()) GetMoviePlayer()->StopMovie();
	if (bInViewport && GetGameInstance()->GetGameViewportClient() != nullptr && LoadingWidget.IsValid())
		GetGameInstance()->GetGameViewportClient()->RemoveViewportWidgetContent(LoadingWidget.ToSharedRef());
	bPlayingMovie = false;
	bInViewport = false;
	LoadingWidget.Reset();
}

void USimCopterLoadingSubsystem::SetStage(const UObject* WorldContext, int32 Stage)
{
	if (UGameInstance* Instance = UGameplayStatics::GetGameInstance(WorldContext))
	{
		if (USimCopterLoadingSubsystem* Loading = Instance->GetSubsystem<USimCopterLoadingSubsystem>();
			Loading != nullptr && Loading->LoadingWidget.IsValid())
		{
			StaticCastSharedPtr<SSimCopterLoadingScreen>(Loading->LoadingWidget)->Stage.store(Stage);
		}
	}
}
