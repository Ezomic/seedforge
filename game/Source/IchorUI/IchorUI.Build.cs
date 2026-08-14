// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class IchorUI : ModuleRules
{
	public IchorUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// HUD and menus. Empty because Stage 0's readout is AddOnScreenDebugMessage and doc 13
// has not been built yet. It takes its UMG and Slate dependencies when it has
// something to draw, which keeps them out of the gameplay module's compile.
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"IchorCore"
		});

		PublicIncludePaths.AddRange(new string[] { "IchorUI" });
	}
}
