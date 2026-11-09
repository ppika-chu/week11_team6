#include "EnginePCH.h"
#include "PrimitiveComponent.h"
#include "../Render/Renderer.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"


#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/Scene.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Components/ShapeComponent.h"
#include "Collision/CollisionShape.h"

namespace
{
FString PrimitiveTypeToString(EPrimitiveType Type)
{
	switch (Type)
	{
	case EPrimitiveType::Sphere:
		return "Sphere";
		break;
	case EPrimitiveType::Cube:
		return "Cube";
		break;
	case EPrimitiveType::Cone:
		return "Cone";
		break;
	case EPrimitiveType::Plane:
		return "Plane";
		break;
	default:
		return "";
		break;
	}
}
}

UPrimitiveComponent::UPrimitiveComponent()
{
}

UPrimitiveComponent::~UPrimitiveComponent()
{
}

void UPrimitiveComponent::BeginPlay()
{
	Super::BeginPlay();
}


void UPrimitiveComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue)
{
}

bool UPrimitiveComponent::LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit)
{
	const FStaticMeshData* Mesh = GetMeshData();
	return Mesh && TraceMesh(WorldRay, *Mesh, GetWorldMatrix(), OutHit);
}

void UPrimitiveComponent::AddReferencedMaterials(TSet<UMaterial*>& OutMaterials) const
{
	if (SceneProxy)
	{
		SceneProxy->AddReferencedMaterials(OutMaterials);
	}
}

void UPrimitiveComponent::MarkRenderStateDirty()
{
	if (SceneProxy)
	{
		SceneProxy->GetScene()->MarkRenderStateDirty(SceneProxy);
	}
}

// Reflection 편집은 SetVisible을 거치지 않으므로 가시성 캐시 갱신을 직접 예약한다.
void UPrimitiveComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "bVisible" || Property.Name == "bCastShadow" || Property.Name == "bReceiveShadow")
	{
		MarkRenderStateDirty();
	}
}

bool UPrimitiveComponent::LineTraceComponentLocal(const FRay& LocalRay, float& OutT)
{
	const FStaticMeshData* Mesh = GetMeshData();
	return Mesh && TraceMeshLocal(LocalRay, *Mesh, OutT);
}

void UPrimitiveComponent::OnTransformDirty()
{
	if (SceneProxy)
	{
		SceneProxy->GetScene()->MarkDirty(SceneProxy);
	}
}

namespace
{
FBox MakeSweepBounds(const FCollisionShape& Shape, const FVector& Start, const FVector& Delta)
{
	float Reach = Shape.Radius;
	if (Shape.IsBox())
	{
		Reach = Shape.BoxHalfExtent.Length();
	}
	else if (Shape.IsCapsule())
	{
		Reach = Shape.HalfHeight;
	}

	const FVector End = Start + Delta;
	const FVector Min(std::min(Start.X, End.X) - Reach, std::min(Start.Y, End.Y) - Reach, std::min(Start.Z, End.Z) - Reach);
	const FVector Max(std::max(Start.X, End.X) + Reach, std::max(Start.Y, End.Y) + Reach, std::max(Start.Z, End.Z) + Reach);
	return FBox{ Min, Max };
}
}

bool UPrimitiveComponent::MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit)
{
	const FVector Start = GetWorldLocation();
	if (Hit)
	{
		Hit->Init(Start, Start + Delta);
	}

	UShapeComponent* Shape = bSweep && IsBlockComponent() ? Cast<UShapeComponent>(this) : nullptr;
	UWorld* World = GetWorld();
	if (!Shape || !World)
	{
		SetWorldLocationAndRotation(Start + Delta, NewRotation, bSweep);
		return true;
	}

	const FCollisionShape MyShape = Shape->GetCollisionShape();
	const FBox SweepBounds = MakeSweepBounds(MyShape, Start, Delta);

	TArray<Collision::FSweepTarget> Targets;
	TArray<UShapeComponent*> TargetComponents;
	for (AActor* Actor : World->GetPersistentLevel()->GetActors())
	{
		if (!Actor || Actor == GetOwner() || Actor->IsActorBeingDestroyed())
		{
			continue;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			UShapeComponent* Other = Cast<UShapeComponent>(Component);
			if (!Other || !Other->IsBlockComponent() || !SweepBounds.Intersects(Other->CalcBounds()))
			{
				continue;
			}

			Targets.Add({ Other->GetCollisionShape(), Other->GetWorldLocation(), Other->GetWorldRotation().Quaternion() });
			TargetComponents.Add(Other);
		}
	}

	const Collision::FSweepResult Sweep = Collision::Sweep(MyShape, Start, Delta, NewRotation.Quaternion(), Targets);
	const FVector NewLocation = Start + Delta * Sweep.Time;
	SetWorldLocationAndRotation(NewLocation, NewRotation, bSweep);

	if (!Sweep.bBlockingHit)
	{
		return true;
	}

	FHitResult LocalHit;
	if (!Hit)
	{
		Hit = &LocalHit;
	}

	UShapeComponent* HitComponent = TargetComponents[Sweep.TargetIndex];
	Hit->bBlockingHit = true;
	Hit->bStartPenetrating = Sweep.bStartPenetrating;
	Hit->Time = Sweep.Time;
	Hit->Distance = Delta.Length() * Sweep.Time;
	Hit->Location = NewLocation;
	Hit->ImpactPoint = Sweep.Contact.Point;
	Hit->Normal = Sweep.Contact.Normal;
	Hit->ImpactNormal = Sweep.Contact.Normal;
	Hit->PenetrationDepth = Sweep.Contact.PenetrationDepth;
	Hit->HitComponent = HitComponent;
	Hit->HitActor = HitComponent->GetOwner();

	const uint32 ShapeUUID = Shape->GetUUID();
	const uint32 HitComponentUUID = HitComponent->GetUUID();
	// 첫 콜백에서 이동 컴포넌트/Actor가 삭제될 수 있으므로 필요한 값을 먼저 복사한다.
	FHitResult ReversedHit = FHitResult::GetReversedHit(*Hit, this, GetOwner());
	Shape->OnHit(Hit->HitActor, HitComponent, *Hit);
	Shape = World->FindShapeByUUID(ShapeUUID);
	HitComponent = World->FindShapeByUUID(HitComponentUUID);
	if (HitComponent)
	{
		ReversedHit.HitComponent = Shape;
		ReversedHit.HitActor = Shape ? Shape->GetOwner() : nullptr;
		HitComponent->OnHit(ReversedHit.HitActor, Shape, ReversedHit);
	}

	return Sweep.Time > 0.0f;
}

// 기본 Primitive는 기존 공통 Proxy를 사용한다.
FPrimitiveSceneProxy* UPrimitiveComponent::CreateSceneProxy()
{
	return new FPrimitiveSceneProxy(this);
}

void UPrimitiveComponent::MarkRenderDynamicDataDirty()
{
	if (SceneProxy)
	{
		SceneProxy->MarkRenderDynamicDataDirty();
	}
}

void UPrimitiveComponent::CreateRenderState()
{
	Super::CreateRenderState();
	GetWorld()->GetScene().AddPrimitive(this);
}

void UPrimitiveComponent::DestroyRenderState()
{
	GetWorld()->GetScene().RemovePrimitive(this);
	Super::DestroyRenderState();
}

bool UPrimitiveComponent::TraceMesh(const FRay& WorldRay, const FStaticMeshData& Mesh, const FMatrix& WorldMatrix, FHitResult& OutResult)
{
	FBox MeshBox = Mesh.AABB.GetWorldAABB(WorldMatrix);
	float BoxT;
	if (!RayIntersectsAABB(WorldRay, MeshBox.Min, MeshBox.Max, BoxT))
	{
		return false;
	}

	FRay LocalRay = ToLocalRay(WorldRay, WorldMatrix);
	float T = FLT_MAX;

	if (!RayIntersectsMesh(LocalRay, Mesh, T))
	{
		return false;
	}

	OutResult.HitComponent = this;
	OutResult.Distance = T;
	OutResult.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;

	return true;
}


bool UPrimitiveComponent::TraceMeshLocal(const FRay& LocalRay, const FStaticMeshData& Mesh, float& OutT)
{
	return RayIntersectsMesh(LocalRay, Mesh, OutT);
}
