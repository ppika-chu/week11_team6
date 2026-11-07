#include "EnginePCH.h"
#include "World.h"
#include "Level.h"
#include "Engine/Engine.h"

#include "Core/EngineStatics.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor/StaticMeshActor.h"

#include "GameFramework/Actor/DefaultPawn.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Input/InputSystem.h"

#include "UObject/UObjectIterator.h"

#include "Collision/Ray.h"
#include "Components/BillboardComponent.h"
#include "Components/ExponentialHeightFogComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Components/ShapeComponent.h"
#include "Asset/LOD/StaticMeshLODSelector.h"

#include "Math/Frustum.h"
#include "Render/ProjectedShadowInfo.h"
#include "Engine/DirectionalLightSceneProxy.h"
#include <utility>

#include "Core/Stats/LightweightStats.h"
#include "Core/Stats/EditorStats.h"
#include "Core/Async/TaskPool.h"

#include "UObject/UObjectGlobals.h"
#include "Core/Stats/ShadowStats.h"

#include "Render/ShadowSettings.h"
#include <algorithm>
#include <cmath>
#include <limits>

DECLARE_CYCLE_STAT("Actor Tick", STAT_ActorTick);                      // Actor 틱 측정
DECLARE_CYCLE_STAT("Update All Transforms", STAT_UpdateAllTransforms); // 각 Transform의 Update 시간 측정

// Level은 World가 소유한다.
// 액터를 먼저 정리해야 지워진 컴포넌트를 가리키는 프록시·Tick 등록이 남지 않는다.
UWorld::~UWorld()
{
	if (PersistentLevel)
	{
		ClearWorld();
	}

	for (ULevel* Level : Levels)
	{
		delete Level;
	}
	Levels.Reset();
	PersistentLevel = nullptr;
	CurrentLevel = nullptr;
}

void UWorld::SetVignette(float Intensity, float Radius, float Softness, const FVector& Color)
{
	VignetteSettings.Intensity = std::clamp(Intensity, 0.0f, 1.0f);
	VignetteSettings.Radius = std::max(Radius, 0.0f);
	VignetteSettings.Softness = std::max(Softness, 0.001f);
	VignetteSettings.Color = FVector{
		std::clamp(Color.X, 0.0f, 1.0f),
		std::clamp(Color.Y, 0.0f, 1.0f),
		std::clamp(Color.Z, 0.0f, 1.0f)
	};
}

void UWorld::ResetVignette()
{
	// 연출 파라미터만 초기화하고 Settings 패널의 마스터 스위치 상태는 유지한다.
	const bool bWasEnabled = VignetteSettings.bEnabled;
	VignetteSettings = FVignetteSettings{};
	VignetteSettings.bEnabled = bWasEnabled;
}

bool UWorld::Init()
{
	// Spawn Actor로 카메라 생성하고 세팅하기
	PersistentLevel = NewObject<ULevel>(this);

	if (!PersistentLevel)
	{
		HTR_LOG(Error, "Failed to create PersistentLevel");
		return false;
	}

	// 레벨 연결
	PersistentLevel->SetWorld(this);
	Levels.Add(PersistentLevel);
	CurrentLevel = PersistentLevel;

	// 기존 기본 카메라 위치. PIE는 시작할 때 편집 중인 View 시점으로 덮어쓴다.
	PlayStartView.SetLocation(FVector(-5.0f, -5.0f, 5.0f));

	return true;
}

AActor* UWorld::SpawnActor(UClass* Class, FName InName, const FTransform* Transform)
{
	if (!Class)
	{
		return nullptr;
	}
	if (!Class->IsChildOf(AActor::StaticClass()))
	{
		return nullptr;
	}

	// 현재 편집 중인 Level에 넣는다. UE의 SpawnActor도 별도 지정이 없으면 CurrentLevel을 쓴다.
	ULevel* SpawnLevel = CurrentLevel ? CurrentLevel : PersistentLevel;

	// 1. ObjectFactory로 Actor 생성
	AActor* NewActor = NewObject<AActor>(SpawnLevel, Class, InName);

	if (!NewActor)
	{
		HTR_LOG(Error, "SpawnActor : Failed to create Actor");
		return nullptr;
	}

	// 2. Actor를 Level에 연결한다. World는 Level을 통해 찾는다.
	NewActor->Level = SpawnLevel;

	// 3. Transform 적용
	const FTransform SpawnTransform = Transform ? *Transform : FTransform::Identity;

	if (!NewActor->GetRootComponent())
	{
		NewActor->CreateDefaultSceneRoot();
	}

	if (NewActor->GetRootComponent())
	{
		NewActor->GetRootComponent()->SetTransform(SpawnTransform);
	}

	NewActor->RegisterAllComponents();

	// 4. Level->Actors에 등록
	SpawnLevel->AddActor(NewActor);

	// 프리뷰 월드는 BeginPlay가 없으므로 스폰할 때 Tick을 등록한다. (UE UActorComponent::RegisterComponentWithWorld)
	if (WorldType == EWorldType::EditorPreview)
	{
		NewActor->RegisterAllActorTickFunctions(true);
		return NewActor;
	}

	// 5. PlayList에 추가
	BeginPlayList.Enqueue(NewActor);

	return NewActor;
}

void UWorld::Tick(float DeltaTime)
{
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateAllTransforms);
		Scene.UpdateAllTransforms();
	}

	if (WorldType == EWorldType::EditorPreview)
	{
		// 게임 시작 처리(BeginPlay), Lua, 충돌 없이 등록된 Tick 함수만 실행한다.
		bInGameTick = true;
		TickTaskManager.RunAllTickGroups(DeltaTime);
		FlushPendingDestroyComponents();
		bInGameTick = false;

		FlushPendingDestroyActors();
		return;
	}

	// 에디터 월드면 더이상 틱을 돌리지 않는다.
	if (WorldType != EWorldType::Editor)
	{
		// 입력받은 실제 DeltaTime으로 이번 프레임의 FGameTime을 만든 뒤,
		// 기존 Tick 경로에는 Time Dilation이 적용된 월드 DeltaTime을 전달한다.
		UpdateGameTime(DeltaTime);
		DeltaTime = GameTime.GetDeltaWorldTimeSeconds();

		bInGameTick = true;
		while (!BeginPlayList.IsEmpty())
		{
			BeginPlayList.Peek()->BeginPlay();
			BeginPlayList.Dequeue();
		}

		// HUD는 프레임마다 스크립트가 새로 채운다. 그리기는 이 Tick이 끝난 뒤 엔진이 한다.
		if (APlayerController* PlayerController = GetPlayerController())
		{
			PlayerController->GetHUD().Reset();
		}

		// 액터 수와 관계없이 게임 월드 프레임마다 한 번만 갱신한다.
		if (FLuaVM* LuaVM = GEngine ? GEngine->GetLuaVM() : nullptr)
		{
			LuaVM->UpdateCoroutine(DeltaTime);
		}

		{
			SCOPE_CYCLE_COUNTER(STAT_ActorTick);
			// 모든 Actor를 도는 대신 등록된 Tick 함수(메인 카메라 포함)만 실행한다.
			TickTaskManager.RunAllTickGroups(DeltaTime);
		}

		GatherOverlap(); // 게임 중에만 충돌처리
		// 소유 액터가 지워지기 전에 처리한다. 삭제 중 나온 액터 삭제 요청은 아래 Flush로 미뤄지도록 플래그를 나중에 내린다.
		FlushPendingDestroyComponents();
		bInGameTick = false;

		FlushPendingDestroyActors();

		if (WorldType == EWorldType::PIE || WorldType == EWorldType::Game)
		{
			APlayerCameraManager* CameraManager{ GetPlayerCameraManager() };
			if (CameraManager)
			{
				CameraManager->UpdateCamera(DeltaTime);
			}
		}
	}
}

void UWorld::SetTimeScale(float InTimeScale)
{
	if (!std::isfinite(InTimeScale))
	{
		HTR_LOG(Warning, "[Time] Ignored non-finite time scale");
		return;
	}

	TimeScale = std::clamp(InTimeScale, 0.0f, 10.0f);
}

void UWorld::ResetTimeEffects()
{
	TimeScale = 1.0f;
}

void UWorld::UpdateGameTime(float DeltaTime)
{
	const float DeltaRealTimeSeconds{ std::isfinite(DeltaTime) ? std::max(0.0f, DeltaTime) : 0.0f };

	const float DeltaWorldTimeSeconds{ DeltaRealTimeSeconds * TimeScale };

	GameTime.DeltaRealTimeSeconds = DeltaRealTimeSeconds;
	GameTime.DeltaWorldTimeSeconds = DeltaWorldTimeSeconds;
	GameTime.RealTimeSeconds += static_cast<double>(DeltaRealTimeSeconds);
	GameTime.WorldTimeSeconds += static_cast<double>(DeltaWorldTimeSeconds);
	GameTime.TimeDilation = TimeScale;
}

void UWorld::ResetPlayerCameraManager()
{
	APlayerCameraManager* CameraManager{ mPlayerController ? mPlayerController->PlayerCameraManager : nullptr };
	if (CameraManager)
	{
		CameraManager->RemoveAllCameraModifiers();
		CameraManager->SetViewTarget(nullptr);
	}
}

void UWorld::ClearWorld()
{
	ResetPlayerCameraManager();
	mPlayerController = nullptr;
	mPlayCameraComponent = nullptr;

	// BeginPlay 대기 중인 Actor 제거
	while (!BeginPlayList.IsEmpty())
	{
		BeginPlayList.Dequeue();
	}
	PendingDestroyActors.Reset();
	PendingDestroyComponents.Reset();


	for (ULevel* Level : Levels)
	{
		for (AActor* Actor : Level->Actors)
		{
			if (!Actor)
			{
				continue;
			}
			Actor->UnregisterAllComponents();
			Actor->RegisterAllActorTickFunctions(false);
		}
		Level->ClearActors();
	}
	// 액터를 지우기 전에 렌더 프록시와 틱 등록부터 푼다. ClearActors는 액터를 delete만 하므로,
	// 그대로 두면 지워진 컴포넌트를 가리키는 프록시가 FScene에 남아 다음 프레임에 터진다.
	Scene.RemoveAllLights();
	Scene.RemoveAllPrimitives();
	Scene.RemoveAllFogInfo();
	Scene.RemoveAllDecals();
	HTR_LOG(Info, "{} : ", PersistentLevel->GetActorNum());
}

UCameraComponent* UWorld::GetPlayCameraComponent()
{
	if (WorldType != EWorldType::PIE && WorldType != EWorldType::Game)
	{
		return nullptr;
	}
	if (!mPlayCameraComponent)
	{
		InitializePlayCamera();
	}
	return mPlayCameraComponent;
}

void UWorld::ResetPlayCameraComponent(UCameraComponent* Camera)
{
	if (APlayerCameraManager * CameraManager{ mPlayerController ? mPlayerController->PlayerCameraManager : nullptr })
	{
		CameraManager->ResetViewTarget(Camera);
	}
	if (mPlayCameraComponent == Camera)
	{
		mPlayCameraComponent = nullptr;
	}
}

APlayerController* UWorld::GetPlayerController()
{
	if (WorldType != EWorldType::PIE && WorldType != EWorldType::Game)
	{
		return nullptr;
	}
	if (mPlayerController && mPlayerController->IsActorBeingDestroyed())
	{
		mPlayerController = nullptr;
	}
	if (!mPlayerController)
	{
		for (AActor* Actor : PersistentLevel->GetActors())
		{
			if (APlayerController* Controller{ Cast<APlayerController>(Actor) }; Controller && !Controller->IsActorBeingDestroyed())
			{
				mPlayerController = Controller;
				break;
			}
		}
	}
	if (!mPlayerController)
	{
		mPlayerController = SpawnActor<APlayerController>();
		if (!mPlayerController)
		{
			return nullptr;
		}
		mPlayerController->SetFlags(EObjectFlags::RF_Transient);

		for (AActor* Actor : PersistentLevel->GetActors())
		{
			APawn* Pawn{ Cast<APawn>(Actor) };
			if (Pawn && Pawn->bAutoPossessPlayer && !Pawn->GetController() && !Pawn->IsActorBeingDestroyed())
			{
				mPlayerController->Possess(Pawn);
				break;
			}
		}

		// 조종할 것도 볼 카메라도 없는 씬은 PIE에서 날아다니며 볼 수 있게 한다.
		// 배치된 카메라로 보여 주는 씬과 패키징된 게임의 동작은 바꾸지 않는다.
		if (WorldType == EWorldType::PIE && !mPlayerController->GetPawn() && !mPlayCameraComponent && !FindLevelCamera())
		{
			SpawnDefaultPawn();
		}
	}
	return mPlayerController;
}

APlayerCameraManager* UWorld::GetPlayerCameraManager()
{
	APlayerController* PlayerController{ GetPlayerController() };
	return PlayerController ? PlayerController->GetPlayerCameraManager() : nullptr;
}

void UWorld::InitializePlayCamera()
{
	mPlayCameraComponent = FindLevelCamera();

	if (!mPlayCameraComponent)
	{
		const FTransform CameraTransform{ PlayStartView.GetRotation(), PlayStartView.GetLocation() };
		ACameraActor* CameraActor{ Cast<ACameraActor>(SpawnActor(ACameraActor::StaticClass(), NAME_None, &CameraTransform)) };
		if (CameraActor)
		{
			// 실행 중 만든 카메라는 씬에 저장하지 않는다.
			CameraActor->SetFlags(EObjectFlags::RF_Transient);
			mPlayCameraComponent = CameraActor->GetCameraComponent();
			ApplyPlayStartView(*mPlayCameraComponent);
		}
	}
}

UCameraComponent* UWorld::FindLevelCamera() const
{
	for (ULevel* Level : Levels)
	{
		if (!Level)
		{
			continue;
		}

		for (AActor* Actor : Level->GetActors())
		{
			if (!Actor)
			{
				continue;
			}

			for (UActorComponent* Component : Actor->GetComponents())
			{
				UCameraComponent* Camera{ Cast<UCameraComponent>(Component) };
				if (Camera && Camera->IsRegistered())
				{
					return Camera;
				}
			}
		}
	}
	return nullptr;
}

void UWorld::ApplyPlayStartView(UCameraComponent& Camera) const
{
	Camera.SetFieldOfView(PlayStartView.GetFieldOfView());
	Camera.SetAspectRatio(PlayStartView.GetAspectRatio());
	Camera.SetNearZ(PlayStartView.GetNearZ());
	Camera.SetFarZ(PlayStartView.GetFarZ());
	Camera.SetIsOrthogonal(PlayStartView.GetIsOrthogonal());
	Camera.SetOrthoWidth(PlayStartView.GetOrthoWidth());
}

void UWorld::SpawnDefaultPawn()
{
	const FTransform PawnTransform{ PlayStartView.GetRotation(), PlayStartView.GetLocation() };
	ADefaultPawn* Pawn{ Cast<ADefaultPawn>(SpawnActor(ADefaultPawn::StaticClass(), NAME_None, &PawnTransform)) };
	if (Pawn)
	{
		// 실행 중 만든 Pawn은 씬에 저장하지 않는다.
		Pawn->SetFlags(EObjectFlags::RF_Transient);
		ApplyPlayStartView(*Pawn->GetCameraComponent());
		mPlayerController->Possess(Pawn);
	}
}

int32 UWorld::GetActorNum()
{
	return PersistentLevel->GetActorNum();
}

bool UWorld::DestroyActor(AActor* Actor)
{
	if (!Actor || !Actor->GetLevel())
	{
		return false;
	}

	if (Actor->IsActorBeingDestroyed())
	{
		return true;
	}

	Actor->bActorIsBeingDestroyed = true;

	if (bInGameTick)
	{
		PendingDestroyActors.Add(Actor);
		return true;
	}

	return DestroyActorNow(Actor);
}

void UWorld::FlushPendingDestroyActors()
{
	const TArray<AActor*> Actors = PendingDestroyActors;
	PendingDestroyActors.Reset();

	for (AActor* Actor : Actors)
	{
		DestroyActorNow(Actor);
	}
}

void UWorld::AddPendingDestroyComponent(UActorComponent* Component)
{
	PendingDestroyComponents.Add(Component);
}

void UWorld::RemovePendingDestroyComponent(UActorComponent* Component)
{
	const int32 Index = PendingDestroyComponents.Find(Component);
	if (Index != -1)
	{
		PendingDestroyComponents.RemoveAt(Index);
	}
}

void UWorld::FlushPendingDestroyComponents()
{
	// 삭제 중에 새 요청이 들어올 수 있고 숨겨진 자식은 목록에서 빠지므로, 뒤에서 하나씩 꺼낸다.
	while (!PendingDestroyComponents.IsEmpty())
	{
		UActorComponent* Component = PendingDestroyComponents[PendingDestroyComponents.Num() - 1];
		PendingDestroyComponents.RemoveLast();

		// 소유 액터도 지워지는 중이면 액터 삭제가 컴포넌트까지 정리한다.
		AActor* Owner = Component->GetOwner();
		if (!Owner->IsActorBeingDestroyed())
		{
			Owner->FinishPendingDestroyComponent(Component);
		}
	}
}

bool UWorld::DestroyActorNow(AActor* Actor)
{
	if (GEngine)
	{
		GEngine->OnActorContentDestroying(Actor);
	}

	ULevel* Level{ Actor->GetLevel() };
	if (Actor == mPlayerController)
	{
		mPlayerController = nullptr;
	}
	else if (mPlayerController)
	{
		mPlayerController->ClearPlayerCameraManager(Cast<APlayerCameraManager>(Actor));
	}

	// 게임 중이던 액터면 EndPlay부터 알린다. 이 시점엔 아직 월드와 컴포넌트에 접근할 수 있다.
	Actor->EndPlay();

	TArray<AActor*> AttachedActors;
	Actor->GetAttachedActors(AttachedActors);
	for (AActor* AttachedActor : AttachedActors)
	{
		AttachedActor->DetachFromActor(EAttachmentRule::KeepWorld);
	}

	// 1. BeginPlay 대기열에서 제거
	TQueue<AActor*> NewBeginPlayList;

	while (!BeginPlayList.IsEmpty())
	{
		AActor* PendingActor = BeginPlayList.Peek();
		BeginPlayList.Dequeue();

		if (PendingActor != Actor)
		{
			NewBeginPlayList.Enqueue(PendingActor);
		}
	}

	BeginPlayList = std::move(NewBeginPlayList);

	// 4. Level의 Actors에서 제거
	for (int32 i = Level->Actors.Num() - 1; i >= 0; --i)
	{
		if (Level->Actors[i] == Actor)
		{
			Level->Actors.RemoveAt(i, 1);
			break;
		}
	}

	FString ActorName = Actor->GetName();
	uint32 ActorUUID = Actor->GetUUID();

	// 5. 프록시 제거
	Actor->UnregisterAllComponents();

	Actor->RegisterAllActorTickFunctions(false);

	// 6. Actor 삭제
	delete Actor;

	HTR_LOG(Info, "Destroy Actor : {} UUID {}", ActorName, ActorUUID);

	return true;
}

// 다른 World의 객체를 제외하고 Component 교차 중 최근접 결과를 선택한다.
bool UWorld::LineTraceSingle(const FRay& WorldRay, FHitResult& OutHit, FBillboardTraceTransform ResolveBillboard, const void* ViewContext, const AActor* IgnoreActor)
{
	SCOPE_CYCLE_COUNTER_ALWAYS(EditorStats::STAT_PickingTime_Name);
	OutHit = FHitResult();
	float NearestT = std::numeric_limits<float>::max();

	const auto TraceComponent = [&](FPrimitiveSceneProxy* Proxy, float& InOutNearestT)
	{
		const AActor* Owner{ Proxy && Proxy->GetComponent() ? Proxy->GetComponent()->GetOwner() : nullptr };
		while (Owner && IgnoreActor)
		{
			if (Owner == IgnoreActor)
			{
				return false;
			}
			Owner = Owner->GetAttachParentActor();
		}

		if (UStaticMesh* Mesh = Proxy ? Proxy->GetMesh() : nullptr)
		{
			if (!Proxy->IsVisible())
			{
				return false;
			}

			const FMatrix& WorldToLocal = Proxy->GetWorldToLocal();
			const FRay LocalRay{
				.Origin = WorldToLocal.TransformPosition(WorldRay.Origin),
				.Direction = WorldToLocal.TransformVector(WorldRay.Direction)
			};

			float T = InOutNearestT;
			if (!RayIntersectsMesh(LocalRay, Mesh->GetMeshData(), T))
			{
				return false;
			}

			OutHit.HitComponent = Proxy->GetComponent();
			OutHit.Distance = T;
			OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
			InOutNearestT = T;
			return true;
		}

		UPrimitiveComponent* Component = Proxy ? Proxy->GetComponent() : nullptr;

		if (!Component || !Component->IsVisible())
		{
			return false;
		}

		if (UBillboardComponent* Billboard = Cast<UBillboardComponent>(Component))
		{
			if (!ResolveBillboard)
			{
				FHitResult Hit;
				if (!Billboard->LineTraceComponent(WorldRay, Hit) ||
					Hit.Distance >= InOutNearestT)
				{
					return false;
				}

				OutHit = Hit;
				InOutNearestT = Hit.Distance;
				return true;
			}

			const FMatrix BillboardToWorld = ResolveBillboard(*Billboard, ViewContext);

			const FRay LocalRay = ToLocalRay(WorldRay, BillboardToWorld);

			float T = InOutNearestT;
			if (!Billboard->LineTraceComponentLocal(LocalRay, T))
			{
				return false;
			}

			OutHit.HitComponent = Billboard;
			OutHit.Distance = T;
			OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
			InOutNearestT = T;
			return true;
		}

		const FMatrix& WorldToLocal = Proxy->GetWorldToLocal();
		const FRay LocalRay{
			.Origin = WorldToLocal.TransformPosition(WorldRay.Origin),
			.Direction = WorldToLocal.TransformVector(WorldRay.Direction)
		};

		float T = InOutNearestT;
		if (!Component->LineTraceComponentLocal(LocalRay, T))
		{
			return false;
		}

		OutHit.HitComponent = Component;
		OutHit.Distance = T;
		OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
		InOutNearestT = T;
		return true;
	};

	const FPreparedRay PreparedRay(WorldRay);

	Scene.BVH.TraceClosest(
		[&](const FBox& Bounds, float& OutEnterT)
		{ return RayIntersectsAABB(PreparedRay, Bounds.Min, Bounds.Max, OutEnterT); },
		[&](FPrimitiveSceneProxy* Proxy, float& OutNearestT)
		{ return TraceComponent(Proxy, OutNearestT); },
		NearestT);

	return OutHit.HitComponent != nullptr;
}

void UWorld::BeginPlay()
{
}

void UWorld::EndPlay()
{
	for (ULevel* Level : Levels)
	{
		// EndPlay에서 액터가 지워질 수 있으므로 목록을 복사해서 돈다.
		const TArray<AActor*> Actors = Level->GetActors();
		for (AActor* Actor : Actors)
		{
			if (Actor)
			{
				Actor->EndPlay();
			}
		}
	}

	if (GEngine && GEngine->GetAudioSystem())
	{
		GEngine->GetAudioSystem()->StopAll();
	}
	// 현재 단일 게임 월드의 대기가 다음 Play까지 남지 않도록 정리한다.

	if (WorldType != EWorldType::Editor && WorldType != EWorldType::EditorPreview)
	{
		if (FLuaVM* LuaVM = GEngine ? GEngine->GetLuaVM() : nullptr)
		{
			LuaVM->ResetCoroutines();
		}
	}

	// 장면 전환이나 PIE 재시작에 이전 장면의 슬로모가 남지 않게 한다.
	ResetTimeEffects();
	ResetVignette();
	GameTime = FGameTime{};

	ResetPlayerCameraManager();
}

UWorld* UWorld::CreateWorld(const EWorldType InWorldType, bool bInformEngineOfWorld, FName WorldName)
{
	UWorld* NewWorld = NewObject<UWorld>(nullptr, WorldName);
	NewWorld->SetFlags(EObjectFlags::RF_Transactional);
	NewWorld->WorldType = InWorldType;
	NewWorld->Init();

	return NewWorld;
}

UWorld* UWorld::GetDuplicatedWorldForPIE(UWorld* InWorld)
{
	if (!InWorld)
	{
		return nullptr;
	}

	UWorld* PIEWorld = CreateWorld(EWorldType::PIE, false);

	PIEWorld->SetScenePath(InWorld->GetScenePath());

	FObjectDuplicationParameters Params(InWorld, nullptr);
	Params.DuplicationSeed.Add(InWorld, PIEWorld);
	Params.DuplicationSeed.Add(InWorld->GetPersistentLevel(), PIEWorld->GetPersistentLevel());
	Params.PortFlags = EPropertyPortFlags::PPF_DuplicateForPIE;

	StaticDuplicateObjectEx(Params);
	return PIEWorld;
}

void UWorld::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);


	if (!bDuplicateForPIE)
	{
		assert(PersistentLevel);

		// Update the persistent level's owning world. This is needed for some initialization
		if (!PersistentLevel->OwningWorld)
		{
			PersistentLevel->OwningWorld = this;
		}
	}

	for (AActor* Actor : PersistentLevel->GetActors())
	{
		if (!Actor)
		{
			continue;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (USceneComponent* SceneComp = Cast<USceneComponent>(Component))
			{
				SceneComp->MarkTransformDirty();
			}
		}

		Actor->RegisterAllComponents();

		BeginPlayList.Enqueue(Actor); // Tick 등록은 BeginPlay에서 함
	}

	Scene.BuildBVH();
}

void UWorld::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record); // "Properties"

	FArchive& Ar{ Record.GetUnderlyingArchive() };
	PersistentLevel->Serialize(Record.EnterRecord("PersistentLevel"));

	if (Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
	{
		UObject* PlayerController{ mPlayerController };
		Record << SA_VALUE("PlayerController", PlayerController);
		if (Ar.IsLoading())
		{
			mPlayerController = Cast<APlayerController>(PlayerController);
		}
		return;
	}

	if (Ar.IsLoading())
	{
		for (AActor* Actor : PersistentLevel->GetActors())
		{
			if (APlayerCameraManager * CameraManager{ Cast<APlayerCameraManager>(Actor) })
			{
				CameraManager->ResolveViewTarget();
			}
		}
	}

	// 실행 중 만든 PlayerCameraManager의 상태는 PlayerController 대신 World가 씬에 기록한다.
	APlayerCameraManager* SavedCameraManager{ Ar.IsSaving() && mPlayerController ? mPlayerController->PlayerCameraManager : nullptr };
	if (TOptional<FStructuredArchive::FSlot> CameraManagerSlot{ Record.TryEnterField("PlayerCameraManager", SavedCameraManager && SavedCameraManager->HasAnyFlags(EObjectFlags::RF_Transient)) })
	{
		FStructuredArchive::FRecord CameraManagerRecord{ CameraManagerSlot.GetValue().EnterRecord() };
		FString ClassName{ SavedCameraManager ? SavedCameraManager->GetClass()->Name : FString{} };
		FString ManagerName{ SavedCameraManager ? SavedCameraManager->GetName() : FString{} };
		CameraManagerRecord << SA_VALUE("Class", ClassName) << SA_VALUE("Name", ManagerName);
		if (Ar.IsLoading())
		{
			UClass* ManagerClass{ FindClass(ClassName) };
			if (!ManagerClass || !ManagerClass->IsChildOf(APlayerCameraManager::StaticClass()) || !ManagerClass->Constructor)
			{
				Ar.SetError();
				return;
			}
			APlayerController* PlayerController{ GetPlayerController() };
			if (!PlayerController)
			{
				Ar.SetError();
				return;
			}
			if (PlayerController->PlayerCameraManager)
			{
				DestroyActor(PlayerController->PlayerCameraManager);
			}
			SavedCameraManager = Cast<APlayerCameraManager>(SpawnActor(ManagerClass, FName{ ManagerName }));
			if (!SavedCameraManager)
			{
				Ar.SetError();
				return;
			}
			SavedCameraManager->SetFlags(EObjectFlags::RF_Transient);
			PlayerController->SetPlayerCameraManager(SavedCameraManager);
		}
		SavedCameraManager->Serialize(CameraManagerRecord);
	}
}

void UWorld::GatherOverlap()
{
	LastFrameOverlapInfo = OverlapInfo; // 이전 FrameOverlapInfo에 OverlapInfo 저장
	ResetOverlap();                     // 현재 FrameOverlapInfo를 리셋
	TArray<UShapeComponent*> Shapes;
	for (ULevel* Level : Levels)
	{
		if (!Level)
		{
			continue;
		}
		for (AActor* Actor : Level->GetActors())
		{
			if (!Actor || Actor->IsActorBeingDestroyed())
			{
				continue;
			}
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (UShapeComponent* Shape = Cast<UShapeComponent>(Component))
				{
					if (Shape->GetGenerateOverlapEvents())
					{
						Shapes.Add(Shape); // Shape 판정을 위해 월드에 존재하는 전체 Shape배열에 추가
					}
				}
			}
		}
	}

	for (int i = 0; i < Shapes.Num(); ++i)
	{
		for (int j = i + 1; j < Shapes.Num(); ++j)
		{
			UShapeComponent* First = Shapes[i];
			UShapeComponent* Second = Shapes[j];
			// IsOverlapping 내부에서 AABB 검사 후 정밀 판정을 수행한다.
			if (First->IsOverlapping(*Second))
			{
				// 프레임마다 같은 쌍을 유지하도록 작은 UUID부터 저장, 전체 충돌 쌍 저장
				AddOverlap(std::min(First->GetUUID(), Second->GetUUID()),
					std::max(First->GetUUID(), Second->GetUUID()));
			}
		}
	}


	for (int i = 0; i < OverlapInfo.Num(); i++)
	{
		if (LastFrameOverlapInfo.Find(OverlapInfo[i]) == -1)
		{
			const FOverlapInfo Pair = OverlapInfo[i];
			UShapeComponent* First = FindShapeByUUID(Pair.OverlapFirst);
			UShapeComponent* Second = FindShapeByUUID(Pair.OverlapSecond);
			if (First)
			{
				First->OnBeginOverlap(Second);
			}
			// 첫 Lua 콜백에서 어느 쪽 Actor든 삭제할 수 있다.
			First = FindShapeByUUID(Pair.OverlapFirst);
			Second = FindShapeByUUID(Pair.OverlapSecond);
			if (Second)
			{
				Second->OnBeginOverlap(First);
			}
		}
	}

	for (int i = 0; i < LastFrameOverlapInfo.Num(); i++)
	{
		if (OverlapInfo.Find(LastFrameOverlapInfo[i]) == -1)
		{
			const FOverlapInfo Pair = LastFrameOverlapInfo[i];
			UShapeComponent* First = FindShapeByUUID(Pair.OverlapFirst);
			UShapeComponent* Second = FindShapeByUUID(Pair.OverlapSecond);
			if (First)
			{
				First->OnEndOverlap(Second);
			}
			First = FindShapeByUUID(Pair.OverlapFirst);
			Second = FindShapeByUUID(Pair.OverlapSecond);
			if (Second)
			{
				Second->OnEndOverlap(First);
			}
		}
	}
}

void UWorld::AddOverlap(uint32 OverlapFirst, uint32 OverlapSecond)
{
	FOverlapInfo OverlapInf;
	OverlapInf.OverlapFirst = OverlapFirst;
	OverlapInf.OverlapSecond = OverlapSecond;
	OverlapInfo.Add(OverlapInf);
}

void UWorld::ResetOverlap()
{
	OverlapInfo.Reset();
}

UShapeComponent* UWorld::FindShapeByUUID(uint32 UUID)
{
	// 렌더 프록시가 없는 Shape도 찾고, 비활성화된 컴포넌트에도 EndOverlap을 전달한다.
	for (ULevel* Level : Levels)
	{
		if (!Level)
		{
			continue;
		}
		for (AActor* Actor : Level->GetActors())
		{
			if (!Actor)
			{
				continue;
			}
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (UShapeComponent* Shape = Cast<UShapeComponent>(Component))
				{
					if (Shape->GetUUID() == UUID)
					{
						return Shape;
					}
				}
			}
		}
	}
	return nullptr;
}

FString UWorld::GetSceneName() const
{
	if (ScenePath.empty())
	{
		return "Untitled";
	}

	return std::filesystem::path(ScenePath)
		.stem()
		.string();
}
