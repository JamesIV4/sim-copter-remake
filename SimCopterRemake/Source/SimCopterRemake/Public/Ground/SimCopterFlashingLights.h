// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "SimCopterFlashingLights.generated.h"

struct FMaxisMeshObject;
class UProceduralMeshComponent;
class UMaterialInterface;
class UPointLightComponent;

// One authored light marker: a Maxis face-type-25 record, which always has exactly one vertex and
// carries its VGA palette colour in the face's material byte.
struct FSimCopterFlashingLightPoint
{
	// Component-space offset, already converted to centimetres by the caller's mesh conventions.
	FVector LocalOffset = FVector::ZeroVector;
	// Raw SIM3D palette index. This is the blink key, not just a colour - see the schedule below.
	uint8 PaletteIndex = 0;
	FLinearColor Color = FLinearColor::White;
};

// SCHOOK: flashing light rasteriser 0x00496c00
//
// The original's blinking lights are pure geometry plus one global counter. Every GEO object that
// blinks carries face-type-25 records - one vertex each, material byte = palette index - and
// FUN_00496c00 is the only thing that draws them. Its whole body is:
//
//     phase = DAT_005039c8 & 7;
//     switch (light->colour) {
//       case 0xf6: if (phase != 0) return; break;   // white  #FFFAF0
//       case 0xf9: if (phase != 1) return; break;   // red    #FF0000
//       case 0xfa: if (phase != 2) return; break;   // green  #00FF00
//       case 0xfb: if (phase != 3) return; break;   // yellow #FFFF00
//       case 0xfc: if (phase != 4) return; break;   // blue   #0000FF
//       default:   if (phase != 5) return; break;
//     }
//     ... plot a 4x4 block of that palette byte at (x >> 12, y >> 12) ...
//
// So the lights do not blink independently: the whole city round-robins by COLOUR through an
// 8-step cycle, and each colour is lit on exactly one of the eight steps. Phases 6 and 7 light
// nothing at all, which is the dark gap between cycles. DAT_005039c8 is bumped once per frame by
// FUN_0047a760 (the main tick, called from FUN_00449850).
//
// Shipped data only ever uses those five palette entries on a type-25 face (verified over all
// three GEO packs: 347 markers, material bytes {246, 249, 250, 251, 252}, all single-vertex), so
// the `default` arm is dead for the retail packs - it is kept because the original has it.
class SIMCOPTERREMAKE_API FSimCopterFlashingLightSchedule
{
public:
	// Maxis face type that marks a blinking light. Face type 26 is the neighbouring effect-marker
	// type (FIREPTS smoke/fire, LP213 lamp posts) whose material byte is an effect class instead
	// of a palette index - do not confuse the two.
	static constexpr uint8 LightFaceType = 25;

	static constexpr int32 PhaseCount = 8;

	// The palette entries FUN_00496c00 names explicitly, in phase order.
	static constexpr uint8 WhitePaletteIndex = 0xf6;
	static constexpr uint8 RedPaletteIndex = 0xf9;
	static constexpr uint8 GreenPaletteIndex = 0xfa;
	static constexpr uint8 YellowPaletteIndex = 0xfb;
	static constexpr uint8 BluePaletteIndex = 0xfc;

	// The `default:` arm - every colour the switch does not name shares this one phase.
	static constexpr int32 UnlistedColorPhase = 5;

	// The 0x10 (shipped, high-resolution) branch writes a 4x4 block of framebuffer pixels; the
	// alternate mode writes 2x2. FUN_00479bb0 stores 0x10, so 4 is the retail footprint.
	static constexpr float LightSizeViewportPixels = 4.0f;

	// --- DELIBERATE DIVERGENCE: the original does not scale its lights ---
	//
	// FUN_00496c00 stamps that 4x4 block straight into the framebuffer at the projected point, so
	// a light covers four pixels no matter how far away it is. In world terms that means it grows
	// without bound with distance - which was invisible on a 560x400 software renderer but reads
	// badly here: a beacon on a tower across the city ends up metres across, blooms over the
	// building it is attached to, and pokes through whatever is in front of it.
	//
	// So the port gives the card a fixed WORLD size and lets perspective shrink it like any other
	// geometry. The size is still derived from the original rather than invented: it is what the
	// 4-pixel block covered at LightSizeReferenceDepthCm through the original's own projection.
	static constexpr float LightSizeReferenceDepthCm = 600.0f;

	// ...with a hard floor on the ACTUAL output footprint, so a marker never rasterizes below one
	// physical screen pixel however far away it is. This is intentionally not an original 560x400
	// pixel: on a modern viewport that old unit can be several output pixels wide.
	static constexpr float MinLightSizeViewportPixels = 1.0f;
	static float GetWorldSizeForScreenPixels(
		float CameraDepthCm,
		float ViewportWidthPixels,
		float HorizontalFovDegrees,
		float ScreenPixels);

	// Full width of a light card in centimetres, at the reference depth.
	static float GetLightWorldSizeCm();

	// DAT_005039c8 advances once per rendered frame, so the original's blink rate rode the frame
	// rate outright. Pin it to the same nominal 0.05 s tick the rest of the port uses for
	// per-frame rules, or the lights strobe six times too fast at a modern frame rate.
	static constexpr double PhaseSeconds = 0.05;

	// Which of the eight steps this colour is lit on.
	static int32 GetPhaseForPaletteIndex(uint8 PaletteIndex);

	// DAT_005039c8 & 7, expressed against elapsed game time.
	static int32 GetPhaseAtTime(double GameTimeSeconds);

	static bool IsLitAtPhase(uint8 PaletteIndex, int32 Phase)
	{
		return GetPhaseForPaletteIndex(PaletteIndex) == Phase;
	}

	static bool IsLitAtTime(uint8 PaletteIndex, double GameTimeSeconds)
	{
		return IsLitAtPhase(PaletteIndex, GetPhaseAtTime(GameTimeSeconds));
	}

	// Pulls every face-type-25 marker out of a GEO object. Positions are converted with the same
	// units/scale the caller used for the object's solid geometry so the lights land on the model.
	// Set bApplyCityMeshOrientation for objects placed by the city builder, which folds in the
	// global 180-degree yaw; helicopter and vehicle meshes do not.
	static int32 ExtractLightPoints(
		const FMaxisMeshObject& Object,
		const TArray<FColor>* Palette,
		float ModelUnitsPerCentimeter,
		float ModelScale,
		bool bApplyCityMeshOrientation,
		TArray<FSimCopterFlashingLightPoint>& OutPoints);
};

// Draws a set of face-type-25 markers as camera-facing cards, showing only the colour whose phase
// is current. One of these can serve a single model (a helicopter) or a whole city's worth of
// building lights - the points are stored in component space, so the owner's transform moves them.
UCLASS(ClassGroup = (SimCopter), meta = (BlueprintSpawnableComponent))
class SIMCOPTERREMAKE_API USimCopterFlashingLightsComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	USimCopterFlashingLightsComponent();

	virtual void BeginPlay() override;

	void SetLightPoints(TArray<FSimCopterFlashingLightPoint> InPoints);
	void AppendLightPoints(const TArray<FSimCopterFlashingLightPoint>& InPoints);
	void ClearLightPoints();

	int32 GetLightPointCount() const { return LightPoints.Num(); }
	bool HasLightPoints() const { return LightPoints.Num() > 0; }

	// Rebuilds the visible cards. Cheap to call every frame: it early-outs unless the phase
	// changed or the camera moved, because only one colour is ever on screen at a time.
	void SyncLights(double GameTimeSeconds, const FVector& CameraLocation, const FRotator& CameraRotation);

	// Convenience wrapper that reads the local player's camera itself.
	void SyncLightsFromPlayerCamera(double GameTimeSeconds);

	// Multiplies the card's world size. 1 is the size derived from the original's 4-pixel block.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Lights", meta = (ClampMin = "0.05"))
	float CardSizeScale = 1.0f;

	// Each lit marker also casts a real point light of its own colour. The original had no dynamic
	// lighting at all - this is purely the remake making the beacons throw light on the geometry
	// they are bolted to.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Lights")
	bool bCastPointLights = true;

	// 0 - the default - means EVERY lit marker gets its own light, with no cap and no distance
	// cull. This project runs with MegaLights, which solves local lights by stochastic sampling at
	// a roughly fixed cost, so the usual "a renderer cannot take hundreds of dynamic lights"
	// budget does not apply and there is no reason to ration them.
	//
	// A positive value re-imposes a pool and hands it to the markers nearest the camera; it is
	// only there for a configuration running without MegaLights.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Lights", meta = (ClampMin = "0"))
	int32 MaxPointLights = 0;

	// On: MegaLights solves shadowed local lights by stochastic sampling, so the cost is there to
	// be spent, and it is what stops a beacon on the far face of a tower lighting up the near one
	// through the geometry. Clear it if the sampling noise on a marker this small ever shows.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Lights")
	bool bPointLightsCastShadows = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Lights", meta = (ClampMin = "0.0"))
	float PointLightIntensity = 12.0f;

	// Multiplies PointLightIntensity. The helicopter's markers and the city's are tuned to
	// different bases (a position light on a small airframe against a rooftop beacon), so the
	// debug slider drives this rather than the absolute value - one knob, both keep their balance.
	//
	// 0.02 is a tuned value, not a placeholder: with MegaLights solving every marker, the unitless
	// 6 and 20 bases are blinding, and this is where they were dialled to on 2026-07-30. Loaded
	// from GameUserSettings on BeginPlay, so the helicopter's and the city's components pick up
	// the same persisted number without either having to know about the other.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Lights", meta = (ClampMin = "0.0"))
	float PointLightIntensityScale = 0.02f;

	static const TCHAR* GetConfigSection() { return TEXT("SimCopter.FlashingLights"); }
	static const TCHAR* GetIntensityScaleConfigKey() { return TEXT("IntensityScale"); }

	// Writes the scale to GameUserSettings. Static because the pawn's setter drives every
	// component in the world and only wants to touch the file once.
	static void SaveIntensityScaleToConfig(float Scale);

	float GetEffectivePointLightIntensity() const
	{
		return FMath::Max(PointLightIntensity * PointLightIntensityScale, 0.0f);
	}

	UPROPERTY(EditAnywhere, Category = "SimCopter|Lights", meta = (ClampMin = "1.0"))
	float PointLightAttenuationRadiusCm = 1200.0f;

private:
	// One lit marker resolved to world space for this rebuild.
	struct FLitLight
	{
		FVector World = FVector::ZeroVector;
		FLinearColor Color = FLinearColor::White;
		float CameraDepthCm = 0.0f;
		float CameraDistanceSquared = 0.0f;
	};

	void RebuildCards(int32 Phase, const FVector& CameraLocation, const FRotator& CameraRotation);
	void UpdatePointLights(const TArray<FLitLight>& LitLights);
	void ReleasePointLights(int32 FirstUnusedIndex);

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> MeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> CardMaterial;

	// Instance of CardMaterial carrying the scene's current effect emissive value, because the card
	// material is unlit - see USimCopterEffectExposureSubsystem.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CardMaterialInstance;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> PointLights;

	TArray<FSimCopterFlashingLightPoint> LightPoints;

	int32 LastPhase = INDEX_NONE;
	FVector LastCameraLocation = FVector::ZeroVector;
	FRotator LastCameraRotation = FRotator::ZeroRotator;
	// The owner can move without the camera moving (a parked helicopter seen from a fixed camera
	// is the obvious case), and the cards are built in component space, so this is tracked too.
	FTransform LastComponentTransform = FTransform::Identity;
	float ActiveViewportWidthPixels = 560.0f;
	float ActiveHorizontalFovDegrees = 60.0f;
	bool bHasDrawnOnce = false;
};
