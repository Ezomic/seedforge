// Stage 0 fight instrumentation. Disposable by design - see docs/18-vertical-slice.md.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "IchorCombatComponent.h"
#include "IchorFightRecorder.generated.h"

/**
 *  One side's tally for one fight. Kept per side rather than globally because the useful
 *  questions are comparative: did the player whiff more than the skirmisher, did they
 *  block at all, were they ever staggered.
 */
USTRUCT()
struct FIchorSideRecord
{
	GENERATED_BODY()

	int32 Attacks = 0;
	int32 Hits = 0;

	/** Attacks whose active frames struck nothing. The cost of committing. */
	int32 Whiffs = 0;

	/** Blows this side absorbed on a block, not blows it blocked from landing elsewhere. */
	int32 Blocked = 0;

	int32 Dodges = 0;
	int32 Staggers = 0;

	float DamageDealt = 0.f;
	float DamageTaken = 0.f;

	float HealthLeft = 0.f;
};

/**
 *  Records every fight to a CSV so the 200-fight protocol has something to read.
 *
 *  Doc 18 is explicit that the distribution of death causes is the diagnosis: deaths
 *  dominated by "could not read the telegraph" mean the telegraphs are wrong, which is a
 *  different problem from the design being wrong and takes the opposite fix. Counting
 *  deaths tells you nothing; classifying them tells you which of the two you have.
 *
 *  Time-to-retry is here for the same reason it is in the gate criteria - a player who
 *  dies and immediately tries again is having a different experience from one who puts the
 *  pad down, and the gap between fights measures that without asking them.
 */
UCLASS()
class UIchorFightRecorder : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Convenience: the recorder for whatever world this actor is in. Null-safe. */
	static UIchorFightRecorder* For(const AActor* Actor);

	void NoteAttack(AActor* Who, EIchorAttack Attack);
	void NoteHit(AActor* Attacker, AActor* Victim, float Damage, bool bBlocked);
	void NoteWhiff(AActor* Who);
	void NoteDodge(AActor* Who);
	void NoteStagger(AActor* Who);
	void NoteDeath(AActor* Who, EIchorDeathCause Cause);

	/** Puts both fighters back on their marks and opens the way for a fresh record. */
	void ResetFight();

	/** Fights completed this session. */
	int32 GetFightNumber() const { return FightNumber; }

private:

	void BeginFightIfNeeded();
	void WriteRow(AActor* Loser, EIchorDeathCause Cause);
	FIchorSideRecord& SideFor(AActor* Who);
	static bool IsPlayerSide(const AActor* Who);

	FIchorSideRecord Player;
	FIchorSideRecord Foe;

	bool bFightRunning = false;
	int32 FightNumber = 0;

	double FightStartTime = 0.0;

	/** Set when a fight ends, so the next one can measure how long the player waited. */
	double LastFightEndTime = 0.0;

	FString CsvPath;
};
