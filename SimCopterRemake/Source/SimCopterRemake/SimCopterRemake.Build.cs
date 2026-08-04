// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SimCopterRemake : ModuleRules
{
	public SimCopterRemake(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Several format readers use same-named helpers in anonymous namespaces; unity builds
		// merge multiple .cpp files into one TU, so chunk reshuffles caused collisions whenever
		// a file was added. Per-file compilation keeps those helpers properly private.
		bUseUnity = false;

		// MeshDescription/StaticMeshDescription back UStaticMesh::BuildFromMeshDescriptions, which the
		// city build uses to turn each distinct GEO building model into a runtime static mesh so
		// buildings can be placed as removable instances instead of baked into one merged mesh.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "ProceduralMeshComponent", "MeshDescription", "StaticMeshDescription" });

		// DaySequence and CelestialVault back the level's day/night cycle. The CityRender actor is an
		// ACelestialVaultDaySequenceActor: USimCopterDayNightFogComponent reads
		// GetTimeOfDay()/GetDayLength() off its DaySequence base to drive the height fog, and
		// USimCopterMoonDiscComponent reaches its MoonDiscComponent property to override the moon
		// disc's material brightness. Both plugins are already enabled in the .uproject (DaySequence
		// via CelestialVault's own dependency), so this only adds the link, not a new plugin.
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "UMG", "RenderCore", "MediaAssets", "DaySequence", "CelestialVault" });

		// The Graphics page replaces the original's render.bmp options with Unreal's, so it drives
		// NVIDIA's two blueprint libraries directly rather than poking console variables. Both
		// plugins are already required by the .uproject; they publish WITH_DLSS / WITH_STREAMLINE
		// as 0 off Windows, so the call sites stay guarded and this stays Windows-only.
		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "DLSSBlueprint", "StreamlineDLSSGBlueprint" });
		}

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
