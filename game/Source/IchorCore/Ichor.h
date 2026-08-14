// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 *  Main log category, used across every module.
 *
 *  Exported with the module API macro because it is now referenced from outside the module
 *  that defines it - the same pattern the engine uses for LogEngine. The file keeps its
 *  original name so the include lines in the gameplay code did not all have to change.
 */
ICHORCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogIchor, Log, All);
