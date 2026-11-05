#include "EnginePCH.h"
#include "Camera/CameraModifier_CameraShake.h"
#include "Camera/PlayerCameraManager.h"

#include <cmath>

UCameraModifier_CameraShake::UCameraModifier_CameraShake()
{
	SetBlendTimes(0.0f, 0.0f);
}

void UCameraModifier_CameraShake::Initialize(std::unique_ptr<FCameraShakePattern> RootPattern, float ShakeScale, ECameraShakePlaySpace PlaySpace, const FRotator& UserPlaySpaceRot)
{
	SetRootPattern(std::move(RootPattern));
	StartShake(ShakeScale, PlaySpace, UserPlaySpaceRot);
}

bool UCameraModifier_CameraShake::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutView)
{
	if (mRootPattern && !mRootPattern->IsFinished())
	{
		FCameraShakeUpdateParams Params{};
		Params.DeltaTime = DeltaTime;
		Params.POV = InOutView;

		FCameraShakeUpdateResult Result{};
		mRootPattern->UpdateShakePattern(Params, Result);
		ApplyResult(Result, mShakeScale, mPlaySpace, mUserPlaySpaceRot, InOutView);
	}
	if (IsFinished())
	{
		if (APlayerCameraManager * CameraOwner{ GetCameraOwner() })
		{
			CameraOwner->RemoveCameraModifier(this);
		}
	}
	return false;
}

void UCameraModifier_CameraShake::SetRootPattern(std::unique_ptr<FCameraShakePattern> RootPattern)
{
	mRootPattern = std::move(RootPattern);
}

FCameraShakePattern* UCameraModifier_CameraShake::GetRootPattern() const
{
	return mRootPattern.get();
}

void UCameraModifier_CameraShake::StartShake(float ShakeScale, ECameraShakePlaySpace PlaySpace, const FRotator& UserPlaySpaceRot)
{
	mShakeScale = ShakeScale;
	mPlaySpace = PlaySpace;
	mUserPlaySpaceRot = UserPlaySpaceRot;
	if (mRootPattern)
	{
		mRootPattern->StartShakePattern();
	}
	EnableModifier();
}

void UCameraModifier_CameraShake::StopShake(bool Immediately)
{
	if (mRootPattern)
	{
		mRootPattern->StopShakePattern(Immediately);
	}
}

bool UCameraModifier_CameraShake::IsFinished() const
{
	return !mRootPattern || mRootPattern->IsFinished();
}

float UCameraModifier_CameraShake::GetShakeScale() const
{
	return mShakeScale;
}

void UCameraModifier_CameraShake::SetShakeScale(float Scale)
{
	mShakeScale = Scale;
}

void UCameraModifier_CameraShake::ApplyResult(const FCameraShakeUpdateResult& InResult, float Scale, ECameraShakePlaySpace PlaySpace, const FRotator& UserPlaySpaceRot, FMinimalViewInfo& InOutView)
{
	if (Scale == 0.0f)
	{
		return;
	}

	FCameraShakeUpdateResult Result{ InResult };
	Result.ApplyScale(Scale);
	const FQuat CameraQuat{ InOutView.GetRotation().Quaternion() };
	const FQuat ShakeQuat{ Result.Rotation.Quaternion() };

	switch (PlaySpace)
	{
	case ECameraShakePlaySpace::CameraLocal:
		InOutView.SetLocation(InOutView.GetLocation() + CameraQuat.RotateVector(Result.Location));
		InOutView.SetRotation((CameraQuat * ShakeQuat).ToFRotator());
		break;
	case ECameraShakePlaySpace::World:
		InOutView.SetLocation(InOutView.GetLocation() + Result.Location);
		InOutView.SetRotation((ShakeQuat * CameraQuat).ToFRotator());
		break;
	case ECameraShakePlaySpace::UserDefined:
	{
		const FQuat UserQuat{ UserPlaySpaceRot.Quaternion() };
		InOutView.SetLocation(InOutView.GetLocation() + UserQuat.RotateVector(Result.Location));
		const FQuat WorldShake{ UserQuat * ShakeQuat * UserQuat.Inverse() };
		InOutView.SetRotation((WorldShake * CameraQuat).ToFRotator());
		break;
	}
	}
	InOutView.SetFieldOfView(InOutView.GetFieldOfView() + Result.FOV);
}

float UCameraModifier_CameraShake::CalcRadialShakeScale(const FVector& Epicenter, const FVector& CameraLocation, float InnerRadius, float OuterRadius, float Falloff)
{
	if (OuterRadius <= InnerRadius)
	{
		return 1.0f;
	}
	const float Distance{ FVector::Distance(Epicenter, CameraLocation) };
	const float DistancePct{ FMath::Clamp((Distance - InnerRadius) / (OuterRadius - InnerRadius), 0.0f, 1.0f) };
	return std::pow(1.0f - DistancePct, Falloff);
}
