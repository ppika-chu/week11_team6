#pragma once

#include "UObject/Class.h"

class APlayerCameraManager;
struct FMinimalViewInfo;

class UCameraModifier : public UObject
{
	friend class APlayerCameraManager;

	DECLARE_CLASS(UCameraModifier, UObject)

public:
	UCameraModifier() = default;
	virtual ~UCameraModifier() override = default;
	UCameraModifier(const UCameraModifier& Other) = delete;
	UCameraModifier& operator=(const UCameraModifier& Other) = delete;
	UCameraModifier(UCameraModifier&& Other) = delete;
	UCameraModifier& operator=(UCameraModifier&& Other) = delete;

public:
	virtual void Initialize();
	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutView);
	void EnableModifier();
	void DisableModifier(bool Immediate = false);
	bool IsDisabled() const;
	bool IsPendingDisable() const;
	float GetAlpha() const;
	int32 GetPriority() const;
	void SetPriority(int32 Priority);
	void SetBlendTimes(float BlendInTime, float BlendOutTime);
	APlayerCameraManager* GetCameraOwner() const;

private:
	void UpdateAlpha(float DeltaTime);

private:
	APlayerCameraManager* mCameraOwner{};
	int32 mPriority{ 127 };
	float mAlpha{};
	float mBlendInTime{ 0.2f };
	float mBlendOutTime{ 0.2f };
	bool mEnabled{ true };
};
