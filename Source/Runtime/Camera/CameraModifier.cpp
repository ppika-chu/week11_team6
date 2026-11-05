#include "EnginePCH.h"
#include "Camera/CameraModifier.h"

#include <algorithm>

void UCameraModifier::Initialize()
{
}

bool UCameraModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutView)
{
	return false;
}

void UCameraModifier::EnableModifier()
{
	mEnabled = true;
	if (mBlendInTime <= 0.0f)
	{
		mAlpha = 1.0f;
	}
}

void UCameraModifier::DisableModifier(bool Immediate)
{
	mEnabled = false;
	if (Immediate || mBlendOutTime <= 0.0f)
	{
		mAlpha = 0.0f;
	}
}

bool UCameraModifier::IsDisabled() const
{
	return !mEnabled && mAlpha <= 0.0f;
}

bool UCameraModifier::IsPendingDisable() const
{
	return !mEnabled && mAlpha > 0.0f;
}

float UCameraModifier::GetAlpha() const
{
	return mAlpha;
}

int32 UCameraModifier::GetPriority() const
{
	return mPriority;
}

void UCameraModifier::SetPriority(int32 Priority)
{
	mPriority = Priority;
}

void UCameraModifier::SetBlendTimes(float BlendInTime, float BlendOutTime)
{
	mBlendInTime = std::max(BlendInTime, 0.0f);
	mBlendOutTime = std::max(BlendOutTime, 0.0f);
	if (mEnabled && mBlendInTime <= 0.0f)
	{
		mAlpha = 1.0f;
	}
	else if (!mEnabled && mBlendOutTime <= 0.0f)
	{
		mAlpha = 0.0f;
	}
}

APlayerCameraManager* UCameraModifier::GetCameraOwner() const
{
	return mCameraOwner;
}

void UCameraModifier::UpdateAlpha(float DeltaTime)
{
	const float BlendTime{ mEnabled ? mBlendInTime : mBlendOutTime };
	if (BlendTime <= 0.0f)
	{
		mAlpha = mEnabled ? 1.0f : 0.0f;
		return;
	}
	const float Change{ std::max(DeltaTime, 0.0f) / BlendTime };
	mAlpha = std::clamp(mAlpha + (mEnabled ? Change : -Change), 0.0f, 1.0f);
}
