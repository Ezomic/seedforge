// Stage 0 fight instrumentation. Disposable by design - see docs/18-vertical-slice.md.

#include "IchorFightRecorder.h"
#include "GameFramework/Pawn.h"
#include "IchorSkirmisher.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/UObjectIterator.h"
#include "Ichor.h"

void UIchorFightRecorder::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Automated runs write somewhere else. Every headless verification appends a fight
	// nobody played, and those rows sit in the same file the 200-fight protocol reads -
	// with zero attacks and a duration set by whatever the AI did to a motionless dummy.
	// Deleting them by hand after each check is a step that will eventually be forgotten.
	const bool bAutomated = FApp::IsUnattended()
		|| FParse::Param(FCommandLine::Get(), TEXT("nullrhi"));

	CsvPath = FPaths::ProjectSavedDir() / TEXT("Fights")
		/ (bAutomated ? TEXT("fights_headless.csv") : TEXT("fights.csv"));

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
	else
	{
		// Carry the count across sessions. Every fight was numbered 1 because the subsystem
		// is rebuilt with the world, so twenty fights produced twenty rows all called the
		// first one - which is useless for a protocol that reads them in order.
		FString Existing;
		if (FFileHelper::LoadFileToString(Existing, *CsvPath))
		{
			TArray<FString> Lines;
			Existing.ParseIntoArrayLines(Lines);
			FightNumber = FMath::Max(0, Lines.Num() - 1);
		}
	}

	UE_LOG(LogIchor, Log, TEXT("[fight]  log has %d fights already; next is #%d"),
		FightNumber, FightNumber + 1);
}

void UIchorFightRecorder::ResetFight()
{
	// An abandoned fight is not a result. Dropping it rather than writing a row keeps the
	// dataset to fights that actually ended.
	if (bFightRunning)
	{
		bFightRunning = false;
		UE_LOG(LogIchor, Log, TEXT("[fight]  #%d abandoned on retry - not recorded"), FightNumber + 1);
	}

	const UWorld* World = GetWorld();
	int32 Reset = 0;
	for (TObjectIterator<UIchorCombatComponent> It; It; ++It)
	{
		UIchorCombatComponent* C = *It;
		if (!C || C->GetWorld() != World) { continue; }
		C->ResetForNewFight();
		Reset++;
	}

	for (TObjectIterator<AIchorSkirmisher> It; It; ++It)
	{
		AIchorSkirmisher* S = *It;
		if (S && S->GetWorld() == World) { S->ResetAI(); }
	}

	// The clock for the next fight starts now, so the gap measures thinking time rather
	// than however long the corpse was left lying there.
	LastFightEndTime = FPlatformTime::Seconds();

	UE_LOG(LogIchor, Log, TEXT("[fight]  reset %d fighters - ready for #%d"),
		Reset, FightNumber + 1);
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
	// engine start. Keyed on the timestamp, not on FightNumber: the counter now persists
	// across sessions, so a fresh run starts at 8 and would subtract a LastFightEndTime of
	// zero - which produced a gap of sixteen million seconds.
	const float Gap = (LastFightEndTime > 0.0)
		? (float)(FightStartTime - LastFightEndTime)
		: 0.f;

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
