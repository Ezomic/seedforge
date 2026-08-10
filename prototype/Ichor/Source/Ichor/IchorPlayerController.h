// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "IchorPlayerController.generated.h"

class UInputMappingContext;

/**
 *  PlayerController for the Stage 0 grey-box prototype.
 *  Manages Enhanced Input mapping contexts.
 *
 *  The template's mobile touch-control support was removed: it pulled in UMG
 *  and Slate for a platform this prototype does not target. See
 *  docs/18-vertical-slice.md - Stage 0 is deliberately lean and disposable.
 */
UCLASS(abstract)
class AIchorPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category = "Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

};
