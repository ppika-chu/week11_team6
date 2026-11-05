#pragma once

#include <type_traits>
#include <utility>

#include "Camera/CameraModifier.h"
#include "Camera/MinimalViewInfo.h"
#include "GameFramework/Actor.h"

class UCameraComponent;

class APlayerCameraManager : public AActor
{
	friend class UWorld;
	friend class APlayerController;

	DECLARE_CLASS(APlayerCameraManager, AActor)

public:
	using Super::Serialize;

public:
	APlayerCameraManager() = default;
	~APlayerCameraManager() override;
	APlayerCameraManager(const APlayerCameraManager& Other) = delete;
	APlayerCameraManager& operator=(const APlayerCameraManager& Other) = delete;
	APlayerCameraManager(APlayerCameraManager&& Other) = delete;
	APlayerCameraManager& operator=(APlayerCameraManager&& Other) = delete;

public:
	void Serialize(FStructuredArchive::FRecord Record) override;
	void PostDuplicate(bool DuplicateForPIE) override;

	bool SetViewTarget(AActor* Target);
	bool SetViewTargetWithBlend(AActor* Target, float BlendTime);
	bool SetViewTargetCamera(UCameraComponent* Camera, float BlendTime = 0.0f);

	AActor* GetViewTarget() const;

	UCameraComponent* GetViewTargetCamera() const;
	void ResetViewTarget(UCameraComponent* Camera);
	void UpdateCamera(float DeltaTime);
	bool HasValidCameraView() const;

	const FMinimalViewInfo& GetCameraView() const;
	const TArray<UCameraModifier*>& GetCameraModifiers() const;

	UCameraModifier* AddCameraModifier(UClass* ModifierClass);

	template<typename T, typename... Args>
		requires std::is_base_of_v<UCameraModifier, T>
	T* AddCameraModifier(Args&&... Arguments);

	UCameraModifier* FindCameraModifierByClass(const UClass* ModifierClass) const;

	bool RemoveCameraModifier(UCameraModifier* Modifier);
	void RemoveAllCameraModifiers();

private:
	static UCameraComponent* FindCameraComponent(AActor* Target);
	static bool CompareModifierPriority(const UCameraModifier* Left, const UCameraModifier* Right);

	UCameraModifier* CreateCameraModifier(UClass* ModifierClass);
	void SerializeViewTarget(FStructuredArchive::FRecord Record);
	void ResolveViewTarget();
	void ResetRuntimeState();
	void ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& InOutView);
	void FlushRemovedModifiers();

private:
	UCameraComponent* mViewTargetCamera{};
	FString mPendingViewTargetActorName{};
	FString mPendingViewTargetCameraName{};
	FMinimalViewInfo mBaseCameraView{};
	FMinimalViewInfo mCameraView{};
	FMinimalViewInfo mBlendSourceView{};

	float mBlendDuration{};
	float mBlendElapsed{};
	bool mHasValidCameraView{};
	bool mApplyingModifiers{};

	TArray<UCameraModifier*> mModifiers{};
	TArray<UCameraModifier*> mRemovedModifiers{};
};

template<typename T, typename... Args>
	requires std::is_base_of_v<UCameraModifier, T>
T* APlayerCameraManager::AddCameraModifier(Args&&... Arguments)
{
	T* Modifier{ static_cast<T*>(CreateCameraModifier(T::StaticClass())) };
	if (!Modifier)
	{
		return nullptr;
	}
	Modifier->Initialize(std::forward<Args>(Arguments)...);
	mModifiers.Add(Modifier);
	return Modifier;
}
