#include "EnginePCH.h"
#include "CameraComponent.h"
#include "../Collision/Ray.h"
#include "Engine/World.h"

bool UCameraComponent::CanSeeActor(const AActor& Target) const
{
	UWorld* World{ GetWorld() };
	if (!World || Target.GetWorld() != World)
	{
		return false;
	}

	FBox Bounds{};
	bool HasBounds{};
	for (UActorComponent* Component : Target.GetComponents())
	{
		const UPrimitiveComponent* Primitive{ Cast<UPrimitiveComponent>(Component) };
		if (!Primitive || !Primitive->IsVisible() || !Primitive->GetMeshData())
		{
			continue;
		}
		const FBox ComponentBounds{ Primitive->CalcBounds() };
		if (HasBounds)
		{
			Bounds.Expand(ComponentBounds);
		}
		else
		{
			Bounds = ComponentBounds;
			HasBounds = true;
		}
	}
	if (!HasBounds)
	{
		return false;
	}

	const AActor* Observer{ GetOwner() };
	while (Observer && Observer->GetAttachParentActor())
	{
		Observer = Observer->GetAttachParentActor();
	}
	const FVector Origin{ GetWorldLocation() };
	const FVector Center{ (Bounds.Min + Bounds.Max) * 0.5f };
	const FVector Extent{ (Bounds.Max - Bounds.Min) * 0.5f };
	const FMatrix ViewProjection{ GetViewProjectionMatrix() };
	const float Samples[]{ 0.0f, -0.9f, 0.9f };
	for (const float Z : Samples)
	{
		for (const float Y : Samples)
		{
			for (const float X : Samples)
			{
				const FVector Point{ Center + FVector{ Extent.X * X, Extent.Y * Y, Extent.Z * Z } };
				const FVector4 Clip{ FVector4{ Point, 1.0f } * ViewProjection };
				if (Clip.W <= 0.0f || Clip.Z < 0.0f || Clip.Z > Clip.W || std::abs(Clip.X) > Clip.W || std::abs(Clip.Y) > Clip.W)
				{
					continue;
				}
				const FVector Delta{ Point - Origin };
				const float Distance{ Delta.Length() };
				if (Distance <= 0.001f)
				{
					return true;
				}
				const FRay Ray{ Origin, Delta / Distance };
				FHitResult Hit{};
				if (!World->LineTraceSingle(Ray, Hit, nullptr, nullptr, Observer) || Hit.Distance >= Distance - 0.01f || (Hit.HitComponent && Hit.HitComponent->GetOwner() == &Target))
				{
					return true;
				}
			}
		}
	}
	return false;
}

void UCameraComponent::GetCameraView(FMinimalViewInfo& OutView) const
{
	OutView.SetLocation(GetWorldLocation());
	OutView.SetRotation(GetWorldRotation());
	OutView.SetFieldOfView(mFieldOfView);
	OutView.SetAspectRatio(mAspectRatio);
	OutView.SetNearZ(mNearClipPlane);
	OutView.SetFarZ(mFarClipPlane);
	OutView.SetIsOrthogonal(mIsOrthogonal);
	OutView.SetOrthoWidth(mOrthoWidth);
}

float UCameraComponent::GetFieldOfView() const
{
	return mFieldOfView;
}

void UCameraComponent::SetFieldOfView(float InFieldOfView)
{
	mFieldOfView = InFieldOfView;
}

float UCameraComponent::GetAspectRatio() const
{
	return mAspectRatio;
}

void UCameraComponent::SetAspectRatio(float InAspectRatio)
{
	mAspectRatio = InAspectRatio;
}

float UCameraComponent::GetNearZ() const
{
	return mNearClipPlane;
}

void UCameraComponent::SetNearZ(float InNearZ)
{
	mNearClipPlane = InNearZ;
}

float UCameraComponent::GetFarZ() const
{
	return mFarClipPlane;
}

void UCameraComponent::SetFarZ(float InFarZ)
{
	mFarClipPlane = InFarZ;
}

bool UCameraComponent::GetIsOrthogonal() const
{
	return mIsOrthogonal;
}

void UCameraComponent::SetIsOrthogonal(bool InIsOrthographic)
{
	mIsOrthogonal = InIsOrthographic;
}

float UCameraComponent::GetOrthoWidth() const
{
	return mOrthoWidth;
}

void UCameraComponent::SetOrthoWidth(float InOrthoWidth)
{
	mOrthoWidth = InOrthoWidth;
}

void UCameraComponent::OnUnregister()
{
	UWorld* World{ GetWorld() };
	if (World)
	{
		World->ResetPlayCameraComponent(this);
	}
	Super::OnUnregister();
}



// 화면 좌표를 투영 모드에 맞는 월드 광선으로 역투영한다.
FRay UCameraComponent::DeProjection(FVector2 MousePos, float ScreenW, float ScreenH)
{
	// 마우스 화면 좌표를 NDC 좌표(-1 ~ 1)로 변환
	const float NDCX{ 2.0f * MousePos.X / ScreenW - 1.0f };
	const float NDCY{ 1.0f - 2.0f * MousePos.Y / ScreenH };


	// Projection / View 행렬의 역행렬
	const FMatrix InvProjection{ GetProjectionMatrix().Inverse() };
	const FMatrix InvView{ GetViewMatrix().Inverse() };


	// NDC 공간의 Near / Far 지점
	FVector4 NearPoint{ NDCX, NDCY, 0.0f, 1.0f };
	FVector4 FarPoint{ NDCX, NDCY, 1.0f, 1.0f };


	// Projection 역변환
	NearPoint = NearPoint * InvProjection;
	FarPoint = FarPoint * InvProjection;


	// Perspective Divide 복원
	NearPoint /= NearPoint.W;
	FarPoint /= FarPoint.W;


	// View 역변환
	NearPoint = NearPoint * InvView;
	FarPoint = FarPoint * InvView;


	// Homogeneous 좌표 정리
	NearPoint /= NearPoint.W;
	FarPoint /= FarPoint.W;

	FRay Ray{};

	if (mIsOrthogonal)
	{
		Ray.Origin = FVector{ NearPoint.X, NearPoint.Y, NearPoint.Z };
	}
	// 원근 투영은 모든 Ray가 카메라 위치에서 시작
	else
	{
		Ray.Origin = GetWorldLocation();
	}

	// 시작점에서 Far 지점을 향하는 방향을 Ray 방향으로 사용
	Ray.Direction = (FVector{ FarPoint.X, FarPoint.Y, FarPoint.Z } - Ray.Origin).Normalized();

	return Ray;
}

// 카메라 Transform의 역변환으로 View 행렬을 만든다.
FMatrix UCameraComponent::GetViewMatrix() const
{
	FMinimalViewInfo View{};
	GetCameraView(View);
	return View.GetViewMatrix();
}

// 시야각·종횡비·클리핑 거리로 원근 행렬을 만든다.
FMatrix UCameraComponent::GetPerspectiveMatrix() const
{
	FMinimalViewInfo View{};
	GetCameraView(View);

	// Forward가 X인 좌표
	return View.GetPerspectiveMatrix();
}

// 직교 폭·종횡비·클리핑 거리로 직교 행렬을 만든다.
FMatrix UCameraComponent::GetOrthogonalMatrix() const
{
	FMinimalViewInfo View{};
	GetCameraView(View);
	return View.GetOrthogonalMatrix();
}

// 투영 모드에 대응하는 행렬을 선택한다.
FMatrix UCameraComponent::GetProjectionMatrix() const
{
	if (mIsOrthogonal)
	{
		return GetOrthogonalMatrix(); // 직교 투영
	}

	return GetPerspectiveMatrix(); // 원근 투영
}

// 행 벡터 규약으로 View와 Projection을 곱한다.
FMatrix UCameraComponent::GetViewProjectionMatrix() const
{
	return GetViewMatrix() * GetProjectionMatrix();
}
