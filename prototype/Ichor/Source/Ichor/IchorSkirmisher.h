// Stage 0 grey-box enemy. Disposable by design - see docs/18-vertical-slice.md.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "IchorSkirmisher.generated.h"

class UIchorCombatComponent;

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

	/** Must return to spacing before committing again, guaranteeing a beat between attacks. */
	UPROPERTY(EditAnywhere, Category = "Tuning|AI") float ReCommitSpacingTolerance = 90.f;

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
	float BaseWalkSpeed = 400.f;
};
