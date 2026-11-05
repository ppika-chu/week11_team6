#pragma once

#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/SceneManagement.h"
#include "Math/Frustum.h"
#include "Math/Sphere.h"
#include "Render/MeshRenderData.h"
#include "Render/RenderPacket.h"

#include <vector>

class FScene;
class FRenderer;
class FProjectedShadowInfo;
class ULightComponent;
struct FLODViewContext;

struct FRenderStats
{
	uint32 TotalPrimitives = 0;
	uint32 VisiblePrimitives = 0;
	uint32 DrawCalls = 0;
	uint64 Triangles = 0;
	uint32 LODCounts[4] = {};
	uint64 LODTriangles[4] = {};

	void Reset() { *this = FRenderStats(); }
};

// FScene에서 View별로 그릴 패킷과 그림자 작업의 Caster를 모은다.
// 컬링·LOD 선택·병렬 수집에 쓰는 버퍼를 프레임마다 재사용하며, World(게임 객체)는 이 일을 하지 않는다.
class FSceneGatherer
{
public:
	// 캡처 구간을 시작하고 첫 가시 View에서 동적 렌더 스냅샷을 준비하게 한다. World Tick 뒤 프레임마다 한 번 부른다.
	void BeginRenderViews(FScene& Scene);

	// Resolver가 있는 View 호출은 LODView와 사전 BeginRenderViews 호출을 전제로 한다.
	void GatherRenderPackets(FScene& Scene, FRenderQueue& RenderArray, const FLODViewContext* LODView = nullptr,
		const FFrustumPlanes* Frustum = nullptr, FRenderer* Renderer = nullptr,
		FBillboardMatrixBuilder ResolveBillboard = nullptr,
		const void* ViewContext = nullptr, FRenderQueue* IconQueue = nullptr,
		FRenderQueue* ArrowQueue = nullptr,
		bool bDrawSkeletalMesh = true);

	// BeginRenderViews 이후 Scene의 View들이 공유하는 지역광 작업을 한 번 추가한다.
	void GatherLocalProjectedShadows(FScene& Scene, TArray<FProjectedShadowInfo>& OutShadows,
		const TArray<FFrustumPlanes>& ActiveViewFrustums,
		const TArray<FLODViewContext>& ActiveViews, float MinScreenRadiusSquared,
		bool bDrawSkeletalMesh = true);

	// 지정한 View·Cascade의 투영과 원래 논리 분할 개수를 보관하고 Caster를 수집한다.
	void GatherDirectionalShadow(FScene& Scene, TArray<FProjectedShadowInfo>& OutShadows,
		const TArray<FVector>& ReceiverCorners, const FLODViewContext& View,
		float ReceiverNearDistance, float ReceiverFarDistance,
		float MinScreenRadiusSquared, uint32 ProjectionIndex, uint32 ViewId,
		uint32 LogicalCascadeCount = 1, bool bDrawSkeletalMesh = true);

	// 마지막 GatherRenderPackets의 통계.
	const FRenderStats& GetRenderStats() const { return RenderStats; }

private:
	// 메인 스레드에서 Proxy를 준비하고 작업 버퍼로 동적 패킷을 추가한다.
	bool GatherDynamicRenderPackets(FPrimitiveSceneProxy& Proxy, FRenderQueue& RenderQueue,
		const FVector& CameraLocation, FBillboardMatrixBuilder ResolveBillboard,
		const void* ViewContext, FRenderQueue* IconQueue = nullptr);

	// BeginRenderViews와 SetupProjection 이후, 해당 작업에 한 번 호출한다.
	// Light Frustum과 CastShadow 조건으로 정적·스켈레탈 메시 Caster를 수집한다.
	// bDrawSkeletalMesh가 false면 스켈레탈 메시는 그림자도 만들지 않는다.
	void GatherShadowCasters(FScene& Scene, FProjectedShadowInfo& ShadowInfo, const FLODViewContext* Views,
		int32 ViewCount, float MinScreenRadiusSquared, const FLightRenderParameters* LocalLight = nullptr,
		float CasterSphereRadius = 0.0f, bool bDrawSkeletalMesh = true);

	// 활성 Lit View 중 하나에서 Receiver 후보가 보이면 유지한다.
	// LocalLight와 ReceiverFrustum은 지정한 경우에만 영향 구와 투영 범위를 제한한다.
	bool HasVisibleShadowReceiver(FScene& Scene, const TArray<FFrustumPlanes>& ActiveViewFrustums,
		const FLightRenderParameters* LocalLight = nullptr, const FFrustumPlanes* ReceiverFrustum = nullptr,
		uint32 ReceiverPlaneMask = FrustumAllPlanesMask) const;

	// 모든 전달 View에서 Bounds가 작을 때만 해당 투영의 제출을 생략한다.
	static bool IsSmallShadowCaster(const FLODSphere& Sphere, const FLODViewContext* Views, int32 ViewCount, float MinScreenRadiusSquared);

	// 실제 사각 Projection 전체와 래스터 경계 여유를 포함한다.
	// 0을 반환하면 추가 Sphere 검사를 사용하지 않는다.
	static float GetLocalShadowCasterRadius(const ULightComponent& Light, const FLightRenderParameters& Parameters, float TanHalfFOV);

	struct alignas(64) FGatherChunk
	{
		TArray<FRenderPacket> Packets; // 스태틱 묶음에 못 들어가는 것 (반투명 섹션, Renderer 없는 호출)
		TArray<uint32> SlowPathIndices;
		std::vector<FStaticDrawGroup> Groups; // 불투명 스태틱 메시: (머티리얼, 메시, LOD)별 묶음. 항목만 매 프레임 비운다
		uint32 LastGroup = 0;                 // 바로 전 물체가 들어간 묶음 (연속한 물체는 대개 같은 묶음)
		uint32 StaticDrawCount = 0;
		uint32 LODCounts[4] = {};
		uint64 LODTriangles[4] = {};
	};
	TArray<FGatherChunk> GatherChunks;

	// GatherRenderPackets가 매 프레임 채우는 컬링 결과. 용량을 재사용한다.
	TArray<FPrimitiveSceneProxy*> VisibleProxies;
	TArray<FLODSelectionInput> LODInputs;
	TArray<uint8> SelectedLODs;
	// 메인 스레드의 동적 수집에서 View마다 내용을 바꾸고 용량은 재사용한다.
	TArray<int32> ParticleOrder;
	TArray<float> ParticleDistances;

	FRenderStats RenderStats;
};
