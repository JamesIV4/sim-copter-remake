// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterParticleFX.h"
#include "Ground/SimCopterApachePool.h"
#include "Ground/SimCopterTearGasPool.h"
#include "Formats/MaxisMeshLibrary.h"
#include "Ground/SimCopterEffectFX.h"
#include "Ground/SimCopterEffectRasterizer.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterEffectsPoolTest, "SimCopter.Effects.PoolParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterEffectsPoolTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("SMOKE pool"), USimCopterParticleFXComponent::GetPoolCapacity(ESimCopterEffectPool::Smoke10), 10);
	TestEqual(TEXT("DEBRIS pool"), USimCopterParticleFXComponent::GetPoolCapacity(ESimCopterEffectPool::Debris30), 30);
	TestEqual(TEXT("wash trajectory pool"), USimCopterParticleFXComponent::GetPoolCapacity(ESimCopterEffectPool::Wash20), 20);
	TestEqual(TEXT("three point trajectory pool"), USimCopterParticleFXComponent::GetPoolCapacity(ESimCopterEffectPool::Trajectory70), 70);
	TestEqual(TEXT("fire pool"), USimCopterParticleFXComponent::GetPoolCapacity(ESimCopterEffectPool::Fire25), 25);
	TestEqual(TEXT("two-slot GEO pool"), USimCopterParticleFXComponent::GetPoolCapacity(ESimCopterEffectPool::Geo2), 2);
	TestEqual(TEXT("splash-column pool"), USimCopterParticleFXComponent::GetPoolCapacity(ESimCopterEffectPool::SplashColumns20), 20);
	TestEqual(TEXT("tile-puff pool"), USimCopterParticleFXComponent::GetPoolCapacity(ESimCopterEffectPool::TilePuffs100), 100);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterEffectsPrimitiveTest, "SimCopter.Effects.PrimitiveParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterEffectsPrimitiveTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("bucket drip has three points"), USimCopterParticleFXComponent::GetPointCountForType(ESimCopterEffectType::BucketDrip), 3);
	TestEqual(TEXT("splash sub-particle has three points"), USimCopterParticleFXComponent::GetPointCountForType(ESimCopterEffectType::SplashSubParticle), 3);
	TestEqual(TEXT("rotor wash has one point"), USimCopterParticleFXComponent::GetPointCountForType(ESimCopterEffectType::RotorWash), 1);
	TestEqual(TEXT("fire ember has four points"), USimCopterParticleFXComponent::GetPointCountForType(ESimCopterEffectType::BuildingFireEmber), 4);
	TestEqual(TEXT("type-9 fixed lifetime"), USimCopterParticleFXComponent::GetLifetime1616ForType(ESimCopterEffectType::SplashSubParticle), 0xE666);
	TestEqual(TEXT("type-7 fixed lifetime"), USimCopterParticleFXComponent::GetLifetime1616ForType(ESimCopterEffectType::SmallSpray), 0x1CCCC);
	TestEqual(TEXT("debris fixed lifetime"), USimCopterParticleFXComponent::GetLifetime1616ForType(ESimCopterEffectType::Debris), 0x1E0000);
	TestEqual(TEXT("bucket face type"), USimCopterParticleFXComponent::GetFaceTypeForType(ESimCopterEffectType::BucketDrip), 0x1A);
	TestEqual(TEXT("fire face type"), USimCopterParticleFXComponent::GetFaceTypeForType(ESimCopterEffectType::BuildingFireEmber), 0x17);
	TestEqual(TEXT("splash ring particle count"), USimCopterParticleFXComponent::GetSplashRingParticleCount(), 14);
	TestEqual(TEXT("all tile puffs use the fixed two-second countdown"),
		USimCopterParticleFXComponent::GetTilePuffLife1616(), 0x20000);
	TestTrue(TEXT("trajectory card size is 20 original units"),
		FMath::IsNearlyEqual(
			USimCopterParticleFXComponent::GetDefaultSizeCmForType(ESimCopterEffectType::BucketDrip),
			20.0f * SimCopterEffectFX::OriginalUnitToCm));
	TestTrue(TEXT("class-8 puff uses default 15-unit rise speed"),
		FMath::IsNearlyEqual(
			USimCopterParticleFXComponent::GetTilePuffRiseSpeedCmPerSec(8),
			15.0f * SimCopterEffectFX::OriginalUnitToCm));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterEffectsRuntimeSaveRoundTripTest,
	"SimCopter.Effects.RuntimeSaveRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterEffectsRuntimeSaveRoundTripTest::RunTest(const FString& Parameters)
{
	USimCopterParticleFXComponent* Effects = NewObject<USimCopterParticleFXComponent>();
	TestTrue(TEXT("Water/effect fixture allocates"),
		Effects->SpawnEffect(ESimCopterEffectType::BucketDrip, FVector(1, 2, 3), FVector(4, 5, 6)));
	TArray<uint8> EffectData;
	TestTrue(TEXT("Water/effect pool writes"), Effects->CaptureRuntimeSaveState(EffectData));
	USimCopterParticleFXComponent* RestoredEffects = NewObject<USimCopterParticleFXComponent>();
	TestTrue(TEXT("Water/effect pool reads"), RestoredEffects->RestoreRuntimeSaveState(EffectData));
	TestEqual(TEXT("Water trajectory resumes"),
		RestoredEffects->GetActiveCount(ESimCopterEffectPool::Trajectory70), 1);

	USimCopterTearGasPoolComponent* TearGas = NewObject<USimCopterTearGasPoolComponent>();
	TestTrue(TEXT("Tear-gas fixture launches"),
		TearGas->Launch(FVector(10, 20, 30), FVector::ForwardVector, 0, 77));
	TArray<uint8> TearGasData;
	TestTrue(TEXT("Tear-gas pool writes"), TearGas->CaptureRuntimeSaveState(TearGasData));
	USimCopterTearGasPoolComponent* RestoredTearGas = NewObject<USimCopterTearGasPoolComponent>();
	TestTrue(TEXT("Tear-gas pool reads"), RestoredTearGas->RestoreRuntimeSaveState(TearGasData));
	TestEqual(TEXT("Tear-gas canister resumes"), RestoredTearGas->GetActiveCanisterCount(), 1);

	USimCopterApachePoolComponent* Apache = NewObject<USimCopterApachePoolComponent>();
	TestTrue(TEXT("Apache tracer fixture launches"),
		Apache->LaunchBullet(FVector(100, 200, 300), FVector::DownVector, 0));
	TArray<uint8> ApacheData;
	TestTrue(TEXT("Apache pools write"), Apache->CaptureRuntimeSaveState(ApacheData));
	USimCopterApachePoolComponent* RestoredApache = NewObject<USimCopterApachePoolComponent>();
	TestTrue(TEXT("Apache pools read"), RestoredApache->RestoreRuntimeSaveState(ApacheData));
	TestEqual(TEXT("Apache tracer resumes"), RestoredApache->GetActiveBulletCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterApacheMissionBlastDefaultsTest,
	"SimCopter.Effects.ApacheMissionBlastDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterApacheMissionBlastDefaultsTest::RunTest(const FString& Parameters)
{
	const USimCopterApachePoolComponent* Apache = NewObject<USimCopterApachePoolComponent>();
	TestEqual(
		TEXT("Missile mission blast is 24 original units"),
		Apache->GetMissileMissionBlastRadiusCm(),
		USimCopterApachePoolComponent::DefaultMissileMissionBlastRadiusCm);
	TestEqual(
		TEXT("Machine-gun mission blast is 8 original units"),
		Apache->GetMachineGunMissionBlastRadiusCm(),
		USimCopterApachePoolComponent::DefaultMachineGunMissionBlastRadiusCm);
	TestTrue(
		TEXT("Missile mission blast is larger than the machine-gun strike"),
		Apache->GetMissileMissionBlastRadiusCm() > Apache->GetMachineGunMissionBlastRadiusCm());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterEffectsRasterizerTest,
	"SimCopter.Effects.OriginalRasterizerParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterEffectsRasterizerTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("0x10 renderer gameplay viewport width"),
		FSimCopterEffectRasterizer::OriginalViewportWidth, 560);
	TestEqual(TEXT("0x10 renderer gameplay viewport height"),
		FSimCopterEffectRasterizer::OriginalViewportHeight, 400);
	TestEqual(TEXT("0x10 renderer framebuffer stride"),
		FSimCopterEffectRasterizer::OriginalFramebufferStride, 640);
	TestEqual(TEXT("0x10 renderer framebuffer height"),
		FSimCopterEffectRasterizer::OriginalFramebufferHeight, 480);
	TestEqual(TEXT("alternate renderer gameplay viewport width"),
		FSimCopterEffectRasterizer::LowResolutionViewportWidth, 280);
	TestEqual(TEXT("alternate renderer gameplay viewport height"),
		FSimCopterEffectRasterizer::LowResolutionViewportHeight, 200);
	// FUN_0046f2ca: focal = (viewportWidth << 11) * 0x1bb6 >> 12, i.e. half the
	// viewport width times sqrt(3) - a 60-degree horizontal frustum.
	TestTrue(TEXT("0x1bb6 projection ratio is the decoded 20.12 constant"),
		FMath::IsNearlyEqual(
			FSimCopterEffectRasterizer::OriginalProjectionRatio,
			1.7319336f,
			1.0e-6f));
	TestTrue(TEXT("original focal length is 484.94 viewport pixels"),
		FMath::IsNearlyEqual(
			FSimCopterEffectRasterizer::OriginalFocalLengthPixels,
			484.9414f,
			1.0e-3f));
	TestTrue(TEXT("half the viewport width subtends a 30-degree half angle"),
		FMath::IsNearlyEqual(
			FMath::RadiansToDegrees(FMath::Atan(
				FSimCopterEffectRasterizer::OriginalViewportWidth * 0.5f /
				FSimCopterEffectRasterizer::OriginalFocalLengthPixels)),
			30.0f,
			0.01f));
	// One viewport pixel at the focal distance is exactly one world unit, and the
	// size stays proportional to depth - it must never depend on the live camera.
	TestTrue(TEXT("one pixel at focal depth spans one world unit"),
		FMath::IsNearlyEqual(
			FSimCopterEffectRasterizer::GetWorldSizePerViewportPixel(
				FSimCopterEffectRasterizer::OriginalFocalLengthPixels),
			1.0f,
			1.0e-4f));
	TestTrue(TEXT("pixel world size scales linearly with depth"),
		FMath::IsNearlyEqual(
			FSimCopterEffectRasterizer::GetWorldSizePerViewportPixel(3000.0f),
			3000.0f / 484.9414f,
			1.0e-4f));

	const FVector OriginalX =
		SimCopterEffectFX::OriginalOffsetToCityLocalCm(0x10000, 0, 0);
	const FVector OriginalUp =
		SimCopterEffectFX::OriginalOffsetToCityLocalCm(0, 0x10000, 0);
	const FVector OriginalZ =
		SimCopterEffectFX::OriginalOffsetToCityLocalCm(0, 0, 0x10000);
	TestEqual(TEXT("source X maps through city yaw to Unreal -Y"),
		OriginalX, FVector(0.0f, -SimCopterEffectFX::OriginalUnitToCm, 0.0f));
	TestEqual(TEXT("source Y-up maps to Unreal Z"),
		OriginalUp, FVector(0.0f, 0.0f, SimCopterEffectFX::OriginalUnitToCm));
	TestEqual(TEXT("source Z maps through city yaw to Unreal -X"),
		OriginalZ, FVector(-SimCopterEffectFX::OriginalUnitToCm, 0.0f, 0.0f));
	TestEqual(
		TEXT("zero depth retains 1.0 16.16 scale"),
		FSimCopterEffectRasterizer::ComputeDepthScale1616(0.0f),
		0x10000);
	TestEqual(
		TEXT("0x590 original-unit far depth rejects the effect"),
		FSimCopterEffectRasterizer::ComputeDepthScale1616(
			1424.0f * SimCopterEffectFX::OriginalUnitToCm),
		0);

	const FSimCopterEffectKernelMetrics Fire =
		FSimCopterEffectRasterizer::ComputeKernelMetrics(2, 0x10000);
	TestEqual(TEXT("class-2 iteration count"), Fire.Iterations, 20);
	TestEqual(TEXT("class-2 doubled jitter half extent"), Fire.JitterHalfExtentPixels, 40);
	TestEqual(TEXT("class-2 doubled jitter modulo span"), Fire.JitterSpanPixels, 80);
	TestEqual(TEXT("class-2 minimum radius"), Fire.MinRadius, 8);
	TestEqual(TEXT("class-2 radius choices"), Fire.RadiusChoiceCount, 2);

	const FSimCopterEffectKernelMetrics Water =
		FSimCopterEffectRasterizer::ComputeKernelMetrics(0, 0x10000);
	TestEqual(TEXT("class-0 iteration count"), Water.Iterations, 15);
	TestEqual(TEXT("class-0 minimum radius"), Water.MinRadius, 2);
	TestEqual(TEXT("class-0 radius choices"), Water.RadiusChoiceCount, 6);

	static constexpr uint8 FireSelector[] = {
		0x13, 0x17, 0x73, 0x7b, 0x64, 0x1d, 0x1f, 0x7f
	};
	static constexpr uint8 SmokeSelector[] = {
		0x3a, 0x3a, 0x3b, 0x3a, 0x3c, 0x3a, 0x39, 0x3a
	};
	static constexpr uint8 WaterSelector[] = {
		0x94, 0xa8, 0x96, 0xaa, 0x9a, 0xac, 0x92, 0xab
	};
	for (int32 Phase = 0; Phase < 8; ++Phase)
	{
		TestEqual(
			FString::Printf(TEXT("class-2 selector phase %d"), Phase),
			FSimCopterEffectRasterizer::GetSelectorPaletteIndex(2, Phase),
			FireSelector[Phase]);
		TestEqual(
			FString::Printf(TEXT("class-1 selector phase %d"), Phase),
			FSimCopterEffectRasterizer::GetSelectorPaletteIndex(1, Phase),
			SmokeSelector[Phase]);
		TestEqual(
			FString::Printf(TEXT("class-0 selector phase %d"), Phase),
			FSimCopterEffectRasterizer::GetSelectorPaletteIndex(0, Phase),
			WaterSelector[Phase]);
	}

	const FSimCopterEffectStencilMetrics Radius4 =
		FSimCopterEffectRasterizer::GetStencilMetrics(4);
	TestEqual(TEXT("radius-4 stencil width"), Radius4.Width, 9);
	TestEqual(TEXT("radius-4 stencil height"), Radius4.Height, 9);
	TestEqual(TEXT("radius-4 selector advances"), Radius4.SelectorAdvance, 5);
	TestEqual(TEXT("radius-4 top-right selector reuse"),
		FSimCopterEffectRasterizer::GetStencilPhaseOffset(4, 8, 0), 1);
	TestEqual(TEXT("radius-4 second row is untouched"),
		FSimCopterEffectRasterizer::GetStencilPhaseOffset(4, 4, 1), INDEX_NONE);

	// The radius-8 case has no terminal jump in the executable and falls into
	// radius 9. Preserve that unusual 35x35 L-shaped write footprint exactly.
	const FSimCopterEffectStencilMetrics Radius8 =
		FSimCopterEffectRasterizer::GetStencilMetrics(8);
	TestEqual(TEXT("radius-8 fall-through width"), Radius8.Width, 35);
	TestEqual(TEXT("radius-8 fall-through height"), Radius8.Height, 35);
	TestEqual(TEXT("radius-8 selector advances"), Radius8.SelectorAdvance, 24);
	TestEqual(TEXT("radius-8 fall-through starts at x16 on row18"),
		FSimCopterEffectRasterizer::GetStencilPhaseOffset(8, 16, 18), 4);
	TestEqual(TEXT("radius-8 leaves left half of row18 untouched"),
		FSimCopterEffectRasterizer::GetStencilPhaseOffset(8, 0, 18), INDEX_NONE);
	TestEqual(TEXT("radius-8 terminal selector phase"),
		FSimCopterEffectRasterizer::GetStencilPhaseOffset(8, 34, 34), 7);

	uint32 RandomState = 1;
	TestEqual(TEXT("MSVC rand sample 1"), FSimCopterEffectRasterizer::AdvanceRandom(RandomState), 41u);
	TestEqual(TEXT("MSVC rand sample 2"), FSimCopterEffectRasterizer::AdvanceRandom(RandomState), 18467u);
	TestEqual(TEXT("MSVC rand sample 3"), FSimCopterEffectRasterizer::AdvanceRandom(RandomState), 6334u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterEffectsRotorWashTest, "SimCopter.Effects.RotorWashParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterEffectsRotorWashTest::RunTest(const FString& Parameters)
{
	const float Unit = SimCopterEffectFX::OriginalUnitToCm;
	TestFalse(TEXT("rejects rotor speed at the strict 280 threshold"),
		USimCopterParticleFXComponent::IsRotorWashEligible(19.0f * Unit, 0x1180000));
	TestTrue(TEXT("accepts low surface and rotor speed above 280"),
		USimCopterParticleFXComponent::IsRotorWashEligible(19.0f * Unit, 0x1180001));
	TestFalse(TEXT("rejects the strict 20-unit height threshold"),
		USimCopterParticleFXComponent::IsRotorWashEligible(20.0f * Unit, 0x1180001));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterEffectsFireMarkerAssetsTest,
	"SimCopter.Effects.FireMarkerAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterEffectsFireMarkerAssetsTest::RunTest(const FString& Parameters)
{
	const FString OriginalRoot = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame")));
	FMaxisMeshLibrary MeshLibrary;
	FString Error;
	if (!TestTrue(TEXT("loads original effect assets"), MeshLibrary.LoadFromOriginalGameRoot(OriginalRoot, Error)))
	{
		AddError(Error);
		return false;
	}

	const FMaxisMeshObject* FirePoints = MeshLibrary.FindObjectByObjectId(0x120);
	if (!TestNotNull(TEXT("finds FIREPTS marker template"), FirePoints))
	{
		return false;
	}
	TestEqual(TEXT("FIREPTS marker count"), FirePoints->Faces.Num(), 22);
	int32 SmokeMarkers = 0;
	int32 FireMarkers = 0;
	for (const FMaxisMeshFace& Face : FirePoints->Faces)
	{
		TestEqual(TEXT("FIREPTS uses point-effect face type"), Face.FaceType, static_cast<uint8>(0x1A));
		TestEqual(TEXT("FIREPTS uses light/effect markers"), Face.LightType, static_cast<uint16>(1));
		TestEqual(TEXT("FIREPTS marker has one vertex"), Face.VertexCount, static_cast<uint16>(1));
		SmokeMarkers += Face.MaterialIndex == 1 ? 1 : 0;
		FireMarkers += Face.MaterialIndex == 2 ? 1 : 0;
	}
	TestEqual(TEXT("FIREPTS smoke marker class count"), SmokeMarkers, 11);
	TestEqual(TEXT("FIREPTS fire marker class count"), FireMarkers, 11);

	static constexpr int32 EffectObjectIds[] = {
		0x7c, 0xae, 0x147, 0x148, 0x149, 0x14a, 0x14b
	};
	for (const int32 ObjectId : EffectObjectIds)
	{
		const FMaxisMeshObject* EffectObject = MeshLibrary.FindObjectByObjectId(ObjectId);
		if (TestNotNull(
			FString::Printf(TEXT("finds original effect GEO 0x%x"), ObjectId),
			EffectObject))
		{
			TestTrue(
				FString::Printf(TEXT("effect GEO 0x%x has authored faces"), ObjectId),
				EffectObject->Faces.Num() > 0);
		}
	}

	const TArray<FColor>* SharedPalette = MeshLibrary.GetSharedColorMap();
	if (TestNotNull(TEXT("effect palette is available"), SharedPalette))
	{
		TestEqual(TEXT("fire selector palette 0x13"), (*SharedPalette)[0x13], FColor(159, 47, 5));
		TestEqual(TEXT("smoke selector palette 0x3a"), (*SharedPalette)[0x3a], FColor(165, 165, 165));
		TestEqual(TEXT("water selector palette 0x94"), (*SharedPalette)[0x94], FColor(0, 79, 149));
	}

	const FMaxisMeshObject* FireTruck = MeshLibrary.FindObjectByTableName(TEXT("CARFIRET"));
	if (TestNotNull(TEXT("finds CARFIRET"), FireTruck))
	{
		TestEqual(TEXT("CARFIRET is the fire-truck model"),
			FireTruck->Header.ObjectName.ToLower(), FString(TEXT("firetruk")));
	}
	return true;
}
