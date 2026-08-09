// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
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
		// ApplicationCore for FDisplayMetrics, which seeds the first run's resolution from the
		// monitor the game opened on; RHI for IsRayTracingEnabled, which decides whether the
		// Settings page may offer Hardware Lumen.
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "UMG", "RenderCore", "RHI", "ApplicationCore", "MediaAssets", "DaySequence", "CelestialVault", "Json", "JsonUtilities" });

		// The Graphics page replaces the original's render.bmp options with Unreal's, so it drives
		// NVIDIA's blueprint libraries directly rather than poking console variables. All three
		// plugins are already required by the .uproject; they publish WITH_DLSS / WITH_STREAMLINE
		// as 0 off Windows, so the call sites stay guarded and this stays Windows-only.
		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "DLSSBlueprint", "StreamlineDLSSGBlueprint", "StreamlineReflexBlueprint" });
		}

		// A packaged game reads the original data as loose files beside the executable. UAT cannot
		// stage files directly from the repo-level Reference folder, so game targets first copy the
		// six runtime data trees into the gitignored project Intermediate folder. DefaultGame.ini's
		// [Staging] remap then lifts them to <Package>/SimCopter, which is ../SimCopter from the
		// packaged ProjectDir and therefore the first root SimCopterOriginalGamePaths searches.
		// Editor targets deliberately skip this 237 MB copy; they read Reference directly.
		if (Target.Type == TargetType.Game)
		{
			string OriginalGameRoot = Path.GetFullPath(Path.Combine(
				ModuleDirectory, "..", "..", "..", "Reference", "SimCopterOriginalGame"));
			string StagingRoot = Path.Combine("$(ProjectDir)", "Intermediate", "OriginalGameStaging");
			string[] RequiredDirectories = { "bmp", "cities", "geo", "sound", "tweak", "x" };
			string[] RequiredFiles =
			{
				"bmp/sim3d.bmp",
				"cities/demo.sc2",
				"geo/sim3d1.max",
				"sound/coploop2.wav",
				"tweak/career.twk",
				"x/people.df",
				"x/privanim.df",
			};

			foreach (string DirectoryName in RequiredDirectories)
			{
				string SourceDirectory = Path.Combine(OriginalGameRoot, DirectoryName);
				if (!Directory.Exists(SourceDirectory))
				{
					throw new BuildException(
						"Cannot build a playable SimCopterRemake game target: required original-game " +
						"directory '{0}' is missing. Restore Reference/SimCopterOriginalGame/{1}.",
						SourceDirectory, DirectoryName);
				}

				// The tweak folder also contains the original editor executable. The remake only
				// reads the tables, so keep executable code out of the distributed data payload.
				string FilePattern = DirectoryName == "tweak" ? "*.twk" : "...";
				RuntimeDependencies.Add(
					Path.Combine(StagingRoot, DirectoryName, FilePattern),
					Path.Combine(SourceDirectory, FilePattern),
					StagedFileType.NonUFS);
			}

			foreach (string RelativeFile in RequiredFiles)
			{
				string SourceFile = Path.Combine(OriginalGameRoot, RelativeFile);
				if (!File.Exists(SourceFile))
				{
					throw new BuildException(
						"Cannot build a playable SimCopterRemake game target: required original-game " +
						"file '{0}' is missing. Restore Reference/SimCopterOriginalGame/{1}.",
						SourceFile, RelativeFile);
				}
			}
		}

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
