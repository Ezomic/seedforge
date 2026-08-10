// Stage 0 grey-box combat. Disposable by design - see docs/18-vertical-slice.md.

#include "IchorCombatComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "DrawDebugHelpers.h"
#include "Ichor.h"

UIchorCombatComponent::UIchorCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UIchorCombatComponent::BeginPlay()
{
	Super::BeginPlay();
	Health = MaxHealth;
	Stamina = MaxStamina;
	Poise = MaxPoise;
}

float UIchorCombatComponent::GetAllowedTurnRate() const
{
	switch (State)
	{
	case EIchorCombatState::Windup:    return TrackingCapWindupDegPerSec;
	case EIchorCombatState::Active:    return TrackingCapActiveDegPerSec;
	case EIchorCombatState::Recovery:  return 0.f;
	case EIchorCombatState::Staggered: return 0.f;
	case EIchorCombatState::Dead:      return 0.f;
	default:                           return 720.f;
	}
}

bool UIchorCombatComponent::TryAttack(EIchorAttack Attack)
{
	// Committed states refuse new input. No cancels, no buffering - that is the point.
	if (State != EIchorCombatState::Idle) { return false; }

	const float Cost = (Attack == EIchorAttack::Light) ? LightStaminaCost : HeavyStaminaCost;
	if (Stamina < Cost) { return false; }

	Stamina -= Cost;
	TimeSinceStaminaSpend = 0.f;
	PendingAttack = Attack;
	bTraceDoneThisSwing = false;
	bBlocking = false;
	EnterState(EIchorCombatState::Windup);
	return true;
}

bool UIchorCombatComponent::TryDodge(FVector Direction)
{
	if (State != EIchorCombatState::Idle) { return false; }
	if (Stamina < DodgeStaminaCost) { return false; }

	Stamina -= DodgeStaminaCost;
	TimeSinceStaminaSpend = 0.f;
	bBlocking = false;

	// A movement burst, not a phase-through. No invincibility is granted anywhere.
	if (ACharacter* Ch = Cast<ACharacter>(GetOwner()))
	{
		FVector Dir = Direction.GetSafeNormal();
		if (Dir.IsNearlyZero()) { Dir = -Ch->GetActorForwardVector(); }
		Ch->LaunchCharacter(Dir * DodgeImpulse, true, false);
	}

	EnterState(EIchorCombatState::Dodging);
	return true;
}

void UIchorCombatComponent::SetBlocking(bool bWantsToBlock)
{
	bBlocking = bWantsToBlock;
}

float UIchorCombatComponent::ReceiveHit(float Damage, float PoiseDamage, AActor* FromActor, bool bAttackerWasOnScreen)
{
	if (State == EIchorCombatState::Dead) { return 0.f; }

	const bool bBlocked = IsBlocking();
	float Applied = Damage;

	if (bBlocked)
	{
		// Block converts damage into stamina drain rather than negating it.
		const float StaminaHit = Damage * (1.f - BlockStaminaRatio) * 2.f;
		Stamina = FMath::Max(0.f, Stamina - StaminaHit);
		TimeSinceStaminaSpend = 0.f;
		Applied = Damage * BlockStaminaRatio;

		// Guard broken: no stamina left to absorb with.
		if (Stamina <= 0.f)
		{
			Poise = 0.f;
			EnterState(EIchorCombatState::Staggered);
		}
	}

	Health = FMath::Max(0.f, Health - Applied);
	Poise = FMath::Max(0.f, Poise - PoiseDamage);

	OnDamaged.Broadcast(Applied, FromActor, bBlocked);

	if (Health <= 0.f)
	{
		// Stamp the cause now, at the moment of the killing blow, rather than
		// reconstructing it afterwards. The distribution is the diagnosis.
		if (!bAttackerWasOnScreen)                      { PendingDeathCause = EIchorDeathCause::OffScreen; }
		else if (State == EIchorCombatState::Recovery)  { PendingDeathCause = EIchorDeathCause::WhiffPunished; }
		else if (bBlocked)                              { PendingDeathCause = EIchorDeathCause::MistimedBlock; }
		else if (Stamina <= 1.f)                        { PendingDeathCause = EIchorDeathCause::OutOfStamina; }
		else                                            { PendingDeathCause = EIchorDeathCause::Unreadable; }

		EnterState(EIchorCombatState::Dead);
		OnDied.Broadcast(PendingDeathCause);
		return Applied;
	}

	if (Poise <= 0.f && State != EIchorCombatState::Staggered)
	{
		EnterState(EIchorCombatState::Staggered);
	}

	return Applied;
}

void UIchorCombatComponent::EnterState(EIchorCombatState NewState)
{
	State = NewState;
	StateTimer = 0.f;

	if (NewState == EIchorCombatState::Staggered)
	{
		Poise = 0.f;
	}
}

void UIchorCombatComponent::PerformAttackTrace()
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World) { return; }

	const FVector Start = Owner->GetActorLocation();
	const FVector End = Start + Owner->GetActorForwardVector() * AttackRange;

	TArray<FHitResult> Hits;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Owner);

	World->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeSphere(AttackRadius), Params);

	const float Damage = (PendingAttack == EIchorAttack::Light) ? LightDamage : HeavyDamage;
	const float PoiseDmg = (PendingAttack == EIchorAttack::Light) ? LightPoiseDamage : HeavyPoiseDamage;

	TSet<AActor*> Struck;
	for (const FHitResult& Hit : Hits)
	{
		AActor* Other = Hit.GetActor();
		if (!Other || Struck.Contains(Other)) { continue; }
		Struck.Add(Other);

		if (UIchorCombatComponent* Target = Other->FindComponentByClass<UIchorCombatComponent>())
		{
			// Whether the attacker was visible is decided by the victim's controller in a
			// fuller build; in the grey box a forward-facing test is close enough to
			// separate "came from off screen" from "was visible and still not read".
			const FVector ToAttacker = (Owner->GetActorLocation() - Other->GetActorLocation()).GetSafeNormal();
			const bool bOnScreen = FVector::DotProduct(Other->GetActorForwardVector(), ToAttacker) > -0.2f;
			Target->ReceiveHit(Damage, PoiseDmg, Owner, bOnScreen);
		}
	}

#if ENABLE_DRAW_DEBUG
	DrawDebugCapsule(World, (Start + End) * 0.5f, AttackRange * 0.5f, AttackRadius,
		FRotationMatrix::MakeFromZ(End - Start).ToQuat(), FColor::Red, false, 0.25f);
#endif
}

void UIchorCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (State == EIchorCombatState::Dead) { return; }

	StateTimer += DeltaTime;
	TimeSinceStaminaSpend += DeltaTime;

	// Stamina only recovers once the owner stops spending it.
	if (TimeSinceStaminaSpend >= StaminaRegenDelay && !IsBlocking())
	{
		Stamina = FMath::Min(MaxStamina, Stamina + StaminaRegenPerSec * DeltaTime);
	}

	if (State == EIchorCombatState::Idle)
	{
		Poise = FMath::Min(MaxPoise, Poise + PoiseRegenPerSec * DeltaTime);
	}

	switch (State)
	{
	case EIchorCombatState::Windup:
	{
		const float Dur = (PendingAttack == EIchorAttack::Light) ? LightWindup : HeavyWindup;
		if (StateTimer >= Dur) { EnterState(EIchorCombatState::Active); }
		break;
	}
	case EIchorCombatState::Active:
	{
		if (!bTraceDoneThisSwing)
		{
			bTraceDoneThisSwing = true;
			PerformAttackTrace();
		}
		const float Dur = (PendingAttack == EIchorAttack::Light) ? LightActive : HeavyActive;
		if (StateTimer >= Dur) { EnterState(EIchorCombatState::Recovery); }
		break;
	}
	case EIchorCombatState::Recovery:
	{
		const float Dur = (PendingAttack == EIchorAttack::Light) ? LightRecovery : HeavyRecovery;
		if (StateTimer >= Dur) { EnterState(EIchorCombatState::Idle); }
		break;
	}
	case EIchorCombatState::Dodging:
		if (StateTimer >= DodgeDuration) { EnterState(EIchorCombatState::Idle); }
		break;

	case EIchorCombatState::Staggered:
		if (StateTimer >= StaggerDuration)
		{
			Poise = MaxPoise * 0.5f;
			EnterState(EIchorCombatState::Idle);
		}
		break;

	default:
		break;
	}
}

FString UIchorCombatComponent::GetDebugLine() const
{
	static const UEnum* StateEnum = StaticEnum<EIchorCombatState>();
	const FString StateName = StateEnum ? StateEnum->GetNameStringByValue((int64)State) : TEXT("?");

	return FString::Printf(TEXT("%-14s HP %5.1f  ST %5.1f  PO %5.1f  t %4.2f %s"),
		*StateName, Health, Stamina, Poise, StateTimer,
		IsBlocking() ? TEXT("[block]") : TEXT(""));
}
