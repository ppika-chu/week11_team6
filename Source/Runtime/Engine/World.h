#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/MovementComponent.h"
#include "Components/TextRenderComponent.h"
#include "Math/Transform.h"
#include "Render/Renderer.h"
#include "Math/Frustum.h"
#include "Math/BVH.h"
#include "Engine/Scene.h"
#include "Camera/CameraActor.h"
#include "Asset/LOD/StaticMeshLODSelector.h"
#include "Engine/EngineTypes.h"
#include "Containers/Queue.h"
#include "GameTime.h"

class ULevel;
class UActorComponent;
class APlayerCameraManager;
class APlayerController;
class UShapeComponent;
class UBillboardComponent;
class FProjectedShadowInfo;
struct FLODViewContext;

struct FOverlapInfo
{
	uint32 OverlapFirst;
	uint32 OverlapSecond;

	bool operator==(const FOverlapInfo& other) const
	{
		if (this->OverlapFirst == other.OverlapFirst && this->OverlapSecond == other.OverlapSecond)
		{
			return true;
		}
		else
		{
			return false;
		}
	}
};

// Lua가 변경하고 각 View의 ToneMap pass가 읽는 월드 단위 후처리 설정이다.
struct FVignetteSettings
{
	// 렌더링 마스터 스위치다. Intensity가 0이면 활성 상태여도 화면에는 적용되지 않는다.
	bool bEnabled = true;
	float Intensity = 0.0f;
	float Radius = 0.65f;
	float Softness = 0.25f;
	FVector Color = FVector::ZeroVector;

	float GetEffectiveIntensity() const { return bEnabled ? Intensity : 0.0f; }
};

class UWorld : public UObject
{
	DECLARE_CLASS(UWorld, UObject)

public:
	UWorld() = default;
	virtual ~UWorld();

	bool Init();
	/*UPrimitiveComponent* SpawnPrimitive(FClass* Class);*/
	AActor* SpawnActor(UClass* Class, FName InName = NAME_None, const FTransform* Transform = nullptr);

	template<class T>
	T* SpawnActor(FName InName = NAME_None, const FTransform* Transform = nullptr)
	{
		return CastChecked<T>(SpawnActor(T::StaticClass(), InName, Transform));
	}

	void Tick(float DeltaTime);

	void SetTimeScale(float InTimeScale);
	float GetTimeScale() const { return TimeScale; }

	void ResetTimeEffects();
	void SetVignette(float Intensity, float Radius, float Softness, const FVector& Color);
	void SetVignetteEnabled(bool bEnabled) { VignetteSettings.bEnabled = bEnabled; }
	bool IsVignetteEnabled() const { return VignetteSettings.bEnabled; }
	void ResetVignette();
	const FVignetteSettings& GetVignetteSettings() const { return VignetteSettings; }

	const FGameTime& GetGameTime() const { return GameTime; }
	float GetDeltaSeconds() const { return GameTime.GetDeltaWorldTimeSeconds(); }
	float GetUnscaledDeltaTime() const { return GameTime.GetDeltaRealTimeSeconds(); }

	void ClearWorld();

	// 게임·PIE 월드에서 화면을 담당하는 카메라. 씬에 카메라가 없으면 PlayStartView 위치에 하나 스폰한다.
	// 에디터 월드는 View 카메라를 에디터(FEditorViewportClient)가 가지므로 nullptr이다.
	UCameraComponent* GetPlayCameraComponent();
	void ResetPlayCameraComponent(UCameraComponent* Camera);
	// 게임·PIE World의 플레이어. 처음 요청할 때 만들고 bAutoPossessPlayer인 Pawn을 조종하게 한다. 에디터 World는 nullptr이다.
	// UE에서 GameMode가 플레이어 접속 시 PlayerController를 스폰하는 일에 해당한다.
	APlayerController* GetPlayerController();
	// 플레이어의 PlayerCameraManager. 소유자는 PlayerController다.
	APlayerCameraManager* GetPlayerCameraManager();

	// 씬에 카메라가 없을 때 스폰할 기본 카메라의 시점. PIE는 시작 직전에 편집 중이던 View 시점을 넣는다.
	// UE의 FRequestPlaySessionParams::StartLocation/StartRotation에 해당한다.
	void SetPlayStartView(const FMinimalViewInfo& View) { PlayStartView = View; }
	const FMinimalViewInfo& GetPlayStartView() const { return PlayStartView; }

	// Level
	ULevel* GetPersistentLevel() const { return PersistentLevel; }
	void SetPersistentLevel(ULevel* InLevel) { PersistentLevel = InLevel; }

	ULevel* GetCurrentLevel() const { return CurrentLevel; }
	void SetCurrentLevel(ULevel* InLevel) { CurrentLevel = InLevel; }


	int32 GetActorNum();

	bool DestroyActor(AActor* Actor);
	void FlushPendingDestroyActors();

	bool IsInGameTick() const { return bInGameTick; }
	void AddPendingDestroyComponent(UActorComponent* Component);
	void RemovePendingDestroyComponent(UActorComponent* Component);

	// View별 Billboard 행렬 공급자는 이 동기 호출 동안만 사용하며 저장하지 않는다.
	using FBillboardTraceTransform = FMatrix (*)(const UBillboardComponent&, const void*);
	// 현재 World의 Component에 Ray를 전달하고 가장 가까운 유효 교차를 반환한다.
	bool LineTraceSingle(const FRay& WorldRay, FHitResult& OutHit, FBillboardTraceTransform ResolveBillboard = nullptr, const void* ViewContext = nullptr, const AActor* IgnoreActor = nullptr);

	void BeginPlay();
	void EndPlay();

	FScene& GetScene() { return Scene; }
	FTickTaskManager& GetTickTaskManager() { return TickTaskManager; }


	void SetWorldType(EWorldType InType) { WorldType = InType; }
	EWorldType GetWorldType() const { return WorldType; }

	static UWorld* CreateWorld(const EWorldType InWorldType, bool bInformEngineOfWorld, FName WorldName = NAME_None); /*, UPackage* InWorldPackage = NULL, bool bAddToRoot = true, ERHIFeatureLevel::Type InFeatureLevel = ERHIFeatureLevel::Num, const InitializationValues* InIVS = nullptr, bool bInSkipInitWorld = false);*/

	bool IsPlayInEditor() const { return WorldType == EWorldType::PIE; }

	static UWorld* GetDuplicatedWorldForPIE(UWorld* InWorld);

	virtual void PostDuplicate(bool bDuplicateForPIE) override;


	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	void SetScenePath(const FString& InScenePath)
	{
		ScenePath = InScenePath;
	}

	const FString& GetScenePath() const
	{
		return ScenePath;
	}

	FString GetSceneName() const;

	void GatherOverlap();
	void AddOverlap(uint32 OverlapFirst, uint32 OverlapSecond);
	void ResetOverlap();
	UShapeComponent* FindShapeByUUID(uint32 UUID);

private:
	void InitializePlayCamera();
	// 레벨에 배치된 첫 카메라. 없으면 nullptr.
	UCameraComponent* FindLevelCamera() const;
	void ApplyPlayStartView(UCameraComponent& Camera) const;
	void SpawnDefaultPawn();
	void UpdateGameTime(float DeltaTime);

	EWorldType WorldType = EWorldType::None;

	FGameTime GameTime;
	float TimeScale = 1.0f;
	FVignetteSettings VignetteSettings;

	// 등록된 Tick 함수만 실행한다. Actor보다 먼저 사라져도 남은 함수와의 연결을 스스로 끊는다.
	FTickTaskManager TickTaskManager;

	TQueue<AActor*> BeginPlayList;

	bool DestroyActorNow(AActor* Actor);
	void FlushPendingDestroyComponents();
	TArray<AActor*> PendingDestroyActors;
	TArray<UActorComponent*> PendingDestroyComponents;
	bool bInGameTick = false;

	FMinimalViewInfo PlayStartView;
	UCameraComponent* mPlayCameraComponent{};
	APlayerController* mPlayerController{};
	// 지우기 전에 카메라 수정자와 시점을 정리한다. 없으면 만들지 않는다.
	void ResetPlayerCameraManager();


	ULevel* PersistentLevel = nullptr;
	ULevel* CurrentLevel = nullptr;
	TArray<ULevel*> Levels;

	FScene Scene;

	TArray<FOverlapInfo> OverlapInfo;
	TArray<FOverlapInfo> LastFrameOverlapInfo;


	FString ScenePath;
};
