#pragma once

#include "UObject/Class.h"
#include "../Components/SceneComponent.h"
#include "../Math/EngineMath.h"
#include "../Math/Rotator.h"
#include "Camera/MinimalViewInfo.h"


class UCameraComponent : public USceneComponent
{
	DECLARE_CLASS(UCameraComponent, USceneComponent)
	REFLECT_START(UCameraComponent)
		PROPERTY(mFieldOfView)
		PROPERTY(mAspectRatio)
		PROPERTY(mNearClipPlane)
		PROPERTY(mFarClipPlane)
		PROPERTY(mIsOrthogonal)
		PROPERTY(mOrthoWidth)
	REFLECT_END()

public:
	UCameraComponent() = default;

	// 시점 데이터만 가진다. 에디터 카메라 조작은 FEditorViewportClient가 맡는다.
	void GetCameraView(FMinimalViewInfo& OutView) const;

	// Camera 행렬 계산
	FMatrix GetViewMatrix() const;
	FMatrix GetPerspectiveMatrix() const;
	FMatrix GetOrthogonalMatrix() const;
	FMatrix GetProjectionMatrix() const;
	FMatrix GetViewProjectionMatrix() const;
	bool CanSeeActor(const AActor& Target) const;

	// Picking
	FRay DeProjection(FVector2 MousePos, float ScreenW, float ScreenH);

	// Get & Set
	float GetFieldOfView() const;
	void SetFieldOfView(float InFieldOfView);

	float GetAspectRatio() const;
	void SetAspectRatio(float InAspectRatio);

	float GetNearZ() const;
	void SetNearZ(float InNearZ);

	float GetFarZ() const;
	void SetFarZ(float InFarZ);

	bool GetIsOrthogonal() const;
	void SetIsOrthogonal(bool InIsOrthographic);

	float GetOrthoWidth() const;
	void SetOrthoWidth(float InOrthoWidth);

private:
	virtual void OnUnregister() override;

private:
	float mFieldOfView{ 60.0f };
	float mAspectRatio{ 16.0f / 9.0f };
	float mNearClipPlane{ 0.1f };
	float mFarClipPlane{ 10000.0f };

	bool mIsOrthogonal{};
	float mOrthoWidth{ 16.0f };
};
