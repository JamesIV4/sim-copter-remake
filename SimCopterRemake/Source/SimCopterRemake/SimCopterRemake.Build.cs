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

		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "UMG" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
