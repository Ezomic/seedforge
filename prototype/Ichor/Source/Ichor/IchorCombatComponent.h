// Stage 0 grey-box combat. Disposable by design - see docs/18-vertical-slice.md.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IchorCombatComponent.generated.h"

class UIchorCombatComponent;
class UAnimMontage;

/** Attacks commit. Once Windup begins there is no cancel - that is the design under test. */
UENUM(BlueprintType)
enum class EIchorCombatState : uint8
{
	Idle,
	Windup,
	Active,
	Recovery,
	Dodging,
	Staggered,
	Dead
};

UENUM(BlueprintType)
enum class EIchorAttack : uint8
{
	Light,
	Heavy
};

/**
 *  Why every death is classified rather than counted: the distribution is the diagnosis.
 *  Deaths dominated by Unreadable mean the telegraphs are wrong, which is a different
 *  problem from the design being wrong, and they take opposite fixes.
 */
UENUM(BlueprintType)
enum class EIchorDeathCause : uint8
{
	Unknown,
	OutOfStamina,
	WhiffPunished,
	Cornered,
	MistimedBlock,
	OffScreen,
	Unreadable
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FIchorDamaged, float, Damage, AActor*, Instigator, bool, bWasBlocked);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIchorDied, EIchorDeathCause, Cause);

/**
 *  Shared by the player and the skirmisher. Both need health, stamina, poise and a
 *  committed attack cycle, so they run the same machine with different numbers.
 *
 *  Every tunable is EditAnywhere on purpose: doc 18's protocol changes one variable per
 *  session, and if that needs a recompile it happens less often and teaches less.
 */
UCLASS(ClassGroup = (Ichor), meta = (BlueprintSpawnableComponent))
class UIchorCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UIchorCombatComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ---- tuning: vitals -----------------------------------------------------
	UPROPERTY(EditAnywhere, Category = "Tuning|Vitals") float MaxHealth = 100.f;
	UPROPERTY(EditAnywhere, Category = "Tuning|Vitals") float MaxStamina = 100.f;
	UPROPERTY(EditAnywhere, Category = "Tuning|Vitals") float StaminaRegenPerSec = 25.f;
	UPROPERTY(EditAnywhere, Category = "Tuning|Vitals") float StaminaRegenDelay = 1.f;

	/** Stagger is the currency of melee: winning is about who gets interrupted. */
	UPROPERTY(EditAnywhere, Category = "Tuning|Vitals") float MaxPoise = 100.f;
	UPROPERTY(EditAnywhere, Category = "Tuning|Vitals") float PoiseRegenPerSec = 35.f;

	/**
	 *  Poise used to refill the moment the victim returned to Idle, which meant nothing
	 *  ever accumulated across a combo and stagger was unreachable inside a fight that ends
	 *  in three hits. The delay is what makes consecutive hits mean something, exactly as
	 *  StaminaRegenDelay does for attacking.
	 */
	UPROPERTY(EditAnywhere, Category = "Tuning|Vitals") float PoiseRegenDelay = 1.5f;

	/**
	 *  0.7s: long enough that an immediate follow-up connects, short enough that it is not
	 *  guaranteed. A light lands 0.75s after the previous one (0.40 recovery + 0.35
	 *  wind-up), so the victim recovers a hair before it arrives unless the attacker
	 *  committed instantly. The opening is real but it must be taken.
	 */
	UPROPERTY(EditAnywhere, Category = "Tuning|Vitals") float StaggerDuration = 0.7f;

	/** Blocking absorbs impact as well as damage, or heavies would make blocking pointless. */
	UPROPERTY(EditAnywhere, Category = "Tuning|Defence") float BlockedPoiseRatio = 0.5f;

	// ---- tuning: attacks ----------------------------------------------------
	/** 100 health, so three lights or two heavies. Doc 18 asks for a 2-3 hit fight. */
	UPROPERTY(EditAnywhere, Category = "Tuning|Light") float LightDamage = 40.f;
	UPROPERTY(EditAnywhere, Category = "Tuning|Light") float LightWindup = 0.35f;
	UPROPERTY(EditAnywhere, Category = "Tuning|Light") float LightActive = 0.10f;
	UPROPERTY(EditAnywhere, Category = "Tuning|Light") float LightRecovery = 0.40f;
	UPROPERTY(EditAnywhere, Category = "Tuning|Light") float LightStaminaCost = 20.f;
	/** Two consecutive lights stagger. One does not - the poke is safe, not decisive. */
	UPROPERTY(EditAnywhere, Category = "Tuning|Light") float LightPoiseDamage = 55.f;

	UPROPERTY(EditAnywhere, Category = "Tuning|Heavy") float HeavyDamage = 55.f;
	UPROPERTY(EditAnywhere, Category = "Tuning|Heavy") float HeavyWindup = 0.70f;
	UPROPERTY(EditAnywhere, Category = "Tuning|Heavy") float HeavyActive = 0.14f;
	UPROPERTY(EditAnywhere, Category = "Tuning|Heavy") float HeavyRecovery = 0.80f;
	UPROPERTY(EditAnywhere, Category = "Tuning|Heavy") float HeavyStaminaCost = 35.f;
	/**
	 *  Equal to MaxPoise, so an unblocked heavy always staggers. This is what gives the two
	 *  attacks distinct jobs: light is the poke, heavy is the interrupt. It is also the
	 *  only way to break a committed wind-up, and it costs a 0.70s tell to do it.
	 */
	UPROPERTY(EditAnywhere, Category = "Tuning|Heavy") float HeavyPoiseDamage = 100.f;

	/** Spear reach. Spacing reads more legibly with reach than with a sword. */
	UPROPERTY(EditAnywhere, Category = "Tuning|Reach") float AttackRange = 220.f;
	UPROPERTY(EditAnywhere, Category = "Tuning|Reach") float AttackRadius = 45.f;

	// ---- tuning: defence ----------------------------------------------------
	UPROPERTY(EditAnywhere, Category = "Tuning|Defence") float BlockStaminaRatio = 0.40f;
	UPROPERTY(EditAnywhere, Category = "Tuning|Defence") float DodgeStaminaCost = 25.f;
	UPROPERTY(EditAnywhere, Category = "Tuning|Defence") float DodgeDuration = 0.35f;
	UPROPERTY(EditAnywhere, Category = "Tuning|Defence") float DodgeImpulse = 900.f;

	/**
	 *  Deliberately absent: invincibility frames. Dodge is a committed movement burst,
	 *  not a phase-through, which makes spacing the entire skill. This is the riskiest
	 *  single decision in Stage 0 and the first suspect if the prototype reads as unfair
	 *  while the telegraphs are demonstrably clean.
	 */

	// ---- rotation clamp -----------------------------------------------------
	/**
	 *  The most important fairness rule in the prototype. Enemies that snap-track through
	 *  their own attack are the commonest reason commitment combat feels cheap: the dodge
	 *  is correct, the hit lands anyway. With i-frames removed, this clamp does all the
	 *  work i-frames would otherwise do.
	 */
	UPROPERTY(EditAnywhere, Category = "Tuning|Fairness") float TrackingCapWindupDegPerSec = 30.f;
	UPROPERTY(EditAnywhere, Category = "Tuning|Fairness") float TrackingCapActiveDegPerSec = 0.f;

	// ---- animation ----------------------------------------------------------
	/**
	 *  One montage per attack, not one per phase: wind-up, active and recovery are parts
	 *  of a single swing and splitting them across assets would only add seams.
	 *
	 *  The frame data above stays authoritative and the clip is play-rate scaled to fit
	 *  it, so every timing remains tunable from a slider. That trade is deliberate - doc
	 *  18's protocol changes one number per session, and it cannot wait on re-authored
	 *  animation. Revisit once the timings settle and fidelity starts to matter more.
	 *
	 *  Swapping these four is how an armed moveset replaces the unarmed one.
	 */
	UPROPERTY(EditAnywhere, Category = "Animation") UAnimMontage* LightMontage;
	UPROPERTY(EditAnywhere, Category = "Animation") UAnimMontage* HeavyMontage;
	UPROPERTY(EditAnywhere, Category = "Animation") UAnimMontage* StaggerMontage;

	/** Plays unscaled - a death has no frame data to honour. */
	UPROPERTY(EditAnywhere, Category = "Animation") UAnimMontage* DeathMontage;

	// ---- queries ------------------------------------------------------------
	UFUNCTION(BlueprintPure, Category = "Ichor|Combat") EIchorCombatState GetState() const { return State; }
	UFUNCTION(BlueprintPure, Category = "Ichor|Combat") float GetHealth() const { return Health; }
	UFUNCTION(BlueprintPure, Category = "Ichor|Combat") float GetStamina() const { return Stamina; }
	UFUNCTION(BlueprintPure, Category = "Ichor|Combat") float GetPoise() const { return Poise; }
	UFUNCTION(BlueprintPure, Category = "Ichor|Combat") bool IsAlive() const { return State != EIchorCombatState::Dead; }
	UFUNCTION(BlueprintPure, Category = "Ichor|Combat") bool IsBlocking() const { return bBlocking && State == EIchorCombatState::Idle; }

	/** True while committed to an attack - used by the rotation clamp and whiff-punish detection. */
	UFUNCTION(BlueprintPure, Category = "Ichor|Combat")
	bool IsCommitted() const { return State == EIchorCombatState::Windup || State == EIchorCombatState::Active || State == EIchorCombatState::Recovery; }

	/** Degrees per second this actor may turn right now. */
	UFUNCTION(BlueprintPure, Category = "Ichor|Combat") float GetAllowedTurnRate() const;

	// ---- actions ------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "Ichor|Combat") bool TryAttack(EIchorAttack Attack);
	UFUNCTION(BlueprintCallable, Category = "Ichor|Combat") bool TryDodge(FVector Direction);
	UFUNCTION(BlueprintCallable, Category = "Ichor|Combat") void SetBlocking(bool bWantsToBlock);

	/** Returns damage actually applied after blocking. */
	UFUNCTION(BlueprintCallable, Category = "Ichor|Combat")
	float ReceiveHit(float Damage, float PoiseDamage, AActor* FromActor, bool bAttackerWasOnScreen);

	// ---- events (instrumentation hooks) --------------------------------------
	UPROPERTY(BlueprintAssignable, Category = "Ichor|Combat") FIchorDamaged OnDamaged;
	UPROPERTY(BlueprintAssignable, Category = "Ichor|Combat") FIchorDied OnDied;

	/** One-line state dump for the always-on debug overlay. */
	FString GetDebugLine() const;

protected:

	virtual void BeginPlay() override;

private:

	void EnterState(EIchorCombatState NewState);
	void PerformAttackTrace();

	/**
	 *  Stands in for the attack animation that does not exist yet. Without it a swing is
	 *  three quarters of a second of standing still, and the fight is invisible.
	 *
	 *  Scaffolding for judging whether the loop runs - explicitly NOT for judging
	 *  readability. Doc 18 is clear that wind-up timing cannot be placeholdered, and a
	 *  drawn shape flatters the tell in a way a real animation will not.
	 */
	void DrawTelegraph() const;

	/** Stretches Montage to last exactly TargetDuration. Pass 0 to play it unscaled. */
	void PlayMontageScaled(UAnimMontage* Montage, float TargetDuration) const;

	EIchorCombatState State = EIchorCombatState::Idle;
	EIchorAttack PendingAttack = EIchorAttack::Light;

	float Health = 100.f;
	float Stamina = 100.f;
	float Poise = 100.f;

	float StateTimer = 0.f;
	float TimeSinceStaminaSpend = 999.f;
	float TimeSincePoiseDamage = 999.f;

	bool bBlocking = false;
	bool bTraceDoneThisSwing = false;

	/** Set when the killing blow lands so the cause is stamped, not recalled. */
	EIchorDeathCause PendingDeathCause = EIchorDeathCause::Unknown;
};
