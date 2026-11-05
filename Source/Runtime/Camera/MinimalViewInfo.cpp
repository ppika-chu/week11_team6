#include "EnginePCH.h"
#include "Camera/MinimalViewInfo.h"

#include <algorithm>
#include <cmath>

FVector FMinimalViewInfo::GetLocation() const
{
	return mLocation;
}

void FMinimalViewInfo::SetLocation(const FVector& Location)
{
	mLocation = Location;
}

FRotator FMinimalViewInfo::GetRotation() const
{
	return mRotation;
}

void FMinimalViewInfo::SetRotation(const FRotator& Rotation)
{
	mRotation = Rotation;
}

float FMinimalViewInfo::GetFieldOfView() const
{
	return mFieldOfView;
}

void FMinimalViewInfo::SetFieldOfView(float FieldOfView)
{
	mFieldOfView = std::clamp(FieldOfView, 1.0f, 179.0f);
}

float FMinimalViewInfo::GetAspectRatio() const
{
	return mAspectRatio;
}

void FMinimalViewInfo::SetAspectRatio(float AspectRatio)
{
	mAspectRatio = std::max(AspectRatio, 0.001f);
}

float FMinimalViewInfo::GetNearZ() const
{
	return mNearClipPlane;
}

void FMinimalViewInfo::SetNearZ(float NearZ)
{
	mNearClipPlane = std::max(NearZ, 0.001f);
	mFarClipPlane = std::max(mFarClipPlane, mNearClipPlane + 0.001f);
}

float FMinimalViewInfo::GetFarZ() const
{
	return mFarClipPlane;
}

void FMinimalViewInfo::SetFarZ(float FarZ)
{
	mFarClipPlane = std::max(FarZ, mNearClipPlane + 0.001f);
}

bool FMinimalViewInfo::GetIsOrthogonal() const
{
	return mIsOrthogonal;
}

void FMinimalViewInfo::SetIsOrthogonal(bool IsOrthogonal)
{
	mIsOrthogonal = IsOrthogonal;
}

float FMinimalViewInfo::GetOrthoWidth() const
{
	return mOrthoWidth;
}

void FMinimalViewInfo::SetOrthoWidth(float OrthoWidth)
{
	mOrthoWidth = std::max(OrthoWidth, 0.001f);
}

FVector FMinimalViewInfo::GetForwardVector() const
{
	return mRotation.Quaternion().GetForwardVector().Normalized();
}

FMatrix FMinimalViewInfo::GetViewMatrix() const
{
	const FMatrix InverseTranslation{ FMatrix::MakeTranslation(mLocation * -1.0f) };
	const FMatrix InverseRotation{ mRotation.Quaternion().ToFMatrix().GetTransposed() };
	return InverseTranslation * InverseRotation;
}

FMatrix FMinimalViewInfo::GetPerspectiveMatrix() const
{
	const float HalfFOV{ FMath::DegreesToRadians(mFieldOfView) * 0.5f };
	const float YScale{ 1.0f / std::tan(HalfFOV) };
	const float XScale{ YScale / mAspectRatio };
	const float ZScale{ mFarClipPlane / (mFarClipPlane - mNearClipPlane) };
	const float ZOffset{ -mNearClipPlane * mFarClipPlane / (mFarClipPlane - mNearClipPlane) };
	return FMatrix{
		0.0f, 0.0f, ZScale, 1.0f,
		XScale, 0.0f, 0.0f, 0.0f,
		0.0f, YScale, 0.0f, 0.0f,
		0.0f, 0.0f, ZOffset, 0.0f
	};
}

FMatrix FMinimalViewInfo::GetOrthogonalMatrix() const
{
	const float Height{ mOrthoWidth / mAspectRatio };
	const float XScale{ 2.0f / mOrthoWidth };
	const float YScale{ 2.0f / Height };
	const float ZScale{ 1.0f / (mFarClipPlane - mNearClipPlane) };
	const float ZOffset{ -mNearClipPlane / (mFarClipPlane - mNearClipPlane) };
	return FMatrix{
		0.0f, 0.0f, ZScale, 0.0f,
		XScale, 0.0f, 0.0f, 0.0f,
		0.0f, YScale, 0.0f, 0.0f,
		0.0f, 0.0f, ZOffset, 1.0f
	};
}

FMatrix FMinimalViewInfo::GetProjectionMatrix() const
{
	return mIsOrthogonal ? GetOrthogonalMatrix() : GetPerspectiveMatrix();
}

FMatrix FMinimalViewInfo::GetViewProjectionMatrix() const
{
	return GetViewMatrix() * GetProjectionMatrix();
}

void FMinimalViewInfo::BlendViewInfo(const FMinimalViewInfo& Other, float Weight)
{
	const float Alpha{ std::clamp(Weight, 0.0f, 1.0f) };
	if (Alpha <= 0.0f)
	{
		return;
	}
	if (Alpha >= 1.0f)
	{
		*this = Other;
		return;
	}

	const FQuat From{ mRotation.Quaternion() };
	const FQuat To{ Other.mRotation.Quaternion() };
	const float Dot{ From.X * To.X + From.Y * To.Y + From.Z * To.Z + From.W * To.W };
	const float ToWeight{ Dot < 0.0f ? -Alpha : Alpha };
	const float FromWeight{ 1.0f - Alpha };
	const FQuat Rotation{ From.X * FromWeight + To.X * ToWeight, From.Y * FromWeight + To.Y * ToWeight, From.Z * FromWeight + To.Z * ToWeight, From.W * FromWeight + To.W * ToWeight };
	mRotation = Rotation.Normalized().ToFRotator();
	mLocation = mLocation + (Other.mLocation - mLocation) * Alpha;
	mFieldOfView += (Other.mFieldOfView - mFieldOfView) * Alpha;
	mAspectRatio += (Other.mAspectRatio - mAspectRatio) * Alpha;
	mNearClipPlane += (Other.mNearClipPlane - mNearClipPlane) * Alpha;
	mFarClipPlane += (Other.mFarClipPlane - mFarClipPlane) * Alpha;
	mOrthoWidth += (Other.mOrthoWidth - mOrthoWidth) * Alpha;
	if (Alpha >= 0.5f)
	{
		mIsOrthogonal = Other.mIsOrthogonal;
	}
}
