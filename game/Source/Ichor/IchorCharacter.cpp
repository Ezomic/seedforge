// Copyright Epic Games, Inc. All Rights Reserved.

#include "IchorCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "IchorCombatComponent.h"
#include "IchorFightRecorder.h"
#include "Ichor.h"

AIchorCharacter::AIchorCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	Combat = CreateDefaultSubobject<UIchorCombatComponent>(TEXT("Combat"));
}

void AIchorCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Combat) { return; }

	UCharacterMovementComponent* Move = GetCharacterMovement();

	// Commitment is rotational as well as temporal. The clamp is what stops a committed
	// attack from quietly tracking its target, which is the commonest reason this kind of
	// combat reads as cheap.
	Move->RotationRate.Yaw = Combat->GetAllowedTurnRate();

	// ...and positional. You cannot stroll through your own swing.
	float Scale = 1.f;
	if (Combat->IsCommitted())                        { Scale = CommittedMoveScale; }
	else if (Combat->IsBlocking())                    { Scale = BlockingMoveScale; }
	if (Combat->GetState() == EIchorCombatState::Staggered) { Scale = 0.f; }
	Move->MaxWalkSpeed = BaseWalkSpeed * Scale;

	if (bShowCombatDebug && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1001, 0.f, FColor::Green,
			FString::Printf(TEXT("PLAYER  %s"), *Combat->GetDebugLine()));
	}
}

void AIchorCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {

		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AIchorCharacter::Move);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AIchorCharacter::Look);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AIchorCharacter::Look);

		// Combat. Started rather than Triggered - attacks fire once on press and commit.
		if (LightAttackAction) { EnhancedInputComponent->BindAction(LightAttackAction, ETriggerEvent::Started, this, &AIchorCharacter::OnLightAttack); }
		if (HeavyAttackAction) { EnhancedInputComponent->BindAction(HeavyAttackAction, ETriggerEvent::Started, this, &AIchorCharacter::OnHeavyAttack); }
		if (DodgeAction)       { EnhancedInputComponent->BindAction(DodgeAction, ETriggerEvent::Started, this, &AIchorCharacter::OnDodge); }
		if (RestartAction)     { EnhancedInputComponent->BindAction(RestartAction, ETriggerEvent::Started, this, &AIchorCharacter::OnRestart); }
		if (BlockAction)
		{
			EnhancedInputComponent->BindAction(BlockAction, ETriggerEvent::Started, this, &AIchorCharacter::OnBlockStart);
			EnhancedInputComponent->BindAction(BlockAction, ETriggerEvent::Completed, this, &AIchorCharacter::OnBlockEnd);
		}
	}
	else
	{
		UE_LOG(LogIchor, Error, TEXT("'%s' Failed to find an Enhanced Input component!"), *GetNameSafe(this));
	}
}

void AIchorCharacter::OnLightAttack() { if (Combat) { Combat->TryAttack(EIchorAttack::Light); } }
void AIchorCharacter::OnHeavyAttack() { if (Combat) { Combat->TryAttack(EIchorAttack::Heavy); } }
void AIchorCharacter::OnBlockStart()  { if (Combat) { Combat->SetBlocking(true); } }
void AIchorCharacter::OnBlockEnd()    { if (Combat) { Combat->SetBlocking(false); } }

void AIchorCharacter::OnRestart()
{
	if (UIchorFightRecorder* Rec = UIchorFightRecorder::For(this))
	{
		Rec->ResetFight();
	}
}

void AIchorCharacter::OnDodge()
{
	if (!Combat) { return; }
	// Dodge in the direction of travel, or backwards when standing still.
	FVector Dir = LastMoveDirection.IsNearlyZero() ? -GetActorForwardVector() : LastMoveDirection;
	Combat->TryDodge(Dir);
}

void AIchorCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void AIchorCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AIchorCharacter::DoMove(float Right, float Forward)
{
	if (GetController() != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// remember intent so a dodge has a direction to use
		LastMoveDirection = (ForwardDirection * Forward + RightDirection * Right).GetSafeNormal();

		// add movement
		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void AIchorCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AIchorCharacter::DoJumpStart()
{
	// signal the character to jump
	Jump();
}

void AIchorCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}
