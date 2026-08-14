// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "IchorCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UIchorCombatComponent;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  Third person player character for the Stage 0 grey-box prototype.
 *
 *  Combat lives in UIchorCombatComponent, shared with the skirmisher. This class only
 *  translates input into intent and enforces the movement side of commitment.
 */
UCLASS(abstract)
class AIchorCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;

	/** Health, stamina, poise and the committed attack cycle */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UIchorCombatComponent* Combat;

protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

	/** Light attack */
	UPROPERTY(EditAnywhere, Category="Input|Combat")
	UInputAction* LightAttackAction;

	/** Heavy attack */
	UPROPERTY(EditAnywhere, Category="Input|Combat")
	UInputAction* HeavyAttackAction;

	/** Hold to block */
	UPROPERTY(EditAnywhere, Category="Input|Combat")
	UInputAction* BlockAction;

	/** Directional dodge-step. No invincibility frames - see the combat component. */
	UPROPERTY(EditAnywhere, Category="Input|Combat")
	UInputAction* DodgeAction;

	/**
	 *  Retry. Doc 18's gate asks whether a player dies and immediately tries again, and
	 *  measures the gap - so trying again has to cost one key. Measured against a level
	 *  reload the number would record how long the editor takes to boot.
	 */
	UPROPERTY(EditAnywhere, Category="Input|Combat")
	UInputAction* RestartAction;

	/** Movement speed multiplier while committed to an attack. Commitment is not only rotational. */
	UPROPERTY(EditAnywhere, Category="Tuning|Movement")
	float CommittedMoveScale = 0.25f;

	UPROPERTY(EditAnywhere, Category="Tuning|Movement")
	float BlockingMoveScale = 0.45f;

	/** Always-on state readout. You will read this more than the code. */
	UPROPERTY(EditAnywhere, Category="Debug")
	bool bShowCombatDebug = true;

	/**
	 *  The controls, on screen.
	 *
	 *  Doc 18's gate is "thirty minutes each, no help, watched". Two strangers cannot
	 *  discover that Shift converts damage into stamina, or that Space repositions rather
	 *  than granting invincibility, by pressing keys at random - and a tester who never
	 *  finds half the defensive design fails the gate for a reason that has nothing to do
	 *  with whether the fight is tense.
	 */
	UPROPERTY(EditAnywhere, Category="Debug")
	bool bShowControls = true;

public:

	/** Constructor */
	AIchorCharacter();

	virtual void Tick(float DeltaSeconds) override;

	UIchorCombatComponent* GetCombat() const { return Combat; }

protected:

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

	void OnLightAttack();
	void OnHeavyAttack();
	void OnBlockStart();
	void OnBlockEnd();
	void OnDodge();
	void OnRestart();

public:

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles look inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

public:

	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }

private:

	/** Last world-space movement intent, used to pick a dodge direction. */
	FVector LastMoveDirection = FVector::ZeroVector;

	float BaseWalkSpeed = 500.f;
};
