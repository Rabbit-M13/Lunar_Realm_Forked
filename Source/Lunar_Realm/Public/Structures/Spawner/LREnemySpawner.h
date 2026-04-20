// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/LRDataStructs.h"
#include "GameFramework/Character.h"
#include "NiagaraSystem.h"
#include "LREnemySpawner.generated.h"

class UBoxComponent;
class ALREnemyCharacter;
class ALREnemyBossCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBossSpawned, ALREnemyBossCharacter*, BossCharacter);

/**
 * 에너미 스포너 클래스
 * - GameDataSubsystem 에서 받아온 키값들 멤버로 저장
 * - 스테이지 정보 받아와서 해당하는 Enemy 후보 선택
 * - 스폰은 오브젝트 풀링 시스템 사용
 */
 //============================================================================
 // (260205) KWB 제작. 제반 사항 구현.
 // (260208) Stage 데이터 드리븐, 가중치 설정, 보스 스테이지 필터링.
 // (260210) KWB 키값 타입 int32 -> FName 으로 변경 반영
 // (260219) KWB 스폰 인터벌 버그 픽스, 주석 수정
 // (260317) KWB SpawnManger 통한 데이터 초기화로 리팩토링 / 용이한 테스트 위해 잠시 원복
 // (260318) KWB 보스 스폰 로직 추가 및 불필요 멤버 삭제, 예외 처리 로직 변경(보스 스테이지면 에너미 데이터 null 허용)
 // (260322) KWB LRStageGameMode 통해서 스포너 시작 로직으로 수정
 // (260326) KWB 뷰포트에 표시된 스포너 크기와 실제 스폰 범위 다른 문제 수정
 // (260327) KWB 에너미 등장 시 VFX 재생 기능 추가
 //============================================================================
UCLASS()
class LUNAR_REALM_API ALREnemySpawner : public AActor
{
	GENERATED_BODY()
	
public:
	// Sets default values for this actor's properties
	ALREnemySpawner();

	// 보스 스폰 완료 시 발화 (SpawnBoss 내부 InitializeByEnemyID 이후)
	UPROPERTY(BlueprintAssignable, Category = "LR|Spawner|Boss")
	FOnBossSpawned OnBossSpawned;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// GameMode의 OnGameStarted 델리게이트 수신 핸들러
	UFUNCTION()
	void OnGameStarted();

	// 코어 파괴 시 즉시 스폰 정지
	UFUNCTION()
	void OnGameEnding();

	// 데이터 준비 + 게임 시작 신호 둘 다 충족 시 스폰 시작
	void TryStartSpawning();

	//실제 스폰 타이머 시작 + 보스 스폰
	void StartEnemySpawning();

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	FORCEINLINE FName GetStageIDToActivate() const { return StageIDToActivate; }

	UFUNCTION(BlueprintCallable)
	void SpawnEnemy();

protected:
	UFUNCTION(BlueprintCallable)
	bool InitializeFromStageData();

	UFUNCTION()
	void OnStageLoaded(FName NewStageID);

	FName PickEnemyIDByWeight() const;
	FTransform MakeRandomSpawnTransform() const;

	void ActivateSpawner();
	void DeactivateSpawner();

	void SpawnBoss();

	void PlaySpawnVFX(ACharacter* SpawnedCharacter);

	// IsTest = true 일때만 동작
	void TestModeAction(bool InIsTest);

protected:
	UPROPERTY(EditAnywhere, Category = "LR|TEST")
	bool IsTest;

	UPROPERTY(EditAnywhere, Category = "LR|TEST")
	int TestSpawnCountLimit = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LR|Spawner")
	TSubclassOf<ALREnemyCharacter> EnemyClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LR|Spawner|Boss")
	TSubclassOf<ALREnemyBossCharacter> BossClass;

	// TEST: 사전 생성 오브젝트 풀 - 추후 개수 변경 필요 (50 ~ 100)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LR|Spawner")
	int32 PrewarmCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LR|Spawner", meta = (ClampMin = "0.1"))
	float WaitTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LR|Spawner", meta = (ClampMin = "1"))
	int32 SpawnCountAtOnce = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LR|Spawner")
	float DefaultSpawnInterval = 5.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LR|Spawner")
	TObjectPtr<UBoxComponent> SpawnAreaBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LR|Spawner")
	TArray<FName> CachedEnemyIDs;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LR|Spawner")
	TArray<float> CachedEnemyWeights;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LR|Spawner|Boss")
	FName CachedBossEnemyID;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LR|Spawner")
	FName CurrentStageID;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LR|Spawner")
	bool bIsBossStage = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LR|Spawner")
	float CurrentSpawnInterval = 1.0f;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "LR|Spawner|StageID")
	FName StageIDToActivate;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LR|Spawner")
	bool bIsActivated = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LR|Spawner|VFX")
	TObjectPtr<UNiagaraSystem> SpawnVFX;

	FTimerHandle SpawnTimerHandle;

	// 스테이지 데이터 초기화 완료 여부
	bool bIsDataReady = false;

	// GameMode로부터 게임 시작 신호 수신 여부
	bool bIsGameStarted = false;

private:
	// 보스 스폰 시 탐지 거리 조정을 위한 오프셋
	float DetectionRangeOffset = 100.f;

	// 테스트 액션을 위한 스폰 횟수 카운트
	int SpawnCount = 0;	
};
