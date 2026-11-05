#include "EnginePCH.h"
#include "Components/SceneComponent.h"

#include "GameFramework/Actor.h"

namespace
{
FQuat GetComponentWorldRotation(const USceneComponent& Component)
{
	const USceneComponent* Parent{ Component.GetAttachParent() };
	const FQuat Rotation{ Component.GetRelativeRotationQuat() };
	return Parent ? GetComponentWorldRotation(*Parent) * Rotation : Rotation;
}
}

USceneComponent::~USceneComponent()
{
	TArray<USceneComponent*> Children = AttachChildren;
	RemoveFromAttachParent();
	for (USceneComponent* Child : Children)
	{
		Child->AttachParent = nullptr;
	}
	AttachChildren.Reset();
}

void USceneComponent::SetRelativeLocationAndRotation(const FVector& NewLocation, const FRotator& NewRotation)
{
	Transform.Location = NewLocation;
	Transform.Rotation = NewRotation;
	MarkTransformDirty();
}

void USceneComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "Transform")
	{
		MarkTransformDirty();
	}
}

void USceneComponent::SetupAttachment(USceneComponent* InParent)
{
	AttachToComponent(InParent, EAttachmentRule::KeepRelative);
}

bool USceneComponent::CanAttachTo(const USceneComponent* InParent) const
{
	if (!InParent || InParent == this)
	{
		return false;
	}

	for (const USceneComponent* Parent = InParent; Parent != nullptr; Parent = Parent->AttachParent)
	{
		if (Parent == this)
		{
			return false;
		}
	}

	const AActor* MyOwner = GetOwner();
	const AActor* ParentOwner = InParent->GetOwner();
	if (!MyOwner || !ParentOwner)
	{
		return true;
	}

	const bool bIsRoot = MyOwner->GetRootComponent() == this;
	if (MyOwner == ParentOwner)
	{
		return !bIsRoot;
	}

	return bIsRoot;
}

bool USceneComponent::AttachToComponent(USceneComponent* InParent, EAttachmentRule Rule)
{
	if (!CanAttachTo(InParent))
	{
		return false;
	}

	if (AttachParent == InParent)
	{
		return true;
	}

	const FMatrix WorldMatrix = GetWorldMatrix();
	RemoveFromAttachParent();
	AttachParent = InParent;
	AttachParent->AttachChildren.Add(this);

	if (Rule == EAttachmentRule::KeepWorld)
	{
		SetWorldTransform(WorldMatrix, false);
	}
	else if (Rule == EAttachmentRule::SnapToTarget)
	{
		SetTransform(FTransform::Identity);
	}

	MarkTransformDirty();
	return true;
}

void USceneComponent::DetachFromComponent(EAttachmentRule Rule)
{
	if (!AttachParent)
	{
		return;
	}

	const FMatrix WorldMatrix = GetWorldMatrix();
	RemoveFromAttachParent();

	if (Rule == EAttachmentRule::KeepWorld)
	{
		SetWorldTransform(WorldMatrix, false);
	}

	MarkTransformDirty();
}

void USceneComponent::RemoveFromAttachParent()
{
	if (!AttachParent)
	{
		return;
	}

	TArray<USceneComponent*>& Siblings = AttachParent->AttachChildren;
	for (uint32 i = 0; i < Siblings.Num(); ++i)
	{
		if (Siblings[i] == this)
		{
			Siblings.RemoveAt(i, 1);
			break;
		}
	}
	AttachParent = nullptr;
}

FRotator USceneComponent::GetWorldRotation() const
{
	if (AttachParent)
	{
		FQuat ParentQuat = AttachParent->GetWorldRotation().Quaternion();
		FQuat LocalQuat = Transform.GetOrientation();

		return (ParentQuat * LocalQuat).ToFRotator();
	}

	return Transform.Rotation;
}

FVector USceneComponent::GetWorldLocation() const
{
	FMatrix WorldMatrix = GetWorldMatrix();

	return FVector(WorldMatrix[3][0], WorldMatrix[3][1], WorldMatrix[3][2]);
}

FVector USceneComponent::GetWorldScale3D() const
{
	if (AttachParent)
	{
		FVector ParentScale = AttachParent->GetWorldScale3D();

		return FVector(
			Transform.Scale.X * ParentScale.X,
			Transform.Scale.Y * ParentScale.Y,
			Transform.Scale.Z * ParentScale.Z);
	}

	return Transform.Scale;
}

FMatrix USceneComponent::GetWorldMatrix() const
{
	if (!AttachParent)
	{
		return Transform.GetLocalMatrix();
	}

	const FVector WorldLocation{ AttachParent->GetWorldMatrix().TransformPosition(Transform.Location) };
	const FQuat WorldRotation{ GetComponentWorldRotation(*this) };
	const FVector WorldScale{ Transform.Scale * AttachParent->GetWorldScale3D() };
	const float ScaleValues[]{ WorldScale.X, WorldScale.Y, WorldScale.Z };
	FMatrix WorldMatrix{ WorldRotation.ToFMatrix() };
	for (int32 Row{}; Row < 3; ++Row)
	{
		for (int32 Column{}; Column < 3; ++Column)
		{
			WorldMatrix.M[Row][Column] *= ScaleValues[Row];
		}
	}
	WorldMatrix.M[3][0] = WorldLocation.X;
	WorldMatrix.M[3][1] = WorldLocation.Y;
	WorldMatrix.M[3][2] = WorldLocation.Z;
	return WorldMatrix;
}

void USceneComponent::SetWorldTransform(const FMatrix& InWorldMatrix, bool bSweep)
{
	FMatrix LocalMatrix = InWorldMatrix;
	if (AttachParent)
	{
		FMatrix ParentWorldMatrix = AttachParent->GetWorldMatrix();
		LocalMatrix = InWorldMatrix * ParentWorldMatrix.Inverse();
	}

	Transform.Location = FVector(LocalMatrix[3][0], LocalMatrix[3][1], LocalMatrix[3][2]);

	FVector XAxis(LocalMatrix[0][0], LocalMatrix[0][1], LocalMatrix[0][2]);
	FVector YAxis(LocalMatrix[1][0], LocalMatrix[1][1], LocalMatrix[1][2]);
	FVector ZAxis(LocalMatrix[2][0], LocalMatrix[2][1], LocalMatrix[2][2]);

	const float XAxisSize = XAxis.Size();
	const float YAxisSize = YAxis.Size();
	const float ZAxisSize = ZAxis.Size();

	Transform.Scale = FVector(XAxisSize, YAxisSize, ZAxisSize);

	XAxis = XAxis.Normalized();
	YAxis = YAxis.Normalized();
	ZAxis = ZAxis.Normalized();

	FMatrix RotationMatrix = FMatrix::Identity;
	RotationMatrix.SetAxis(0, XAxis);
	RotationMatrix.SetAxis(1, YAxis);
	RotationMatrix.SetAxis(2, ZAxis);

	Transform.Rotation = MatrixToRotator(RotationMatrix);

	MarkTransformDirty();
}

void USceneComponent::SetWorldLocation(FVector NewLocation, bool bSweep)
{
	FVector NewRelLocation = NewLocation;

	if (GetAttachParent() != nullptr)
	{
		NewRelLocation = GetAttachParent()->GetWorldMatrix().Inverse().TransformPosition(NewRelLocation);
	}

	SetRelativeLocation(NewRelLocation);
}

void USceneComponent::SetWorldRotation(FRotator NewRotation, bool bSweep)
{
	FRotator NewRelRotation = NewRotation;

	if (GetAttachParent() != nullptr)
	{
		FQuat NewRelQuat = GetAttachParent()->GetWorldRotation().Quaternion().Inverse() * NewRelRotation.Quaternion();
		NewRelRotation = NewRelQuat.ToFRotator();
	}

	SetRelativeRotation(NewRelRotation);
}

void USceneComponent::SetWorldLocationAndRotation(FVector NewLocation, FRotator NewRotation, bool bSweep)
{
	FVector NewRelLocation = NewLocation;
	FRotator NewRelRotation = NewRotation;

	if (GetAttachParent() != nullptr)
	{
		NewRelLocation = GetAttachParent()->GetWorldMatrix().Inverse().TransformPosition(NewRelLocation);

		FQuat NewRelQuat = GetAttachParent()->GetWorldRotation().Quaternion().Inverse() * NewRelRotation.Quaternion();
		NewRelRotation = NewRelQuat.ToFRotator();
	}

	SetRelativeLocationAndRotation(NewRelLocation, NewRelRotation);
}

void USceneComponent::SetWorldScale3D(FVector NewScale)
{
	FVector NewRelScale = NewScale;

	// GetWorldScale3D가 부모 스케일을 축별로 곱하므로 반대로 나눈다.
	if (AttachParent)
	{
		const FVector ParentScale = AttachParent->GetWorldScale3D();
		NewRelScale.X = ParentScale.X != 0.0f ? NewScale.X / ParentScale.X : 0.0f;
		NewRelScale.Y = ParentScale.Y != 0.0f ? NewScale.Y / ParentScale.Y : 0.0f;
		NewRelScale.Z = ParentScale.Z != 0.0f ? NewScale.Z / ParentScale.Z : 0.0f;
	}

	SetRelativeScale3D(NewRelScale);
}

// Delta와 NewRotation은 월드 기준이다. 부모에 붙어 있으면 Transform.Location은 상대 좌표라 월드 위치에서 출발해야 한다.
bool USceneComponent::MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit)
{
	SetWorldLocationAndRotation(GetWorldLocation() + Delta, NewRotation, bSweep);

	return true;
}

void USceneComponent::MarkTransformDirty()
{
	OnTransformDirty();

	for (USceneComponent* Child : AttachChildren)
	{
		Child->MarkTransformDirty(); // 부모가 움직이면 자식의 월드 행렬도 바뀐다
	}
}

void USceneComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record); // UActorComponent: Owner까지 처리됨

	FArchive& Ar = Record.GetUnderlyingArchive();
	if (Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
	{
		UObject* ParentObj = AttachParent;
		Record << SA_VALUE("AttachParent", ParentObj);
		AttachParent = static_cast<USceneComponent*>(ParentObj);

		int32 Num = AttachChildren.Num();
		FStructuredArchive::FArray ChildArray = Record.EnterArray("AttachChildren", Num);
		if (Ar.IsLoading())
		{
			AttachChildren.SetNum(Num);
		}
		for (int32 i = 0; i < Num; ++i)
		{
			UObject* Obj = AttachChildren[i];
			ChildArray.EnterElement() << Obj;
			AttachChildren[i] = static_cast<USceneComponent*>(Obj);
		}
	}
}
