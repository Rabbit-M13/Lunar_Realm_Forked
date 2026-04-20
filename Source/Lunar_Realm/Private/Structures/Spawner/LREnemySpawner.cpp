#include "Structures/Spawner/LREnemySpawner.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Core/Stage/LRStageGameMode.h"
#include "Core/LRGameInstance.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetMathLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "Subsystems/GameDataSubsystem.h"
#include "Subsystems/StageManagerSubsystem.h"
#include "Subsystems/PoolingSubsystem.h"
#include "System/LoggingSystem.h"
#include "Units/Enemy/LREnemyAIController.h"
#include "Units/Enemy/LREnemyBossCharacter.h"
#include "Units/Enemy/LREnemyCharacter.h"
#include "TimerManager.h"

// Sets default values
ALREnemySpawner::ALREnemySpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	
	SpawnAreaBox = CreateDefaultSubobject<UBoxComponent>(TEXT("SpawnAreaBox"));
	SetRootComponent(SpawnAreaBox);
	SpawnAreaBox->SetBoxExtent(FVector(300.0f, 300.0f, 100.0f));
	SpawnAreaBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);

}

void ALREnemySpawner::BeginPlay()
{
	Super::BeginPlay();

	UStageManagerSubsystem* StageMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UStageManagerSubsystem>() : nullptr;
	if (!StageMgr)
	{
		LR_ERROR(TEXT("EnemySpawner(%s): StageManagerSubsystem not found"), *GetName());
		return;
	}

	StageMgr->OnStageLoaded.AddDynamic(this, &ALREnemySpawner::OnStageLoaded);

	const FName CurrentLoadedStageID = StageMgr->GetCurrentStageID();
	if (CurrentLoadedStageID != NAME_None && CurrentLoadedStageID == StageIDToActivate)
	{
		ActivateSpawner();
	}
	else
	{
		return;
	}

	if (ALRStageGameMode* StageGM = Cast<ALRStageGameMode>(GetWorld()->GetAuthGameMode()))
	{
		StageGM->OnGameStarted.AddDynamic(this, &ALREnemySpawner::OnGameStarted);
		StageGM->OnGameEnding.AddDynamic(this, &ALREnemySpawner::OnGameEnding);
	}
	else
	{
		LR_WARN(TEXT("EnemySpawner(%s): StageGameMode not found — cannot bind OnGameStarted"), *GetName());
	}
}

void ALREnemySpawner::OnGameStarted()
{
	bIsGameStarted = true;
	TryStartSpawning();
}

void ALREnemySpawner::OnGameEnding()
{
	// TEST
	LR_INFO(TEXT("EnemySpawner(%s): 코어 파괴 감지 — 스폰 즉시 정지"), *GetName());
	DeactivateSpawner();
}

void ALREnemySpawner::TryStartSpawning()
{
	if (!bIsDataReady || !bIsGameStarted)
	{
		return;
	}

	StartEnemySpawning();
}

void ALREnemySpawner::StartEnemySpawning()
{
	// 보스 스테이지 처리
	if (bIsBossStage && CachedBossEnemyID != NAME_None)
	{
		SpawnBoss();
	}

	// 스폰 타이머 시작
	const float Rate = FMath::Max(CurrentSpawnInterval, 0.05f);
	if (WaitTime <= 0.0f)
	{
		SpawnEnemy();
		GetWorldTimerManager().SetTimer(SpawnTimerHandle, this, &ALREnemySpawner::SpawnEnemy, Rate, true);
	}
	else
	{
		GetWorldTimerManager().SetTimer(SpawnTimerHandle, this, &ALREnemySpawner::SpawnEnemy, Rate, true, WaitTime);
	}
}

// Called every frame
void ALREnemySpawner::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

bool ALREnemySpawner::InitializeFromStageData()
{
	UGameInstance* GI = GetGameInstance();
	UStageManagerSubsystem* StageMgr = GI ? GI->GetSubsystem<UStageManagerSubsystem>() : nullptr;
	if (!GI || !StageMgr)
	{
		LR_ERROR(TEXT("StageManagerSubsystem not found in spawner"));
		return false;
	}

	CurrentStageID = StageMgr->GetCurrentStageID();
	if (CurrentStageID == NAME_None)
	{
		LR_ERROR(TEXT("EnemySpawner(%s): CurrentStageID is NAME_None. StageManagerSubsystem->LoadStage()가 호출되었는지 확인 필요."), *GetName());
		return false;
	}

	bIsBossStage = StageMgr->IsBossStage();

	const FStageSpawnerData& SpawnerData = StageMgr->GetCurrentStageSpawnerData();

	CachedEnemyIDs.Empty();
	CachedEnemyWeights.Empty();
	for (const FStageSpawnEnemyData& EnemyEntry : SpawnerData.SpawnableEnemies)
	{
		CachedEnemyIDs.Add(EnemyEntry.EnemyID);
		CachedEnemyWeights.Add(EnemyEntry.SpawnWeight);
	}

	CurrentSpawnInterval = SpawnerData.SpawnInterval > -0.1f ? SpawnerData.SpawnInterval : DefaultSpawnInterval;

	CachedBossEnemyID = StageMgr->GetBossEnemyID();

	if (CachedEnemyIDs.Num() <= 0 && CachedBossEnemyID == NAME_None)
	{
		LR_ERROR(TEXT("EnemySpawner(%s): Stage(%s) has no enemy data and no boss"), *GetName(), *CurrentStageID.ToString());
		return false;
	}

	return true;
}

void ALREnemySpawner::OnStageLoaded(FName NewStageID)
{
	if (NewStageID == StageIDToActivate)
	{
		if (!bIsActivated)
		{
			ActivateSpawner();
		}
	}
	else
	{
		if (bIsActivated)
		{
			DeactivateSpawner();
		}
	}
}

FName ALREnemySpawner::PickEnemyIDByWeight() const
{
	if (CachedEnemyIDs.Num() <= 0)
	{
		return NAME_None;
	}

	const float RandomValue = FMath::FRand();
	float Accumulated = 0.0f;

	for (int32 i = 0; i < CachedEnemyIDs.Num(); ++i)
	{
		const float Weight = CachedEnemyWeights.IsValidIndex(i) ? CachedEnemyWeights[i] : (1.0f / CachedEnemyIDs.Num());
		Accumulated += Weight;
		if (RandomValue <= Accumulated)
		{
			return CachedEnemyIDs[i];
		}
	}

	return CachedEnemyIDs.Last();
}

FTransform ALREnemySpawner::MakeRandomSpawnTransform() const
{
	if (!SpawnAreaBox)
	{
		return GetActorTransform();
	}

	const FVector Extent = SpawnAreaBox->GetUnscaledBoxExtent();

	const FVector LocalRandomPoint(
		FMath::FRandRange(-Extent.X, Extent.X),
		FMath::FRandRange(-Extent.Y, Extent.Y),
		FMath::FRandRange(-Extent.Z, Extent.Z));

	const FVector WorldLocation = SpawnAreaBox->GetComponentTransform().TransformPosition(LocalRandomPoint);

	return FTransform(GetActorRotation(), WorldLocation, FVector::OneVector);
}

void ALREnemySpawner::ActivateSpawner()
{
	LR_INFO(TEXT("EnemySpawner(%s): Activating for stage [%s]"), *GetName(), *StageIDToActivate.ToString());

	if (!InitializeFromStageData())
	{
		LR_WARN(TEXT("EnemySpawner(%s): Failed to initialize from stage data"), *GetName());
		return;
	}

	if (!EnemyClass)
	{
		LR_WARN(TEXT("EnemySpawner(%s): EnemyClass is null"), *GetName());
		return;
	}

	bIsActivated = true;

	// 풀 프리웜
	UPoolingSubsystem* PoolSys = GetWorld() ? GetWorld()->GetSubsystem<UPoolingSubsystem>() : nullptr;
	if (PoolSys)
	{
		PoolSys->InitializePool(ALREnemyAIController::StaticClass(), PrewarmCount);
		PoolSys->InitializePool(EnemyClass, PrewarmCount);
	}

	bIsDataReady = true;

	TryStartSpawning();
}

void ALREnemySpawner::DeactivateSpawner()
{
	bIsActivated = false;
	GetWorldTimerManager().ClearTimer(SpawnTimerHandle);

	// TODO: 이 스포너가 스폰한 잔여 에너미 풀 회수 로직, StageManager 또는 GameMode에서 일괄 처리할 수도 있음
	if (ALRStageGameMode* StageGM = Cast<ALRStageGameMode>(GetWorld()->GetAuthGameMode()))
	{
		StageGM->OnGameEnding.RemoveDynamic(this, &ALREnemySpawner::OnGameEnding);
	}
}

void ALREnemySpawner::SpawnBoss()
{
	if (!BossClass)
	{
		LR_WARN(TEXT("EnemySpawner(%s): BossClass is null. Cannot spawn boss."), *GetName());
		return;
	}

	const FTransform SpawnTransform = MakeRandomSpawnTransform();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	ALREnemyBossCharacter* BossEnemy = GetWorld()->SpawnActor<ALREnemyBossCharacter>(BossClass, SpawnTransform, SpawnParams);

	if (!BossEnemy)
	{
		LR_ERROR(TEXT("EnemySpawner(%s): Failed to spawn boss actor"), *GetName());
		return;
	}
	
	if (UAbilitySystemComponent* BossASC = BossEnemy->GetAbilitySystemComponent())
	{
		BossASC->AddLooseGameplayTag(LRTags::Team_Enemy_Structure_Core);
	}

	BossEnemy->InitializeByEnemyID(CachedBossEnemyID);
	BossEnemy->InitializeBossSpeed();
	BossEnemy->RegisterMontageNotifyDelegate();

	if (ALREnemyAIController* EnemyAIC = Cast<ALREnemyAIController>(BossEnemy->GetController()))
	{
		float NewRadius = EnemyAIC->GetAttackRange() + DetectionRangeOffset;
		EnemyAIC->SetDetectionRadius(NewRadius);
		BossEnemy->SetCoreAttackOverlapRadius(NewRadius);
	}

	PlaySpawnVFX(BossEnemy);

	OnBossSpawned.Broadcast(BossEnemy);
}

void ALREnemySpawner::PlaySpawnVFX(ACharacter* SpawnedCharacter)
{
	if (!SpawnVFX || !SpawnedCharacter)
	{
		return;
	}

	// 캡슐 바닥 위치 계산
	const float HalfHeight = SpawnedCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector FootLocation = SpawnedCharacter->GetActorLocation() - FVector(0.0f, 0.0f, HalfHeight);

	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		SpawnVFX,
		FootLocation,
		SpawnedCharacter->GetActorRotation(),
		FVector::OneVector,
		true,
		true
	);
}

void ALREnemySpawner::TestModeAction(bool InIsTest)
{
	if (!InIsTest)
	{
		return;
	}
	else
	{
		if (SpawnCount >= TestSpawnCountLimit)
		{
			DeactivateSpawner();
			Destroy();
		}
		else
		{
			SpawnCount++;
		}
	}
	
}

void ALREnemySpawner::SpawnEnemy()
{
	if (!bIsActivated)
	{
		return;
	}

	// TestAction
	TestModeAction(IsTest);

	// TestModeAction()에서 DeactivateSpawner()/Destroy()가 호출됐을 수 있으니 즉시 중단
	if (!bIsActivated)
	{
		return;
	}

	UPoolingSubsystem* PoolSys = GetWorld() ? GetWorld()->GetSubsystem<UPoolingSubsystem>() : nullptr;
	if (!PoolSys || !EnemyClass)
	{
		LR_ERROR(TEXT("SpawnEnemy failed: PoolSys or EnemyClass is null"));
		return;
	}

	for (int32 i = 0; i < SpawnCountAtOnce; ++i)
	{
		// 1. 스폰할 적 ID 무작위 선택
		const FName TargetEnemyID = PickEnemyIDByWeight();
		if (TargetEnemyID == NAME_None)
		{
			LR_WARN(TEXT("Failed to pick EnemyID By Random!"));
			continue; // 특정 적 스폰에 실패해도 남은 횟수는 계속 진행하도록 continue 사용
		}

		// 2. 무작위 위치 생성 (매 반복마다 새로운 위치 계산)
		FTransform SpawnTransform = MakeRandomSpawnTransform();

		// 3. 풀링 시스템에서 가져오기
		ALREnemyCharacter* NewEnemy = PoolSys->Spawn<ALREnemyCharacter>(EnemyClass, SpawnTransform);

		if (!NewEnemy)
		{
			LR_ERROR(TEXT("EnemySpawner(%s): Failed to spawn enemy from pool (returned null)"), *GetName());
			continue; // 남은 스폰을 위해 continue
		}

		// 4. 데이터 초기화
		NewEnemy->InitializeByEnemyID(TargetEnemyID);

		// 5. 스폰 VFX 재생
		PlaySpawnVFX(NewEnemy);
	}
}

