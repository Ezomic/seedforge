// Stage 0 grey-box enemy. Disposable by design - see docs/18-vertical-slice.md.

#include "IchorSkirmisher.h"
#include "IchorCombatComponent.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "Ichor.h"

AIchorSkirmisher::AIchorSkirmisher()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.f);

	// Rotation is driven manually so the tracking clamp is exact rather than approximate.
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;

	// AddMovementInput needs a controller present.
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = AAIController::StaticClass();

	Combat = CreateDefaultSubobject<UIchorCombatComponent>(TEXT("Combat"));

	// The opponent wears the dark crest. Set here rather than in BeginPlay because the
	// combat component builds its kit during its own BeginPlay, which runs first.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DarkCrest(TEXT("/Game/Ichor/Materials/MI_HelmetCrestDark"));
	if (DarkCrest.Succeeded() && Combat) { Combat->CrestMaterial = DarkCrest.Object; }

	static ConstructorHelpers::FObjectFinder<USoundBase> TellAsset(TEXT("/Game/Ichor/Audio/A_Tell"));
	if (TellAsset.Succeeded()) { TellCue = TellAsset.Object; }
}

void AIchorSkirmisher::BeginPlay()
{
	Super::BeginPlay();

	if (Combat)
	{
		Combat->TrackingCapWindupDegPerSec = TrackingCapDegPerSec;
		Combat->LightWindup *= WindupScale;
		Combat->HeavyWindup *= WindupScale;

		// Reach decides when a swing can land, so reach decides when one may start. Derived
		// rather than typed twice: the two drifting apart is exactly how it ended up
		// committing from outside its own range.
		StrikeRange = Combat->AttackRange + Combat->AttackRadius + 40.f;
	}

	Target = UGameplayStatics::GetPlayerPawn(this, 0);
}

void AIchorSkirmisher::EnterState(EIchorSkirmisherState NewState)
{
	// Logged as well as drawn: the on-screen readout is for playing, this is for
	// verifying the loop headlessly and for the fight records the gate depends on.
	static const UEnum* E = StaticEnum<EIchorSkirmisherState>();
	UE_LOG(LogIchor, Log, TEXT("[skirm] %s -> %s  (desire %.2f, commits %d)"),
		E ? *E->GetNameStringByValue((int64)State) : TEXT("?"),
		E ? *E->GetNameStringByValue((int64)NewState) : TEXT("?"),
		Desire, CommitsSinceRest);

	State = NewState;
	StateTimer = 0.f;
}

void AIchorSkirmisher::FaceTarget(float DeltaSeconds)
{
	if (!Target || !Combat) { return; }

	const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
	const float DesiredYaw = ToTarget.Rotation().Yaw;
	const float CurrentYaw = GetActorRotation().Yaw;

	// The clamp that replaces i-frames. During active frames this is zero, so a correct
	// dodge cannot be tracked through.
	const float MaxStep = Combat->GetAllowedTurnRate() * DeltaSeconds;
	const float Delta = FMath::FindDeltaAngleDegrees(CurrentYaw, DesiredYaw);
	const float Step = FMath::Clamp(Delta, -MaxStep, MaxStep);

	SetActorRotation(FRotator(0.f, CurrentYaw + Step, 0.f));
}

void AIchorSkirmisher::MoveToward(const FVector& WorldDir, float Scale)
{
	if (WorldDir.IsNearlyZero()) { return; }
	AddMovementInput(WorldDir.GetSafeNormal(), Scale);
}

void AIchorSkirmisher::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!Combat || !Combat->IsAlive()) { return; }

	if (!Target)
	{
		Target = UGameplayStatics::GetPlayerPawn(this, 0);
		if (!Target) { return; }
	}

	// Stagger belongs to the combat component; the decision layer simply defers.
	if (Combat->GetState() == EIchorCombatState::Staggered)
	{
		Desire = 0.f;
		return;
	}

	StateTimer += DeltaSeconds;

	const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
	const float Distance = ToTarget.Size2D();
	const FVector Forward = ToTarget.GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);

	UIchorCombatComponent* TargetCombat = Target->FindComponentByClass<UIchorCombatComponent>();
	const bool bTargetRecovering = TargetCombat && TargetCombat->GetState() == EIchorCombatState::Recovery;

	// A dead target is not an opponent. Without this it goes on attacking the body, which
	// looks broken and, less obviously, opens a new fight record on every connect.
	if (TargetCombat && !TargetCombat->IsAlive())
	{
		if (State != EIchorSkirmisherState::Unaware)
		{
			EnterState(EIchorSkirmisherState::Unaware);
		}
		Desire = 0.f;
		CommitsSinceRest = 0;
		PressureTimer = 0.f;
		return;
	}

	FaceTarget(DeltaSeconds);
	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed * (Combat->IsCommitted() ? 0.15f : 1.f);

	switch (State)
	{
	case EIchorSkirmisherState::Unaware:
		if (Distance <= AggroRange) { EnterState(EIchorSkirmisherState::Approach); }
		break;

	case EIchorSkirmisherState::Approach:
		MoveToward(Forward, 1.f);
		if (Distance <= PreferredSpacing + ReCommitSpacingTolerance) { EnterState(EIchorSkirmisherState::Neutral); }
		break;

	case EIchorSkirmisherState::Neutral:
	{
		// Hold the band and circle. Desire builds here rather than a timer firing, so
		// pressure is something the player can feel without being able to count it.
		const float Error = Distance - PreferredSpacing;
		if (FMath::Abs(Error) > 40.f) { MoveToward(Forward * FMath::Sign(Error), 0.8f); }
		MoveToward(Right * StrafeSign, StrafeSpeedScale);
		if (FMath::Fmod(StateTimer, 2.2f) < DeltaSeconds) { StrafeSign = -StrafeSign; }

		PressureTimer += DeltaSeconds;
		Desire += (bTargetRecovering ? DesireGainVsRecovering : DesireGainRate) * DeltaSeconds;

		// A whiff is the opening the player's commitment creates. Punishing it is what
		// makes their commitment mean something.
		if (bTargetRecovering && Distance <= PreferredSpacing + ReCommitSpacingTolerance)
		{
			EnterState(EIchorSkirmisherState::Punish);
			break;
		}

		if (CommitsSinceRest >= RestAfterCommits || PressureTimer >= RestAfterSeconds)
		{
			EnterState(EIchorSkirmisherState::Rest);
			break;
		}

		if (Desire >= DesireThreshold)
		{
			// Ready to swing, but only from somewhere the swing can land. Out of reach it
			// closes instead of committing - a whiff should be something the player earned
			// by moving, not something the enemy did to itself.
			if (Distance <= StrikeRange)
				{
					// Sounded on entry, so it lands a full TellDuration before the wind-up.
					if (TellCue)
					{
						UGameplayStatics::PlaySoundAtLocation(this, TellCue, GetActorLocation());
					}
					EnterState(EIchorSkirmisherState::Tell);
				}
			else                         { MoveToward(Forward, 1.f); }
		}
		break;
	}

	case EIchorSkirmisherState::Tell:
		// A weight shift before the wind-up proper. Cheapest fairness upgrade available:
		// large readability gain without lengthening the attack itself.
		if (StateTimer >= TellDuration)
		{
			Combat->TryAttack(FMath::FRand() < 0.3f ? EIchorAttack::Heavy : EIchorAttack::Light);
			EnterState(EIchorSkirmisherState::Committing);
		}
		break;

	case EIchorSkirmisherState::Punish:
		// Step into the opening during the reaction beat. A punish that starts out of reach
		// is not a punish, it is a gift.
		if (Distance > StrikeRange * 0.8f) { MoveToward(Forward, 1.f); }
		if (StateTimer >= PunishReactionTime)
		{
			Combat->TryAttack(EIchorAttack::Light);
			EnterState(EIchorSkirmisherState::Committing);
		}
		break;

	case EIchorSkirmisherState::Committing:
		// Wait out the swing. No cancelling, no repositioning mid-attack.
		if (!Combat->IsCommitted())
		{
			Desire = 0.f;
			CommitsSinceRest++;
			EnterState(EIchorSkirmisherState::Neutral);
		}
		break;

	case EIchorSkirmisherState::Rest:
		// Deliberate disengagement: where the player heals, repositions and thinks.
		if (Distance < PreferredSpacing * 1.6f) { MoveToward(-Forward, 0.7f); }
		if (StateTimer >= RestDuration)
		{
			CommitsSinceRest = 0;
			PressureTimer = 0.f;
			Desire = 0.f;
			EnterState(EIchorSkirmisherState::Neutral);
		}
		break;
	}

	if (bShowDebug && GEngine)
	{
		static const UEnum* E = StaticEnum<EIchorSkirmisherState>();
		const FString Name = E ? E->GetNameStringByValue((int64)State) : TEXT("?");
		GEngine->AddOnScreenDebugMessage(1002, 0.f, FColor::Yellow,
			FString::Printf(TEXT("SKIRM   %-11s desire %4.2f  dist %6.1f (strike %5.1f)  commits %d   | %s"),
				*Name, Desire, Distance, StrikeRange, CommitsSinceRest, *Combat->GetDebugLine()));

#if ENABLE_DRAW_DEBUG
		// Two rings, and the difference between them is the whole spacing game: cyan is
		// where it wants to stand, red is where it can actually hurt you.
		DrawDebugCircle(GetWorld(), GetActorLocation(), PreferredSpacing, 32, FColor::Cyan,
			false, -1.f, 0, 2.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
		DrawDebugCircle(GetWorld(), GetActorLocation(), StrikeRange, 32, FColor(160, 40, 40),
			false, -1.f, 0, 1.5f, FVector(1, 0, 0), FVector(0, 1, 0), false);
#endif
	}
}
