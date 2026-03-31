// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TheKillingHour : ModuleRules
{
	public TheKillingHour(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"TheKillingHour",
			"TheKillingHour/Variant_Platforming",
			"TheKillingHour/Variant_Platforming/Animation",
			"TheKillingHour/Variant_Combat",
			"TheKillingHour/Variant_Combat/AI",
			"TheKillingHour/Variant_Combat/Animation",
			"TheKillingHour/Variant_Combat/Gameplay",
			"TheKillingHour/Variant_Combat/Interfaces",
			"TheKillingHour/Variant_Combat/UI",
			"TheKillingHour/Variant_SideScrolling",
			"TheKillingHour/Variant_SideScrolling/AI",
			"TheKillingHour/Variant_SideScrolling/Gameplay",
			"TheKillingHour/Variant_SideScrolling/Interfaces",
			"TheKillingHour/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
