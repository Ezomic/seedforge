// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class IchorCore : ModuleRules
{
	public IchorCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Deliberately tiny. Anything every other module needs and nothing owns:
		// right now that is one log category. It has no dependency on Engine so it
		// cannot start accumulating gameplay by accident.
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject"
		});

		PublicIncludePaths.AddRange(new string[] { "IchorCore" });
	}
}
