// Stage 0 grey-box enemy. Disposable by design - see docs/18-vertical-slice.md.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "IchorSkirmisher.generated.h"

class UIchorCombatComponent;
class USoundBase;

/**
 *  Neutral is the most important state. Tension lives there, and a fight with no neutral
 *  contains no decisions. Stagger and Dead are deliberately absent - those live on the
 *  combat component, and this machine simply defers while they are active.
 */
UENUM(BlueprintType)
enum class EIchorSkirmisherState : uint8
{
	Unaware,
	Approach,
	Neutral,
	Tell,
	Committing,
	Punish,
	Rest
};

/**
 *  A rhythm generator, not an opponent.
 *
 *  Its job is not to be smart or to win. It is to produce readable pressure with legible
 *  gaps - the player continuously choosing under threat, able to see openings and take or
 *  decline them. A plain enum and switch, not a Behaviour Tree: for one enemy in
 *  disposable code, blackboards and task nodes are ceremony.
 */
UCLASS()
class AIchorSkirmisher : public ACharacter
{
	GENERATED_BODY()

public:

	AIchorSkirmisher();

	virtual void Tick(float DeltaSeconds) override;

	UIchorCombatComponent* GetCombat() const { return Combat; }

	/** Back to Unaware with every accumulator cleared, so a retry is not half a fight in. */
	void ResetAI();

	// ---- the six knobs the 200-fight protocol turns ---------------------------
	/** How fast the urge to attack accumulates while in the engagement band. */
	UPROPERTY(EditAnywhere, Category = "Tuning|AI") float DesireGainRate = 0.35f;

	/** Just outside spear reach, so the player can read the gap. */
	UPROPERTY(EditAnywhere, Category = "Tuning|AI") float PreferredSpacing = 250.f;

	/** The pause is designed content. Continuous pressure removes decision-making. */
	UPROPERTY(EditAnywhere, Category = "Tuning|AI") float RestDuration = 1.5f;

	/** Delay before punishing a whiff, so it reads as a reaction rather than a script. */
	UPROPERTY(EditAnywhere, Category = "Tuning|AI") float PunishReactionTime = 0.25f;

	/** Scales this enemy's wind-ups without touching the shared combat defaults. */
	UPROPERTY(EditAnywhere, Category = "Tuning|AI") float WindupScale = 1.0f;

	/** Mirrors onto the combat component so both clamps are tuned from one place. */
	UPROPERTY(EditAnywhere, Category = "Tuning|AI") float TrackingCapDegPerSec = 30.f;

	// ---- supporting parameters ------------------------------------------------
	UPROPERTY(EditAnywhere, Category = "Tuning|AI") float DesireThreshold = 1.0f;
	UPROPERTY(EditAnywhere, Category = "Tuning|AI") float DesireGainVsRecovering = 0.6f;
	UPROPERTY(EditAnywhere, Category = "Tuning|AI") int32 RestAfterCommits = 3;
	UPROPERTY(EditAnywhere, Category = "Tuning|AI") float RestAfterSeconds = 6.f;
	UPROPERTY(EditAnywhere, Category = "Tuning|AI") float TellDuration = 0.15f;
	UPROPERTY(EditAnywhere, Category = "Tuning|AI") float AggroRange = 1500.f;
	UPROPERTY(EditAnywhere, Category = "Tuning|AI") float StrafeSpeedScale = 0.55f;

	/**
	 *  How many blows it may land into a stagger before it has to let the player up.
	 *
	 *  Doc 18 lists this among the anti-frustration rules for a reason: being hit, being
	 *  unable to act, and being hit again is the commonest way commitment combat reads as
	 *  cheap. One follow-up is a punish; two is a stunlock, and the player cannot tell the
	 *  difference between a stunlock and a broken game.
	 */
	UPROPERTY(EditAnywhere, Category = "Tuning|AI") int32 MaxFollowUpsIntoStagger = 1;

	/** Must return to spacing before committing again, guaranteeing a beat between attacks. */
	UPROPERTY(EditAnywhere, Category = "Tuning|AI") float ReCommitSpacingTolerance = 90.f;

	/**
	 *  How close it must be before a swing can actually land, and therefore before it is
	 *  allowed to start one. Distinct from the spacing band: the band is where it wants to
	 *  stand, this is where its weapon reaches. Gating commitment on the band instead let it
	 *  swing from outside its own reach and whiff every time.
	 *
	 *  Derived at BeginPlay from the combat component's reach; the editor value is a fallback.
	 */
	UPROPERTY(EditAnywhere, Category = "Tuning|AI") float StrikeRange = 300.f;

	/** The weight shift, 0.15s before the wind-up proper. Quiet and dry on purpose - it
	 *  must read as the moment before an attack, never as the attack. */
	UPROPERTY(EditAnywhere, Category = "Telegraph") USoundBase* TellCue;

	UPROPERTY(EditAnywhere, Category = "Debug") bool bShowDebug = true;

protected:

	virtual void BeginPlay() override;

private:

	void EnterState(EIchorSkirmisherState NewState);
	void FaceTarget(float DeltaSeconds);
	void MoveToward(const FVector& WorldDir, float Scale);

	UPROPERTY(VisibleAnywhere, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UIchorCombatComponent* Combat;

	UPROPERTY() AActor* Target = nullptr;

	EIchorSkirmisherState State = EIchorSkirmisherState::Unaware;
	float StateTimer = 0.f;
	float Desire = 0.f;
	float PressureTimer = 0.f;
	int32 CommitsSinceRest = 0;
	int32 StrafeSign = 1;

	/** Reset when the target stands up, so the allowance is per stagger, not per fight. */
	int32 FollowUpsThisStagger = 0;
	float BaseWalkSpeed = 400.f;
};
