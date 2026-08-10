// Stage 0 grey-box combat. Disposable by design - see docs/18-vertical-slice.md.

#include "IchorCombatComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "DrawDebugHelpers.h"
#include "Animation/AnimMontage.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Ichor.h"

UIchorCombatComponent::UIchorCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Defaults, so the player and the skirmisher both animate without per-Blueprint
	// wiring - and so a montage cannot go missing because an asset edit silently failed.
	// Every one is EditAnywhere, so an armed moveset still overrides them per Blueprint.
	static ConstructorHelpers::FObjectFinder<UAnimMontage> LightAsset(TEXT("/Game/Ichor/Anims/Unarmed/AM_Light_Unarmed"));
	static ConstructorHelpers::FObjectFinder<UAnimMontage> HeavyAsset(TEXT("/Game/Ichor/Anims/Unarmed/AM_Heavy_Unarmed"));
	static ConstructorHelpers::FObjectFinder<UAnimMontage> StaggerAsset(TEXT("/Game/Ichor/Anims/Unarmed/AM_Stagger_Unarmed"));
	static ConstructorHelpers::FObjectFinder<UAnimMontage> DeathAsset(TEXT("/Game/Ichor/Anims/Unarmed/AM_Death_Unarmed"));

	if (LightAsset.Succeeded())   { LightMontage = LightAsset.Object; }
	if (HeavyAsset.Succeeded())   { HeavyMontage = HeavyAsset.Object; }
	if (StaggerAsset.Succeeded()) { StaggerMontage = StaggerAsset.Object; }
	if (DeathAsset.Succeeded())   { DeathMontage = DeathAsset.Object; }

	static ConstructorHelpers::FObjectFinder<UAnimMontage> SpearLightAsset(TEXT("/Game/Ichor/Anims/Spear/AM_Light_Spear"));
	static ConstructorHelpers::FObjectFinder<UAnimMontage> SpearHeavyAsset(TEXT("/Game/Ichor/Anims/Spear/AM_Heavy_Spear"));
	static ConstructorHelpers::FObjectFinder<UAnimMontage> SpearStaggerAsset(TEXT("/Game/Ichor/Anims/Spear/AM_Stagger_Spear"));
	static ConstructorHelpers::FObjectFinder<UAnimMontage> SpearDeathAsset(TEXT("/Game/Ichor/Anims/Spear/AM_Death_Spear"));

	if (SpearLightAsset.Succeeded())   { SpearLightMontage = SpearLightAsset.Object; }
	if (SpearHeavyAsset.Succeeded())   { SpearHeavyMontage = SpearHeavyAsset.Object; }
	if (SpearStaggerAsset.Succeeded()) { SpearStaggerMontage = SpearStaggerAsset.Object; }
	if (SpearDeathAsset.Succeeded())   { SpearDeathMontage = SpearDeathAsset.Object; }

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(TEXT("/Engine/BasicShapes/Cylinder"));
	if (CylinderAsset.Succeeded()) { CylinderMesh = CylinderAsset.Object; }
}

UAnimMontage* UIchorCombatComponent::PickMontage(UAnimMontage* Unarmed, UAnimMontage* Armed) const
{
	if (Loadout == EIchorLoadout::SpearAndShield && Armed) { return Armed; }
	return Unarmed;
}

void UIchorCombatComponent::BuildWeaponVisuals()
{
	if (Loadout != EIchorLoadout::SpearAndShield) { return; }

	ACharacter* Ch = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Body = Ch ? Ch->GetMesh() : nullptr;
	if (!Body || !CylinderMesh)
	{
		UE_LOG(LogIchor, Warning, TEXT("[loadout] %s armed but no %s - going without"),
			*GetNameSafe(GetOwner()), Body ? TEXT("primitive mesh") : TEXT("skeletal mesh"));
		return;
	}

	// Both are the same cylinder at different scales. A spear is a long thin one and a
	// shield is a short fat one, which is as much as a grey box needs to say.
	struct FLocal
	{
		static UStaticMeshComponent* Make(ACharacter* Owner, USkeletalMeshComponent* Parent,
			UStaticMesh* Mesh, FName Bone, const FVector& Loc, const FRotator& Rot, const FVector& Scale)
		{
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
			C->SetStaticMesh(Mesh);

			// Never let a prop participate in the attack sweep - it traces on ECC_Pawn and
			// a weapon that blocks it would have the character hitting its own spear.
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetGenerateOverlapEvents(false);

			C->RegisterComponent();
			C->AttachToComponent(Parent, FAttachmentTransformRules::SnapToTargetIncludingScale, Bone);
			C->SetRelativeLocation(Loc);
			C->SetRelativeRotation(Rot);
			C->SetRelativeScale3D(Scale);
			return C;
		}
	};

	SpearMesh = FLocal::Make(Ch, Body, CylinderMesh, SpearAttachBone, SpearOffset, SpearRotation, SpearScale);
	ShieldMesh = FLocal::Make(Ch, Body, CylinderMesh, ShieldAttachBone, ShieldOffset, ShieldRotation, ShieldScale);

	UE_LOG(LogIchor, Log, TEXT("[loadout] %s armed: spear on %s, shield on %s"),
		*GetNameSafe(GetOwner()), *SpearAttachBone.ToString(), *ShieldAttachBone.ToString());
}

void UIchorCombatComponent::PlayMontageScaled(UAnimMontage* Montage, float TargetDuration) const
{
	ACharacter* Ch = Cast<ACharacter>(GetOwner());
	if (!Montage || !Ch) { return; }

	const float Length = Montage->GetPlayLength();

	// TargetDuration of 0 means "play it as authored".
	const float Rate = (TargetDuration > KINDA_SMALL_NUMBER && Length > KINDA_SMALL_NUMBER)
		? Length / TargetDuration
		: 1.f;

	Ch->PlayAnimMontage(Montage, Rate);
}

void UIchorCombatComponent::BeginPlay()
{
	Super::BeginPlay();
	Health = MaxHealth;
	Stamina = MaxStamina;
	Poise = MaxPoise;

	BuildWeaponVisuals();

	// A null montage plays nothing and says nothing, which is indistinguishable from an
	// animation that simply looks wrong. Say it out loud once, at BeginPlay - and report
	// what will actually play, not what is merely assigned.
	static const UEnum* LoadoutEnum = StaticEnum<EIchorLoadout>();
	UE_LOG(LogIchor, Log, TEXT("[anim]   %s  [%s]  light=%s heavy=%s stagger=%s death=%s"),
		*GetNameSafe(GetOwner()),
		LoadoutEnum ? *LoadoutEnum->GetNameStringByValue((int64)Loadout) : TEXT("?"),
		*GetNameSafe(PickMontage(LightMontage, SpearLightMontage)),
		*GetNameSafe(PickMontage(HeavyMontage, SpearHeavyMontage)),
		*GetNameSafe(PickMontage(StaggerMontage, SpearStaggerMontage)),
		*GetNameSafe(PickMontage(DeathMontage, SpearDeathMontage)));
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

	// A blocked heavy would otherwise guarantee a stagger, which makes blocking a heavy
	// strictly worse than doing nothing.
	const float AppliedPoise = bBlocked ? PoiseDamage * BlockedPoiseRatio : PoiseDamage;
	if (AppliedPoise > 0.f)
	{
		Poise = FMath::Max(0.f, Poise - AppliedPoise);
		TimeSincePoiseDamage = 0.f;
	}

	OnDamaged.Broadcast(Applied, FromActor, bBlocked);

	UE_LOG(LogIchor, Log, TEXT("[hit]    %s took %.0f from %s%s  (HP %.0f, ST %.0f)"),
		*GetNameSafe(GetOwner()), Applied, *GetNameSafe(FromActor),
		bBlocked ? TEXT(" [blocked]") : TEXT(""), Health, Stamina);

#if ENABLE_DRAW_DEBUG
	// Connection needs to be felt before it can be tuned, and there is no hit reaction
	// animation to feel it through yet.
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 1.5f, bBlocked ? FColor::Cyan : FColor::Orange,
			FString::Printf(TEXT("%s  -%.0f%s"), *GetNameSafe(GetOwner()), Applied,
				bBlocked ? TEXT("  (blocked)") : TEXT("")));
	}
#endif

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

		// The cause, not just the fact. Doc 18's gate reads the distribution of these.
		static const UEnum* CauseEnum = StaticEnum<EIchorDeathCause>();
		UE_LOG(LogIchor, Log, TEXT("[death]  %s killed by %s - cause: %s"),
			*GetNameSafe(GetOwner()), *GetNameSafe(FromActor),
			CauseEnum ? *CauseEnum->GetNameStringByValue((int64)PendingDeathCause) : TEXT("?"));

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
	static const UEnum* E = StaticEnum<EIchorCombatState>();
	UE_LOG(LogIchor, Log, TEXT("[combat] %-20s %s -> %s  (HP %.0f, ST %.0f, PO %.0f)"),
		*GetNameSafe(GetOwner()),
		E ? *E->GetNameStringByValue((int64)State) : TEXT("?"),
		E ? *E->GetNameStringByValue((int64)NewState) : TEXT("?"),
		Health, Stamina, Poise);

	State = NewState;
	StateTimer = 0.f;

	if (NewState == EIchorCombatState::Staggered)
	{
		Poise = 0.f;
	}

	// Montages share DefaultSlot, so starting one interrupts whatever was playing. That
	// is what makes a stagger visibly cut an attack short rather than overlap it.
	switch (NewState)
	{
	case EIchorCombatState::Windup:
	{
		const bool bHeavy = (PendingAttack == EIchorAttack::Heavy);
		const float Total = bHeavy
			? HeavyWindup + HeavyActive + HeavyRecovery
			: LightWindup + LightActive + LightRecovery;
		PlayMontageScaled(bHeavy
			? PickMontage(HeavyMontage, SpearHeavyMontage)
			: PickMontage(LightMontage, SpearLightMontage), Total);
		break;
	}
	case EIchorCombatState::Staggered:
		PlayMontageScaled(PickMontage(StaggerMontage, SpearStaggerMontage), StaggerDuration);
		break;

	case EIchorCombatState::Dead:
		PlayMontageScaled(PickMontage(DeathMontage, SpearDeathMontage), 0.f);
		break;

	default:
		break;
	}
}

void UIchorCombatComponent::DrawTelegraph() const
{
#if ENABLE_DRAW_DEBUG
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World) { return; }

	const bool bHeavy = (PendingAttack == EIchorAttack::Heavy);
	const FVector Base = Owner->GetActorLocation();
	const FVector Fwd = Owner->GetActorForwardVector();

	if (State == EIchorCombatState::Windup)
	{
		const float Dur = bHeavy ? HeavyWindup : LightWindup;
		const float T = FMath::Clamp(StateTimer / FMath::Max(Dur, KINDA_SMALL_NUMBER), 0.f, 1.f);

		// Charge reads as the shape growing out to where the blow will land, so the
		// distance closing is the tell rather than a colour change alone.
		const FColor Col = bHeavy ? FColor(255, 90, 0) : FColor(255, 210, 60);
		DrawDebugSphere(World, Base + Fwd * AttackRange * T, AttackRadius * (0.4f + 0.6f * T),
			12, Col, false, -1.f, 0, bHeavy ? 3.f : 1.5f);
		DrawDebugLine(World, Base, Base + Fwd * AttackRange * T, Col, false, -1.f, 0, 2.f);
	}
	else if (State == EIchorCombatState::Active)
	{
		DrawDebugCapsule(World, Base + Fwd * AttackRange * 0.5f, AttackRange * 0.5f, AttackRadius,
			FRotationMatrix::MakeFromZ(Fwd).ToQuat(), FColor::Red, false, -1.f, 0, 3.f);
	}
#endif
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
	TimeSincePoiseDamage += DeltaTime;

	// Stamina only recovers once the owner stops spending it.
	if (TimeSinceStaminaSpend >= StaminaRegenDelay && !IsBlocking())
	{
		Stamina = FMath::Min(MaxStamina, Stamina + StaminaRegenPerSec * DeltaTime);
	}

	// Same shape as stamina: recovery starts only once the pressure stops.
	if (State == EIchorCombatState::Idle && TimeSincePoiseDamage >= PoiseRegenDelay)
	{
		Poise = FMath::Min(MaxPoise, Poise + PoiseRegenPerSec * DeltaTime);
	}

	DrawTelegraph();

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
			// Full, not half. At half, the next light would re-stagger immediately and the
			// regen delay would hold it there - a stunlock, not a fight. Full poise also
			// means nobody is staggered twice in a fight that ends in three hits, so the
			// opening a stagger buys has to actually be converted.
			Poise = MaxPoise;
			TimeSincePoiseDamage = 999.f;
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
