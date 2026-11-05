#pragma once

#include "Math/EngineMath.h"

struct FMinimalViewInfo
{
public:
	FMinimalViewInfo() = default;

public:
	FVector GetLocation() const;
	void SetLocation(const FVector& Location);
	FRotator GetRotation() const;
	void SetRotation(const FRotator& Rotation);
	float GetFieldOfView() const;
	void SetFieldOfView(float FieldOfView);
	float GetAspectRatio() const;
	void SetAspectRatio(float AspectRatio);
	float GetNearZ() const;
	void SetNearZ(float NearZ);
	float GetFarZ() const;
	void SetFarZ(float FarZ);
	bool GetIsOrthogonal() const;
	void SetIsOrthogonal(bool IsOrthogonal);
	float GetOrthoWidth() const;
	void SetOrthoWidth(float OrthoWidth);
	FVector GetForwardVector() const;
	FMatrix GetViewMatrix() const;
	FMatrix GetPerspectiveMatrix() const;
	FMatrix GetOrthogonalMatrix() const;
	FMatrix GetProjectionMatrix() const;
	FMatrix GetViewProjectionMatrix() const;
	void BlendViewInfo(const FMinimalViewInfo& Other, float Weight);

private:
	FVector mLocation{};
	FRotator mRotation{};
	float mFieldOfView{ 60.0f };
	float mAspectRatio{ 16.0f / 9.0f };
	float mNearClipPlane{ 0.1f };
	float mFarClipPlane{ 10000.0f };
	bool mIsOrthogonal{};
	float mOrthoWidth{ 16.0f };
};
