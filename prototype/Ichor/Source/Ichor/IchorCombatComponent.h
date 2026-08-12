// Stage 0 grey-box combat. Disposable by design - see docs/18-vertical-slice.md.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IchorCombatComponent.generated.h"

class UIchorCombatComponent;
class UAnimMontage;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class USoundBase;

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
 *  One weapon class, not one weapon - doc 05 puts movesets on the class. Stage 0 has
 *  exactly one armed loadout because doc 18's slice is spear and shield, and a second
 *  weapon is on its explicit not-in-scope list.
 */
UENUM(BlueprintType)
enum class EIchorLoadout : uint8
{
	Unarmed,
	SpearAndShield
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

	/**
	 *  The armed set. Different source clips from unarmed so the two loadouts read as
	 *  different fighters rather than the same one holding a stick - but it is the same
	 *  mannequin motion underneath. No spear animation exists in the project and none has
	 *  been authored yet, so these are honest placeholders with correct frame data, which
	 *  is the half doc 18 says cannot be faked.
	 */
	UPROPERTY(EditAnywhere, Category = "Animation|Spear") UAnimMontage* SpearLightMontage;
	UPROPERTY(EditAnywhere, Category = "Animation|Spear") UAnimMontage* SpearHeavyMontage;
	UPROPERTY(EditAnywhere, Category = "Animation|Spear") UAnimMontage* SpearStaggerMontage;
	UPROPERTY(EditAnywhere, Category = "Animation|Spear") UAnimMontage* SpearDeathMontage;

	// ---- telegraphs ---------------------------------------------------------
	/**
	 *  Half the readability variable, and the half that still works when the attacker is
	 *  behind you - which is why doc 18's anti-frustration rules forbid an attack from off
	 *  screen without one.
	 *
	 *  The two contours are deliberately opposite: light rises, heavy falls. If they
	 *  differed only in volume the player would have to judge loudness under pressure,
	 *  which nobody can do.
	 */
	UPROPERTY(EditAnywhere, Category = "Telegraph") USoundBase* LightWindupCue;
	UPROPERTY(EditAnywhere, Category = "Telegraph") USoundBase* HeavyWindupCue;

	// ---- loadout ------------------------------------------------------------
	/** AttackRange is already 220 - spear reach - so armed needs no separate tuning. */
	UPROPERTY(EditAnywhere, Category = "Loadout") EIchorLoadout Loadout = EIchorLoadout::SpearAndShield;

	/**
	 *  Grey-box weapons built from engine primitives at BeginPlay. Doc 18 says make
	 *  nothing custom for the grey box, and a spear that is a scaled cylinder is on-spec.
	 *
	 *  These transforms are the one thing here I cannot get right without looking at it -
	 *  hand bone axes are not something to reason about from a bone list. They are
	 *  EditAnywhere so they can be dragged into place without a recompile.
	 */
	UPROPERTY(EditAnywhere, Category = "Loadout|Spear") FName SpearAttachBone = TEXT("hand_r");
	UPROPERTY(EditAnywhere, Category = "Loadout|Spear") FVector SpearOffset = FVector(0.f, 0.f, 0.f);
	UPROPERTY(EditAnywhere, Category = "Loadout|Spear") FRotator SpearRotation = FRotator(0.f, 0.f, 90.f);

	/** The generated meshes are already real-world size, so this is a correction, not a fit. */
	UPROPERTY(EditAnywhere, Category = "Loadout|Spear") FVector SpearScale = FVector(1.f, 1.f, 1.f);

	/**
	 *  Doc 16 puts covered faces on day one - it deletes facial rigging, animation and
	 *  lipsync from the project in one decision, and a Corinthian helm happens to be
	 *  exactly the right answer for hoplites. It also solves a grey-box problem: two
	 *  identical mannequins are hard to tell apart mid-fight, and a crest colour is
	 *  readable from any angle at any distance.
	 */
	UPROPERTY(EditAnywhere, Category = "Loadout|Helmet") FName HelmetAttachBone = TEXT("head");
	UPROPERTY(EditAnywhere, Category = "Loadout|Helmet") FVector HelmetOffset = FVector(0.f, 0.f, 0.f);
	UPROPERTY(EditAnywhere, Category = "Loadout|Helmet") FRotator HelmetRotation = FRotator(0.f, 0.f, 0.f);

	/** Material slot 1 on the helmet. Set this differently per Blueprint to tell sides apart. */
	UPROPERTY(EditAnywhere, Category = "Loadout|Helmet") UMaterialInterface* CrestMaterial;

	UPROPERTY(EditAnywhere, Category = "Loadout|Shield") FName ShieldAttachBone = TEXT("hand_l");
	UPROPERTY(EditAnywhere, Category = "Loadout|Shield") FVector ShieldOffset = FVector(0.f, 0.f, 0.f);
	UPROPERTY(EditAnywhere, Category = "Loadout|Shield") FRotator ShieldRotation = FRotator(90.f, 0.f, 0.f);
	UPROPERTY(EditAnywhere, Category = "Loadout|Shield") FVector ShieldScale = FVector(1.f, 1.f, 1.f);

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

	/** Put this fighter back exactly as it started: full vitals, idle, back on its mark. */
	void ResetForNewFight();

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

	/** Armed choice when one exists, falling back to unarmed rather than playing nothing. */
	UAnimMontage* PickMontage(UAnimMontage* Unarmed, UAnimMontage* Armed) const;

	/** Spawns and attaches the grey-box spear and shield. No-op when unarmed. */
	void BuildWeaponVisuals();

	UPROPERTY() UStaticMeshComponent* SpearMesh = nullptr;
	UPROPERTY() UStaticMeshComponent* ShieldMesh = nullptr;

	/**
	 *  Generated by Scripts/make_weapon_meshes.py rather than bought or modelled: a dory
	 *  is a shaft, a leaf head and a sauroter, an aspis is a domed disc with a rolled rim.
	 *  Both are built along +Z with the origin at the grip, so attaching needs rotation
	 *  rather than a fudged offset.
	 */
	UPROPERTY(EditAnywhere, Category = "Loadout|Spear") UStaticMesh* SpearMeshAsset;
	UPROPERTY(EditAnywhere, Category = "Loadout|Shield") UStaticMesh* ShieldMeshAsset;
	UPROPERTY(EditAnywhere, Category = "Loadout|Helmet") UStaticMesh* HelmetMeshAsset;

	/** Bronze, shared by all three pieces of kit. */
	UPROPERTY(EditAnywhere, Category = "Loadout") UMaterialInterface* KitMaterial;

	UPROPERTY() UStaticMeshComponent* HelmetMesh = nullptr;

	EIchorCombatState State = EIchorCombatState::Idle;
	EIchorAttack PendingAttack = EIchorAttack::Light;

	float Health = 100.f;
	float Stamina = 100.f;
	float Poise = 100.f;

	float StateTimer = 0.f;
	float TimeSinceStaminaSpend = 999.f;
	float TimeSincePoiseDamage = 999.f;

	/**
	 *  One-shot footing check.
	 *
	 *  Every headless run so far proved the fight loop worked while the arena floor had no
	 *  collision at all and both fighters were falling - they fall at the same rate, stay
	 *  in reach, and every log line looks normal. "The fight ran" is not the same claim as
	 *  "anyone was standing on anything", and only one of them was ever being checked.
	 */
	float FootingTimer = 0.f;
	bool bFootingReported = false;

	/** Captured at BeginPlay so a retry returns everyone to their marks, not to wherever
	 *  the last fight happened to end. */
	FTransform SpawnTransform;

	bool bBlocking = false;
	bool bTraceDoneThisSwing = false;

	/** Set when the killing blow lands so the cause is stamped, not recalled. */
	EIchorDeathCause PendingDeathCause = EIchorDeathCause::Unknown;
};
