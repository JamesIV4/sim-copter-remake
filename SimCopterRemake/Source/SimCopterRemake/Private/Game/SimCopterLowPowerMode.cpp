// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/SimCopterLowPowerMode.h"

#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterLowPower, Log, All);

namespace
{
bool GLowPowerEnabled = false;

/**
 * What each switch was before the mode was first entered, so leaving it puts the renderer back
 * where it was rather than at whatever the engine's compiled-in default happens to be. Captured
 * once, on the first apply: entering the mode twice must not capture the low values as "normal".
 */
TMap<FString, FString> GCapturedValues;

SimCopterLowPower::FOnLowPowerModeChanged GOnChanged;

using SimCopterLowPower::FRenderSwitch;

// Everything the Low scalability profile does NOT do. Anything the profile already handles is
// deliberately absent, so the Settings page's Quality rows stay meaningful for what they own -
// duplicating a value here would pin it at ECVF_SetByGameOverride and quietly kill its dropdown.
const FRenderSwitch GRenderSwitches[] =
{
	// --- upscaling -----------------------------------------------------------------------------

	{
		TEXT("r.AntiAliasingMethod"), TEXT("1"), /*bSkipWhileUpscaling=*/true,
		TEXT("TSR -> FXAA. TSR is a heavy compute upscaler and scalability only tunes its history; ")
		TEXT("on an integrated GPU it is one of the largest single items in the frame.")
	},

	// --- global illumination and reflections ---------------------------------------------------
	//
	// Scalability@0 sets `r.Lumen.DiffuseIndirect.Allow 0`, which stops Lumen LIGHTING but leaves the
	// GI method as Lumen, so the scene still maintains a Lumen scene and its surface cache. Dropping
	// the method itself is what actually stops that work. Reflections go to None rather than
	// screen-space (which is what the Lumen=Off row picks): SSR is cheap on geometry this simple but
	// it is not free, and there is nothing shiny in a SimCopter city.

	{
		TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("0"), false,
		TEXT("No Lumen at all - no surface cache, no card capture, no radiosity. The city's SelfIllum ")
		TEXT("floor and the sky light are what keep shadowed faces readable without it.")
	},
	{
		TEXT("r.ReflectionMethod"), TEXT("0"), false,
		TEXT("No reflection pass. Reflection captures and the sky light still apply.")
	},
	{
		TEXT("r.Lumen.HardwareRayTracing"), TEXT("0"), false,
		TEXT("Belt and braces with the method above, and it is what the Lumen row would have set.")
	},
	{
		TEXT("r.RayTracing.ForceAllRayTracingEffects"), TEXT("0"), false,
		TEXT("The project ships r.RayTracing=True; this stops every RT effect asking for the scene.")
	},

	// --- local lights --------------------------------------------------------------------------
	//
	// MegaLights is enabled by the level's global post process volume (bOverride_bMegaLights), so
	// `r.MegaLights.EnableForProject` cannot switch it off from here - the volume writes over it
	// every frame. `r.MegaLights.Allowed` is the scalability gate that sits above both.

	{
		TEXT("r.MegaLights.Allowed"), TEXT("0"), false,
		TEXT("Back to the standard deferred light loop. Affordable because the mode also stops the ")
		TEXT("beacons and the car headlights spawning lights - see USimCopterFlashingLightsComponent.")
	},

	// --- shadows -------------------------------------------------------------------------------
	//
	// Scalability@0 already turns dynamic shadows off (`r.ShadowQuality 0` clears the DynamicShadows
	// show flag), which is why nothing here bothers with per-component cast flags. The virtual shadow
	// map still gets its own line: it has a fixed setup cost and its physical page pool is a large
	// allocation, and Low only shrinks that pool to 512 pages rather than releasing it.

	{
		TEXT("r.Shadow.Virtual.Enable"), TEXT("0"), false,
		TEXT("No virtual shadow map pool. With r.ShadowQuality 0 from the Low profile the city has no ")
		TEXT("dynamic shadows at all in this mode - deliberately, it is the single biggest saving.")
	},

	// --- sky, cloud and fog --------------------------------------------------------------------

	{
		TEXT("r.VolumetricCloud"), TEXT("0"), false,
		TEXT("The CelestialVaultDaySequenceActor carries a volumetric cloud layer, and scalability has ")
		TEXT("no opinion about it. It is ray-marched per pixel; the sky atmosphere keeps the sky.")
	},
	{
		TEXT("r.VolumetricFog"), TEXT("0"), false,
		TEXT("Also in the Low shadow profile, but listed because the Settings page has its own ")
		TEXT("Volumetric Fog row: forcing it here is what lets that row be greyed out honestly.")
	},

	// --- post process --------------------------------------------------------------------------
	//
	// The Low profile leaves BloomQuality at 4, which is the most expensive thing left in post once
	// motion blur, DOF, AO and the light shafts are off. 1 is the cheapest gaussian sum that still
	// glows - and something has to, because the night windows, the fire and the effect cards are all
	// emissive and read as flat squares with no bloom at all.

	{
		TEXT("r.BloomQuality"), TEXT("1"), false,
		TEXT("One blur pass instead of five. The night skyline and the fire still glow.")
	},
};

/**
 * Puts one switch back where it was.
 *
 * `Unset` is the correct tool - it drops this override out of the CVar's priority history and lets
 * whatever set it before (DefaultEngine.ini, scalability) own it again, so a later scalability
 * change is not silently blocked by a stale ECVF_SetByGameOverride entry. But the history it walks
 * only exists when UE_ALLOW_CVAR_HISTORY is on, so the value is checked afterwards and written
 * explicitly if the unset did nothing. Leaving a low-power value pinned would be the worse failure.
 */
void RestoreSwitch(const FString& Name, const FString& CapturedValue)
{
	IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Name);
	if (Variable == nullptr)
	{
		return;
	}

	Variable->Unset(ECVF_SetByGameOverride);
	if (Variable->GetString() != CapturedValue)
	{
		Variable->Set(*CapturedValue, ECVF_SetByGameOverride);
	}
}
}

TArrayView<const FRenderSwitch> SimCopterLowPower::GetRenderSwitches()
{
	return MakeArrayView(GRenderSwitches);
}

bool SimCopterLowPower::IsEnabled()
{
	return GLowPowerEnabled;
}

SimCopterLowPower::FOnLowPowerModeChanged& SimCopterLowPower::OnChanged()
{
	return GOnChanged;
}

void SimCopterLowPower::Apply(const bool bLowPower, const bool bExternalUpscalerActive)
{
	if (bLowPower)
	{
		for (const FRenderSwitch& Switch : GRenderSwitches)
		{
			const FString Name(Switch.Name);

			if (Switch.bSkipWhileUpscaling && bExternalUpscalerActive)
			{
				// DLSS was turned on while the mode was already running, so this one has to go back
				// even though the mode itself is staying on.
				if (const FString* Captured = GCapturedValues.Find(Name))
				{
					RestoreSwitch(Name, *Captured);
					GCapturedValues.Remove(Name);
				}
				continue;
			}

			IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Switch.Name);
			if (Variable == nullptr)
			{
				// A renamed CVar is a dead optimization, not a crash - but it is worth saying so,
				// because the symptom is silently getting less of the frame back than expected.
				UE_LOG(LogSimCopterLowPower, Warning,
					TEXT("Low power graphics: '%s' does not exist in this engine build; skipped."),
					Switch.Name);
				continue;
			}

			if (!GCapturedValues.Contains(Name))
			{
				GCapturedValues.Add(Name, Variable->GetString());
			}

			// ECVF_SetByGameOverride, for the reason spelled out on SetRenderCVar in
			// SimCopterSettings.cpp: DefaultEngine.ini's renderer settings land at
			// ECVF_SetByProjectSetting, which outranks ECVF_SetByGameSetting despite the names.
			Variable->Set(Switch.LowPowerValue, ECVF_SetByGameOverride);
		}
	}
	else
	{
		for (const TPair<FString, FString>& Captured : GCapturedValues)
		{
			RestoreSwitch(Captured.Key, Captured.Value);
		}
		GCapturedValues.Reset();
	}

	if (GLowPowerEnabled != bLowPower)
	{
		GLowPowerEnabled = bLowPower;
		UE_LOG(LogSimCopterLowPower, Log, TEXT("Low power graphics %s."), bLowPower ? TEXT("ON") : TEXT("OFF"));
		GOnChanged.Broadcast(bLowPower);
	}
}
