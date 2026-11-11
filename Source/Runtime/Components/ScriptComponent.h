#pragma once
#include "ActorComponent.h"

#include <sol/sol.hpp>

class FLuaVM;
struct FHitResult;

class UScriptComponent : public UActorComponent
{
	DECLARE_CLASS(UScriptComponent, UActorComponent)

	REFLECT_START(ClassName)
		PROPERTY_FLAGS(ScriptPath, CPF_None)
	REFLECT_END()

public:
	virtual ~UScriptComponent() override;

	virtual void InitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime) override;
	virtual void EndPlay() override;

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	void OnOverlap(AActor* OtherActor);
	void OnEndOverlap(AActor* OtherActor);
	void OnHit(AActor* OtherActor, const FHitResult& Hit);
	// Custom AnimInstance가 전달한 이름 이벤트를 Lua의 OnAnimNotify로 호출한다.
	void OnAnimNotify(const FString& NotifyName);

	bool CreateScriptFromTemplate(const std::filesystem::path& NewScriptPath);
	bool LoadScript();
	bool EnsureScriptFile();
	bool SetScriptPath(const FString& NewFileName);

	void BindScriptContext(sol::environment& TargetEnvironment);

	const FString& GetScriptPath() const { return ScriptPath; }

private:
	sol::environment Environment;

	sol::protected_function BeginPlayFunction;
	sol::protected_function TickFunction;
	sol::protected_function EndPlayFunction;
	sol::protected_function OverlapFunction;
	sol::protected_function EndOverlapFunction;
	sol::protected_function HitFunction;
	sol::protected_function AnimNotifyFunction;

	FLuaVM* LuaVM = nullptr;
	sol::state* State = nullptr;

	struct FCollisionBinding
	{
		uint32 ShapeUUID;
		uint32 BeginOverlapHandle;
		uint32 EndOverlapHandle;
		uint32 HitHandle;
	};
	TArray<FCollisionBinding> CollisionBindings;
	// Broadcast의 복사본에 남은 콜백도 EndPlay/소멸 후에는 실행하지 않는다.
	TSharedPtr<bool> CollisionCallbackLifetime;

	void BindCollisionEvents();
	void UnbindCollisionEvents();

	FString ScriptPath;

	std::filesystem::file_time_type LastWriteTime{};
	float HotReloadCheckTimer = 0.0f;

	bool ReloadScript();
	void CheckHotReload(float DeltaTime);
};
