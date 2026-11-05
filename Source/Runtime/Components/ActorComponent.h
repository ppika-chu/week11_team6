#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Engine/EngineBaseTypes.h"
#include "Containers/Set.h"

class AActor;
class UWorld;
class UMaterial;

class UActorComponent : public UObject
{
	DECLARE_CLASS(UActorComponent, UObject)

	REFLECT_START(ClassName)
	REFLECT_END()

public:
	UActorComponent() { PrimaryComponentTick.Target = this; }
	virtual ~UActorComponent() override;

	virtual void InitializeComponent() {};
	virtual void BeginPlay() {};
	virtual void EndPlay() {};
	virtual void TickComponent(float DeltaTime) {
	};
	virtual void OnSiblingComponentDestroyed(UActorComponent* DestroyedComponent) {}
	// 머티리얼을 들고 있는 컴포넌트가 구현한다. 여기 없는 인스턴스는 UMaterial이 수거한다.
	virtual void AddReferencedMaterials(TSet<UMaterial*>& OutMaterials) const {}

	void SetOwner(AActor* InOwner) { Owner = InOwner; }
	AActor* GetOwner() const { return Owner; }
	bool IsBeingDestroyed() const { return bBeingDestroyed; }

	// 디테일 패널에서 이 컴포넌트를 통째로 숨긴다 (에디터 시각화용 빌보드 등)
	void SetHiddenInDetails(bool bInHidden) { bHiddenInDetails = bInHidden; }
	bool IsHiddenInDetails() const { return bHiddenInDetails; }

	// 액터 클래스가 만들지 않고 나중에 추가된 컴포넌트. 클래스가 만든 컴포넌트는 씬을 불러올 때
	// 이름으로 찾으므로, 이름을 바꾸면 찾지 못해 같은 컴포넌트가 하나 더 생긴다.
	void MarkUserCreated() { bUserCreated = true; }
	bool IsUserCreated() const { return bUserCreated; }

	// UE와 같이 기본값은 bCanEverTick = false. Tick이 필요한 컴포넌트만 생성자에서 켠다.
	FActorComponentTickFunction PrimaryComponentTick;

	virtual void SetActive(bool bNewActive);
	bool IsActive() const { return bIsActive; }

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record);

	void RegisterComponent();
	void RegisterComponentWithWorld(UWorld* InWorld);
	void UnregisterComponent();

	bool IsRegistered() const { return bRegistered; }
	bool IsRenderStateCreated() const { return bRenderStateCreated; }

	UWorld* GetWorld() const { return WorldPrivate; }

protected:
	virtual void OnRegister();
	virtual void OnUnregister();
	virtual bool ShouldCreateRenderState() const
	{
		return false;
	}

	// 원래는 _Concurrent 병렬처리 -> 현재 엔진에서는 멀티 스레드 사용 X
	virtual void CreateRenderState();
	virtual void DestroyRenderState();

	void ExecuteRegisterEvents();
	void ExecuteUnregisterEvents();

private:
	friend class AActor;

	AActor* Owner = nullptr;
	bool bBeingDestroyed = false;
	bool bHiddenInDetails = false;
	bool bUserCreated = false;
	bool bIsActive = true;
	bool bRegistered = false;

	UWorld* WorldPrivate = nullptr;
	bool bRenderStateCreated = false;
};