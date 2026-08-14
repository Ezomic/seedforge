// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class IchorWorldRuntime : ModuleRules
{
	public IchorWorldRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Runtime side of the world generator: consuming the phase 12 bake, streaming the
// baked world, and the consequence ledger. Empty until libworldforge produces
// something to consume - the module exists now because doc 17 is right that adding
// boundaries later is miserable, and an empty module costs a build second.
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"IchorCore"
		});

		PublicIncludePaths.AddRange(new string[] { "IchorWorldRuntime" });
	}
}
