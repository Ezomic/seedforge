// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ichor.h"
#include "Modules/ModuleManager.h"

// The primary game module. Everything a player interacts with lives here; IchorCore holds
// only what the other modules share.
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, IchorGameplay, "Ichor");
