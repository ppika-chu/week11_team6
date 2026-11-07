#include "EnginePCH.h"
#include "Components/ParticleSubUVComponent.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"

#include "Serialization/TypeSerializer.h"

#include "Engine/World.h"
#include "Engine/ParticleSceneProxy.h"

#include <algorithm>
#include <cmath>

const float UParticleSubUVComponent::MAX_NORMALIZED_VALUE = 1.0f;

UParticleSubUVComponent::UParticleSubUVComponent()
{
	// 파티클 시뮬레이션을 매 프레임 진행한다.
	PrimaryComponentTick.bCanEverTick = true;

	QuadMesh = UAssetManager::GetAssetByPath<UStaticMesh>("ParticleQuad");
	Material = UAssetManager::GetAssetByPath<UMaterial>("SubUVMaterial");

	ColSize = 8;
	RowSize = 8;
	FrameRate = 12.0f;
}

// 파티클 렌더 스냅샷은 Scene이 관리하는 전용 Proxy가 소유한다.
FPrimitiveSceneProxy* UParticleSubUVComponent::CreateSceneProxy()
{
	return new FParticleSceneProxy(this);
}

FBox UParticleSubUVComponent::CalcLocalBounds() const
{
	if (!QuadMesh)
	{
		return Super::CalcLocalBounds();
	}

	const FBox& Quad = QuadMesh->GetMeshData().AABB;
	const FVector Scale = GetWorldScale3D();
	const float HalfWidth = std::max(std::fabs(Quad.Min.Y), std::fabs(Quad.Max.Y)) * std::fabs(Scale.Y);
	const float HalfHeight = std::max(std::fabs(Quad.Min.Z), std::fabs(Quad.Max.Z)) * std::fabs(Scale.Z);
	const float Radius = std::sqrt(HalfWidth * HalfWidth + HalfHeight * HalfHeight);

	// 프록시가 이 상자에 컴포넌트 월드 행렬(스케일 포함)을 곱하므로 축마다 스케일을 미리 나눈다.
	constexpr float MinScale = 1.0e-4f;
	const FVector Half(
		Radius / std::max(std::fabs(Scale.X), MinScale),
		Radius / std::max(std::fabs(Scale.Y), MinScale),
		Radius / std::max(std::fabs(Scale.Z), MinScale));
	return FBox{ Half * -1.0f, Half };
}

bool UParticleSubUVComponent::LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit)
{
	if (!QuadMesh)
	{
		return false;
	}

	UWorld* World = GetOwner() ? GetOwner()->GetWorld() : nullptr;
	const FMatrix FacingMatrix = MakePlayCameraFacingMatrix(World, GetWorldRotation(), GetWorldLocation(), GetWorldScale3D());
	return TraceMesh(WorldRay, QuadMesh->GetMeshData(), FacingMatrix, OutHit);
}

void UParticleSubUVComponent::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);

	if (bIsLoading)
	{
		if (Handle.contains("Material") && Handle["Material"].is_object())
		{
			if (UMaterial* Loaded = UMaterial::LoadMaterial(Handle["Material"]))
			{
				Material = Loaded; // 못 만들었으면 생성자 기본값 유지
			}
		}
	}
	else
	{
		Handle["Material"] = Material ? UMaterial::SaveMaterial(Material) : json(nullptr);
	}
}

void UParticleSubUVComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	UMaterial* Loaded = Material;
	UMaterial::SerializeMaterialReference(Record.EnterField("Material"), Loaded);

	// 불러오기에 실패하면(null) 생성자가 넣어 둔 기본 머티리얼을 유지한다
	if (Record.GetUnderlyingArchive().IsLoading() && Loaded)
	{
		Material = Loaded;
	}
}

// 파티클 에셋과 배열을 준비하고 초기 상태를 채운다.
void UParticleSubUVComponent::BeginPlay()
{
	Super::BeginPlay();

	Particles.Reserve(ParticleCount);
	for (int32 i = 0; i < ParticleCount; ++i)
	{
		FParticle Particle;
		RespawnParticle(Particle);

		Particle.Age = GetRandomNumberBetween(0.0f, Particle.LifeTime);
		Particle.Location.Z += Particle.Velocity.Z * Particle.Age;

		const float LifeRatio = Particle.Age / Particle.LifeTime;
		Particle.Scale = Lerp(StartScale, EndScale, LifeRatio);

		const float ElapsedFrames = Particle.Age * FrameRate;
		Particle.SubUVFrame = static_cast<uint32>(ElapsedFrames) % (ColSize * RowSize);

		Particles.Add(Particle);
	}
}

// 열·행 개수가 양수인지 검사해 SubUV 분할 수를 설정한다.
void UParticleSubUVComponent::SetSubUVSize(uint32 NewColSize, uint32 NewRowSize)
{
	assert(NewColSize > 0 && NewRowSize > 0);

	ColSize = NewColSize;
	RowSize = NewRowSize;
}

// 유효한 프레임 속도를 저장하고 음수·0이면 기본값을 쓴다.
void UParticleSubUVComponent::SetFrameRate(float InFrameRate)
{
	FrameRate = (InFrameRate > 0.0f) ? InFrameRate : 1.0f;
}

// DeltaTime으로 이동·수명·Atlas 프레임을 갱신한다.
void UParticleSubUVComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	const uint32 TotalFrames = ColSize * RowSize;

	for (FParticle& Particle : Particles)
	{
		Particle.Location += Particle.Velocity * DeltaTime;
		Particle.Age += DeltaTime;

		if (Particle.Age >= Particle.LifeTime)
		{
			RespawnParticle(Particle);

			continue;
		}

		const float LifeRatio = Particle.Age / Particle.LifeTime;

		Particle.Scale = Lerp(StartScale, EndScale, LifeRatio);

		if (LifeRatio > FadeStart)
		{
			const float FadeRatio = GedSmoothStepedRatio(FadeStart, MAX_NORMALIZED_VALUE, LifeRatio);
			Particle.Alpha = MAX_NORMALIZED_VALUE - FadeRatio;
		}
		else
		{
			Particle.Alpha = MAX_NORMALIZED_VALUE;
		}

		Particle.FrameTimer += DeltaTime;
		const float FrameDuration = MAX_NORMALIZED_VALUE / FrameRate;

		if (Particle.FrameTimer >= FrameDuration)
		{
			Particle.FrameTimer = 0.0f;

			++Particle.SubUVFrame;
			Particle.SubUVFrame %= TotalFrames;
		}
	}
}

// 기본 카메라 기준으로 파티클 상수와 렌더 패킷을 구성한다.
void UParticleSubUVComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue)
{
	assert(QuadMesh != nullptr);
	assert(Material != nullptr);

	Constants.Reset();
	Constants.Reserve(Particles.Num());

	UWorld* World = GetOwner() ? GetOwner()->GetWorld() : nullptr;
	const UCameraComponent* Camera = World ? World->GetPlayCameraComponent() : nullptr;
	const FVector CameraPos = Camera ? Camera->GetWorldLocation() : GetWorldLocation();
	for (FParticle& Particle : Particles)
	{
		if (Particle.bAlive == false)
		{
			continue;
		}


		FMatrix WorldMatrix = MakePlayCameraFacingMatrix(World, GetWorldRotation(), GetWorldLocation(), GetWorldScale3D());

		// Scale, Move
		WorldMatrix.M[0][0] *= Particle.Scale;
		WorldMatrix.M[0][1] *= Particle.Scale;
		WorldMatrix.M[0][2] *= Particle.Scale;
		WorldMatrix.M[0][3] *= Particle.Scale;

		WorldMatrix.M[1][0] *= Particle.Scale;
		WorldMatrix.M[1][1] *= Particle.Scale;
		WorldMatrix.M[1][2] *= Particle.Scale;
		WorldMatrix.M[1][3] *= Particle.Scale;

		WorldMatrix.M[2][0] *= Particle.Scale;
		WorldMatrix.M[2][1] *= Particle.Scale;
		WorldMatrix.M[2][2] *= Particle.Scale;
		WorldMatrix.M[2][3] *= Particle.Scale;

		WorldMatrix.M[3][0] = Particle.Location.X;
		WorldMatrix.M[3][1] = Particle.Location.Y;
		WorldMatrix.M[3][2] = Particle.Location.Z;
		WorldMatrix.M[3][3] = 1.0f;

		const FVector ParticlePos = Particle.Location;
		const FVector CameraToParticleVec = ParticlePos - CameraPos;

		const float CameraToParticleDistance =
			CameraToParticleVec.X *
				CameraToParticleVec.X +

			CameraToParticleVec.Y *
				CameraToParticleVec.Y +

			CameraToParticleVec.Z *
				CameraToParticleVec.Z;

		FRenderPacket Packet;
		Packet.Model = RenderQueue.StoreWorldMatrix(WorldMatrix);
		Packet.VertexFactory = &QuadMesh->GetVertexFactory(0);
		Packet.IndexBuffer = QuadMesh->GetIndexBuffer(0);
		Packet.Material = Material;

		FSubUVConstants C;
		C.CurrentFrame = Particle.SubUVFrame;
		C.AtlasColSize = ColSize;
		C.AtlasRowSize = RowSize;
		C.Alpha = Particle.Alpha;

		Constants.Add(C);

		Packet.MaterialParamData = &Constants.Last();
		Packet.MaterialParamDataSize = sizeof(FSubUVConstants);

		Packet.CameraToParticleDistance = CameraToParticleDistance;

		RenderQueue.Add(Packet);
	}
}

// 파티클 위치·속도·수명 등 재생성 상태를 초기화한다.
void UParticleSubUVComponent::RespawnParticle(FParticle& Particle)
{
	Particle.Location = GetWorldLocation();

	Particle.Velocity.X = 0.0f;
	Particle.Velocity.Y = 0.0f;
	Particle.Velocity.Z = GetRandomNumberBetween(MinRiseSpeed, MaxRiseSpeed);

	Particle.LifeTime = GetRandomNumberBetween(MinLifeTime, MaxLifeTime);
	Particle.Age = 0.0f;
	Particle.SubUVFrame = static_cast<uint32>(GetRandomNumberBetween(0.0f, static_cast<float>((ColSize * RowSize) - 1)));
	Particle.Scale = StartScale;
	Particle.FrameTimer = 0.0f;
	Particle.bAlive = true;

	Particle.Alpha = MAX_NORMALIZED_VALUE;
}

// Todo: Move to util class
// 난수를 지정 실수 구간으로 변환한다.
float UParticleSubUVComponent::GetRandomNumberBetween(float start, float end) const
{
	float randomNumber = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);

	return start + randomNumber * (end - start);
}

// 두 값을 alpha 비율로 선형 보간한다.
float UParticleSubUVComponent::Lerp(float left, float right, float alpha) const
{
	if (alpha < 0.0f)
	{
		alpha = 0.0f;
	}

	if (alpha > 1.0f)
	{
		alpha = 1.0f;
	}

	return left + (right - left) * alpha;
}

// 정규화한 구간 비율에 삼차 smoothstep을 적용한다.
float UParticleSubUVComponent::GedSmoothStepedRatio(float start, float end, float value) const
{
	float normedValue = (value - start) / (end - start);

	if (normedValue < 0.0f)
	{
		normedValue = 0.0f;
	}

	if (normedValue > 1.0f)
	{
		normedValue = 1.0f;
	}

	return normedValue * normedValue * (3.0f - 2.0f * normedValue);
}
