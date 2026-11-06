#pragma once

#include "../Math/Transform.h"
#include "ActorComponent.h"
#include "Math/Box.h"
#include "Collision/HitResult.h"

enum class EAttachmentRule
{
	KeepRelative,
	KeepWorld,
	SnapToTarget
};

class USceneComponent : public UActorComponent
{
	DECLARE_CLASS(USceneComponent, UActorComponent)

	REFLECT_START(ClassName)
		PROPERTY(Transform)
	REFLECT_END()

public:
	USceneComponent() = default;
	virtual ~USceneComponent() override;

	virtual void PostEditChangeProperty(const FProperty& Property) override;

	// Get & Set
	const FVector& GetRelativeLocation() const { return Transform.Location; }
	void SetRelativeLocation(const FVector& InLocation)
	{
		Transform.Location = InLocation;
		MarkTransformDirty();
	}


	FRotator GetRelativeRotation() const { return Transform.Rotation.ToFRotator(); }
	void SetRelativeRotation(const FRotator& InRotation)
	{
		Transform.Rotation = InRotation;
		MarkTransformDirty();
	}
	void SetRelativeLocationAndRotation(const FVector& NewLocation, const FRotator& NewRotation);
	const FVector& GetRelativeScale3D() const { return Transform.Scale; }
	void SetRelativeScale3D(const FVector& InScale)
	{
		Transform.Scale = InScale;
		MarkTransformDirty();
	}

	// 쿼터니언 적용된 회전행렬
	FQuat GetRelativeRotationQuat() const { return Transform.GetOrientation(); }

	const FTransform& GetTransform() const { return Transform; }
	void SetTransform(const FTransform& InTransform)
	{
		Transform = InTransform;
		MarkTransformDirty();
	}

	// Attatch-To
	USceneComponent* GetAttachParent() const { return AttachParent; }
	const TArray<USceneComponent*>& GetAttachChildren() const { return AttachChildren; }
	void SetupAttachment(USceneComponent* InParent);
	bool CanAttachTo(const USceneComponent* InParent) const;
	bool AttachToComponent(USceneComponent* InParent, EAttachmentRule Rule);
	void DetachFromComponent(EAttachmentRule Rule = EAttachmentRule::KeepRelative);

	virtual FBox CalcLocalBounds() const { return FBox{ FVector(), FVector() }; }
	FBox CalcBounds() const { return CalcLocalBounds().GetWorldAABB(GetWorldMatrix()); }

	FVector GetWorldLocation() const;
	FRotator GetWorldRotation() const;
	FVector GetWorldScale3D() const;
	FMatrix GetWorldMatrix() const;

	void SetWorldLocation(FVector NewLocation, bool bSweep);
	void SetWorldRotation(FRotator NewRotation, bool bSweep);
	void SetWorldLocationAndRotation(FVector NewLocation, FRotator NewRotation, bool bSweep);
	void SetWorldScale3D(FVector NewScale);
	void SetWorldTransform(const FMatrix& InWorldMatrix, bool bSweep);

	virtual bool MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit = nullptr);

	void MarkTransformDirty();
	virtual void OnTransformDirty() {};

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

public:
	FVector ComponentVelocity;

protected:
	void RemoveFromAttachParent();

	FTransform Transform;

	USceneComponent* AttachParent = nullptr; // Attach 부모 정보
	TArray<USceneComponent*> AttachChildren;
};