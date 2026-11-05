#pragma once

#include "PrimitiveComponent.h"
// FSubUVConstants와 카메라를 향하는 행렬 계산을 Billboard와 함께 쓴다.
#include "BillboardComponent.h"

struct FParticle
{
	// 파티클 한 개의 물리·수명·SubUV 프레임 상태를 담는다.
	FVector Location;
	FVector Velocity;
	float LifeTime;

	float Age;
	float Scale;
	uint32 SubUVFrame;
	float FrameTimer;

	float Alpha;

	bool bAlive;
};



// 가장 단순한 SubUV 파티클입니다.
// Atlas 전체를 Col x Row로 나누고 CurrentFrame 하나만 변경합니다.
// UE의 파티클 컴포넌트처럼 Primitive를 직접 상속한다. 파티클마다 카메라를 향하는 쿼드를 그린다.
class UParticleSubUVComponent : public UPrimitiveComponent
{
	DECLARE_CLASS(UParticleSubUVComponent, UPrimitiveComponent)

	REFLECT_START(ClassName)
		PROPERTY(ColSize)
		PROPERTY(RowSize)
		PROPERTY(FrameRate)
	REFLECT_END()

public:
	UParticleSubUVComponent();
	virtual ~UParticleSubUVComponent() = default;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime) override;
	// 시뮬레이션 배열과 View별 렌더 제출을 분리한다.
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	// 기본 카메라용 호환 경로이며 View별 수집은 SceneProxy가 맡는다.
	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue) override;

	virtual void AddReferencedMaterials(TSet<UMaterial*>& OutMaterials) const override
	{
		Super::AddReferencedMaterials(OutMaterials);
		OutMaterials.Add(Material);
	}
	virtual int32 GetNumMaterials() const override { return 1; }
	virtual UMaterial* GetMaterial(int32 SlotIndex) const override { return SlotIndex == 0 ? Material : nullptr; }
	virtual void SetMaterial(int32 SlotIndex, UMaterial* InMaterial) override
	{
		if (SlotIndex == 0)
		{
			Material = InMaterial;
		}
	}
	virtual const FStaticMeshData* GetMeshData() const override { return QuadMesh ? &QuadMesh->GetMeshData() : nullptr; }
	// 쿼드가 어느 방향으로 돌아도 감싸도록 스케일 Y·Z를 폭·높이로 본 반지름 상자를 쓴다.
	virtual FBox CalcLocalBounds() const override;
	// 게임 카메라를 향하는 쿼드로 판정한다.
	virtual bool LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit) override;

	virtual void Serialize(json& Handle, bool bIsLoading) override;
	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	void SetSubUVSize(uint32 Cols, uint32 Rows);
	void SetFrameRate(float InFrameRate);

private:
	friend class FParticleSceneProxy;
	void RespawnParticle(FParticle& Particle);

	// Todo: Move to util class
	float GetRandomNumberBetween(float start, float end) const;
	float Lerp(float first, float second, float alpha) const;
	float GedSmoothStepedRatio(float start, float end, float value) const;

private:
	static const float MAX_NORMALIZED_VALUE;

	// ---- 튜닝 값 ----
	// 방출은 수명이 끝난 파티클이 무작위 시점에 리스폰되는 방식이다.
	// 초당 평균 방출량 ≈ ParticleCount / 평균 수명. 수명 범위가 넓을수록 방출 간격이 더 불규칙해진다.
	int32 ParticleCount = 10; // 동시에 존재하는 파티클 수 (생성자에서만 반영)

	float MinLifeTime = 6.0f; // 수명 최소값(초)
	float MaxLifeTime = 8.0f; // 수명 최대값(초)

	float MinRiseSpeed = 0.7f; // 상승 속도 최소값
	float MaxRiseSpeed = 0.9f; // 상승 속도 최대값

	float StartScale = 1.2f; // 생성 시 크기
	float EndScale = 1.5f;   // 수명이 끝날 때 크기

	float FadeStart = 0.8f; // 수명 비율이 이 값을 넘으면 페이드아웃 시작 (0~1)

	UMaterial* Material = nullptr;
	UStaticMesh* QuadMesh = nullptr;

	// 기본 카메라 호환 제출용이며 View 공통 상수는 Particle Proxy가 별도로 소유한다.
	TArray<FSubUVConstants> Constants;

	uint32 ColSize;
	uint32 RowSize;
	float FrameRate;

	// Todo: Reserve particles
	TArray<FParticle> Particles;
};
