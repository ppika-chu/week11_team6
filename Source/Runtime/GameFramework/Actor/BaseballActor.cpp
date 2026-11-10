#include "EnginePCH.h"
#include "BaseballActor.h"

#include "Asset/AssetManager.h"
#include "Components/ProjectileMovementComponent.h"
#include "Components/ScriptComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"

ABaseballActor::ABaseballActor()
{
	constexpr float BallRadius = 0.08f;
	constexpr float LaunchSpeed = 15.0f;

	// Sweep 충돌은 Root인 Sphere가 담당하고, 자식 Mesh는 외형만 담당한다.
	USphereComponent* Collision = CreateDefaultSubobject<USphereComponent>("CollisionComponent");
	SetRootComponent(Collision);
	Collision->SetSphereRadius(BallRadius);
	Collision->SetBlockComponent(true);
	Collision->SetDrawOnlyIfSelected(true);

	UStaticMeshComponent* Mesh = CreateDefaultSubobject<UStaticMeshComponent>("BallMesh");
	Mesh->SetupAttachment(Collision);
	Mesh->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Sphere"));
	Mesh->SetRelativeScale3D(FVector(BallRadius, BallRadius, BallRadius));

	// 생성 회전의 로컬 +X로 발사하고, 벽에 맞으면 약하게 튕긴 뒤 중력으로 떨어진다.
	UProjectileMovementComponent* Movement = CreateDefaultSubobject<UProjectileMovementComponent>("ProjectileMovement");
	Movement->Velocity = FVector(LaunchSpeed, 0.0f, 0.0f);
	Movement->bInitialVelocityInLocalSpace = true;
	Movement->ProjectileGravityScale = 1.0f;
	Movement->bShouldBounce = true;
	Movement->Bounciness = 0.2f;
	Movement->Friction = 0.3f;

	UScriptComponent* Lifetime = CreateDefaultSubobject<UScriptComponent>("LifetimeScript");
	Lifetime->SetScriptPath("Assets/Scripts/Samples/BaseballLifetime.lua");
}
