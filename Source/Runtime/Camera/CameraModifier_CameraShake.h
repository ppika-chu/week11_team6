#pragma once

#include <memory>

#include "Camera/CameraModifier.h"
#include "Camera/Shake/CameraShakePattern.h"

class UCameraModifier_CameraShake : public UCameraModifier
{
	DECLARE_CLASS(UCameraModifier_CameraShake, UCameraModifier)

public:
	using Super::Initialize;

public:
	UCameraModifier_CameraShake();
	~UCameraModifier_CameraShake() override = default;
	UCameraModifier_CameraShake(const UCameraModifier_CameraShake& Other) = delete;
	UCameraModifier_CameraShake& operator=(const UCameraModifier_CameraShake& Other) = delete;
	UCameraModifier_CameraShake(UCameraModifier_CameraShake&& Other) = delete;
	UCameraModifier_CameraShake& operator=(UCameraModifier_CameraShake&& Other) = delete;

public:
	void Initialize(std::unique_ptr<FCameraShakePattern> RootPattern, float ShakeScale = 1.0f, ECameraShakePlaySpace PlaySpace = ECameraShakePlaySpace::CameraLocal, const FRotator& UserPlaySpaceRot = FRotator{});
	bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutView) override;

	void SetRootPattern(std::unique_ptr<FCameraShakePattern> RootPattern);
	FCameraShakePattern* GetRootPattern() const;

	void StartShake(float ShakeScale = 1.0f, ECameraShakePlaySpace PlaySpace = ECameraShakePlaySpace::CameraLocal, const FRotator& UserPlaySpaceRot = FRotator{});
	void StopShake(bool Immediately = false);
	bool IsFinished() const;

	float GetShakeScale() const;
	void SetShakeScale(float Scale);

	static void ApplyResult(const FCameraShakeUpdateResult& Result, float Scale, ECameraShakePlaySpace PlaySpace, const FRotator& UserPlaySpaceRot, FMinimalViewInfo& InOutView);
	static float CalcRadialShakeScale(const FVector& Epicenter, const FVector& CameraLocation, float InnerRadius, float OuterRadius, float Falloff = 1.0f);

private:
	std::unique_ptr<FCameraShakePattern> mRootPattern{};
	float mShakeScale{ 1.0f };
	ECameraShakePlaySpace mPlaySpace{ ECameraShakePlaySpace::CameraLocal };
	FRotator mUserPlaySpaceRot{};
};
