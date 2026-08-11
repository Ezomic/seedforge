// Stage 0 fight instrumentation. Disposable by design - see docs/18-vertical-slice.md.

#include "IchorFightRecorder.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"
#include "Ichor.h"

void UIchorFightRecorder::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	CsvPath = FPaths::ProjectSavedDir() / TEXT("Fights") / TEXT("fights.csv");

	// Header once, on first creation. Appending to a file with no header produces a
	// spreadsheet nobody can read a month later.
	if (!FPaths::FileExists(CsvPath))
	{
		const FString Header = TEXT(
			"fight,seconds,gap_to_retry,loser,cause,"
			"p_hp,p_attacks,p_hits,p_whiffs,p_blocked,p_dodges,p_staggers,p_dealt,p_taken,"
			"f_hp,f_attacks,f_hits,f_whiffs,f_blocked,f_dodges,f_staggers,f_dealt,f_taken\n");
		FFileHelper::SaveStringToFile(Header, *CsvPath);
	}
}

UIchorFightRecorder* UIchorFightRecorder::For(const AActor* Actor)
{
	const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UIchorFightRecorder>() : nullptr;
}

bool UIchorFightRecorder::IsPlayerSide(const AActor* Who)
{
	const APawn* Pawn = Cast<APawn>(Who);
	return Pawn && Pawn->IsPlayerControlled();
}

FIchorSideRecord& UIchorFightRecorder::SideFor(AActor* Who)
{
	return IsPlayerSide(Who) ? Player : Foe;
}

void UIchorFightRecorder::BeginFightIfNeeded()
{
	if (bFightRunning) { return; }

	// Refuse to open a record while anyone is already dead. The skirmisher will happily
	// keep swinging at a corpse, and each connect would start a fresh fight - filling the
	// protocol's dataset with duels that never happened.
	if (const UWorld* World = GetWorld())
	{
		for (TObjectIterator<UIchorCombatComponent> It; It; ++It)
		{
			const UIchorCombatComponent* C = *It;
			if (C && C->GetWorld() == World && !C->IsAlive()) { return; }
		}
	}

	Player = FIchorSideRecord();
	Foe = FIchorSideRecord();
	bFightRunning = true;
	FightStartTime = FPlatformTime::Seconds();

	UE_LOG(LogIchor, Log, TEXT("[fight]  #%d started"), FightNumber + 1);
}

void UIchorFightRecorder::NoteAttack(AActor* Who, EIchorAttack Attack)
{
	// A fight starts at the first swing, not at aggro. Two actors circling each other
	// without committing is not yet a fight, and counting it as one inflates every duration.
	BeginFightIfNeeded();
	SideFor(Who).Attacks++;
}

void UIchorFightRecorder::NoteHit(AActor* Attacker, AActor* Victim, float Damage, bool bBlocked)
{
	BeginFightIfNeeded();

	FIchorSideRecord& A = SideFor(Attacker);
	FIchorSideRecord& V = SideFor(Victim);

	A.Hits++;
	A.DamageDealt += Damage;
	V.DamageTaken += Damage;
	if (bBlocked) { V.Blocked++; }
}

void UIchorFightRecorder::NoteWhiff(AActor* Who)  { if (bFightRunning) { SideFor(Who).Whiffs++; } }
void UIchorFightRecorder::NoteDodge(AActor* Who)  { if (bFightRunning) { SideFor(Who).Dodges++; } }
void UIchorFightRecorder::NoteStagger(AActor* Who){ if (bFightRunning) { SideFor(Who).Staggers++; } }

void UIchorFightRecorder::NoteDeath(AActor* Who, EIchorDeathCause Cause)
{
	if (!bFightRunning) { return; }
	WriteRow(Who, Cause);

	bFightRunning = false;
	FightNumber++;
	LastFightEndTime = FPlatformTime::Seconds();
}

void UIchorFightRecorder::WriteRow(AActor* Loser, EIchorDeathCause Cause)
{
	// Whatever health each side has left at the moment the fight ends. A win with 5hp and
	// a win with 95hp are the same result and completely different experiences.
	if (const UWorld* World = GetWorld())
	{
		for (TObjectIterator<UIchorCombatComponent> It; It; ++It)
		{
			UIchorCombatComponent* C = *It;
			if (!C || C->GetWorld() != World) { continue; }
			if (AActor* Owner = C->GetOwner())
			{
				SideFor(Owner).HealthLeft = C->GetHealth();
			}
		}
	}

	static const UEnum* CauseEnum = StaticEnum<EIchorDeathCause>();
	const FString CauseName = CauseEnum
		? CauseEnum->GetNameStringByValue((int64)Cause) : TEXT("?");

	const double Now = FPlatformTime::Seconds();
	const float Duration = (float)(Now - FightStartTime);

	// Zero on the first fight of a session rather than a meaningless number measured from
	// engine start.
	const float Gap = (FightNumber > 0) ? (float)(FightStartTime - LastFightEndTime) : 0.f;

	const FString Row = FString::Printf(
		TEXT("%d,%.2f,%.2f,%s,%s,")
		TEXT("%.0f,%d,%d,%d,%d,%d,%d,%.0f,%.0f,")
		TEXT("%.0f,%d,%d,%d,%d,%d,%d,%.0f,%.0f\n"),
		FightNumber + 1, Duration, Gap,
		IsPlayerSide(Loser) ? TEXT("player") : TEXT("foe"), *CauseName,
		Player.HealthLeft, Player.Attacks, Player.Hits, Player.Whiffs, Player.Blocked,
		Player.Dodges, Player.Staggers, Player.DamageDealt, Player.DamageTaken,
		Foe.HealthLeft, Foe.Attacks, Foe.Hits, Foe.Whiffs, Foe.Blocked,
		Foe.Dodges, Foe.Staggers, Foe.DamageDealt, Foe.DamageTaken);

	FFileHelper::SaveStringToFile(Row, *CsvPath, FFileHelper::EEncodingOptions::AutoDetect,
		&IFileManager::Get(), EFileWrite::FILEWRITE_Append);

	UE_LOG(LogIchor, Log,
		TEXT("[fight]  #%d %s in %.1fs - %s died (%s) | player %d/%d hits, %d whiffs, %d dodges, %.0f hp left"),
		FightNumber + 1, IsPlayerSide(Loser) ? TEXT("LOST") : TEXT("won"), Duration,
		IsPlayerSide(Loser) ? TEXT("player") : TEXT("foe"), *CauseName,
		Player.Hits, Player.Attacks, Player.Whiffs, Player.Dodges, Player.HealthLeft);
}
