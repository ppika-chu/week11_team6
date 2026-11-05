#pragma once

#include "Math/Matrix.h"
#include "Math/Vector4.h"
#include "Render/Buffer.h"
#include "Render/PipelineState.h"
#include "Render/ViewMode.h"

class FScene;
class FPrimitiveSceneProxy;
class UDecalComponent;
struct FFrustumPlanes;

// DecalShader.hlsl의 cbuffer DecalConstants(b3)와 순서·크기가 같아야 한다. b0은 UberLit과 같은 ViewConstants 자리다.
// VS(행렬)와 PS(색·알파)가 같은 버퍼를 함께 쓴다.
struct FDecalConstants
{
	FMatrix World;          // 받는 메시의 월드 행렬
	FMatrix WorldInverseTranspose;
	FMatrix DecalVP;        // UDecalComponent::GetDecalViewProjection()
	FVector4 DecalColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f); // 선형 머티리얼 DiffuseOrFactor × 선형 컴포넌트 DecalColor
	float FadeAlpha = 1.0f;
	float bAdditive = 0.0f; // 1이면 셰이더가 색에 알파를 미리 곱한다 (One/One 블렌드)
	float Shininess = 32.0f;
	uint32 ReceiveShadow = 0;
	FVector SpecularColor = FVector(1, 1, 1); // 선형 반사광 색상
	float SpecularFactor = 1.f; // 반사광 강도 계수
	FVector EmissiveColor = FVector(0, 0, 0); // 선형 자체 발광 색상
	float EmissiveFactor = 1.f; // 자체 발광 강도 계수

};
static_assert(sizeof(FDecalConstants) == 64 * 3 + 16 + 16 + 32, "DecalShader.hlsl cbuffer DecalConstants와 크기가 같아야 한다");

// Decal Pass. 불투명을 그린 뒤 반투명을 그리기 전에, 데칼 상자와 겹치는 메시를 데칼 셰이더로 한 번 더 그린다 (Multipass).
// 깊이는 읽기만 하고(LESS_EQUAL, 쓰기 Off) 색은 Alpha Blend로 섞는다.
class FDecalRenderer
{
public:
	FDecalRenderer() = default;
	~FDecalRenderer() = default;

	FDecalRenderer(const FDecalRenderer&) = delete;
	FDecalRenderer& operator=(const FDecalRenderer&) = delete;

	bool Init();
	// 렌더 타깃(장면 색 + 깊이)과 라이트 리소스(FRenderer::BindLightingResources)는 호출한 쪽이 묶어 둔다.
	// ViewMode에 맞는 조명 모델 변형으로 그린다. World Normal 뷰에서는 그리지 않는다.
	// 상자가 Frustum 밖인 데칼은 수신자를 찾지 않고 건너뛴다.
	void OnRender(const FScene& Scene, EViewMode ViewMode, const FFrustumPlanes& Frustum);

private:
	// OBBTree가 고른 삼각형 인덱스를 올릴 동적 버퍼가 Count개를 담을 수 있게 키운다.
	void EnsureTriangleIndexCapacity(uint32 Count);

	// 뷰 모드별 셰이더 변형. Gouraud는 픽셀 단위로 조명하는 Lambert로 대신한다.
	FShaderProgram* Shaders[static_cast<int32>(EViewMode::Count)]{};
	FPipelineState PipelineState;
	TUniquePtr<FConstantBuffer> ConstantBuffer;

	TUniquePtr<FIndexBuffer> TriangleIndexBuffer;
	uint32 TriangleIndexCapacity = 0;

	// 프레임마다 다시 할당하지 않도록 멤버로 두고 Reset해서 쓴다.
	TArray<UDecalComponent*> SortedDecals;
	TArray<FPrimitiveSceneProxy*> Receivers;
	TArray<uint32> TriangleIndices;
};
