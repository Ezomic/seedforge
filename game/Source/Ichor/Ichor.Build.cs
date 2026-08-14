// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Ichor : ModuleRules
{
	public Ichor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Deliberately lean. StateTree and GameplayStateTree came with the
		// template's Combat variant; doc 18 specifies the skirmisher as a plain
		// C++ enum and switch, so they are dropped. UMG is dropped too - the
		// Stage 0 debug overlay uses AddOnScreenDebugMessage and DrawDebug*.
		// Both are one line to re-add if that changes.
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Ichor"
		});
	}
}
