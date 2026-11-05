#pragma once

#include "Asset/LOD/StaticMeshLODSelector.h"
#include "Render/DecalRenderer.h"
#include "Render/FogRenderer.h"
#include "Render/FShadowViewRenderer.h"
#include "Render/FXAARenderer.h"
#include "Render/RenderPacket.h"
#include "Render/Renderer.h"
#include "Render/RenderingInfo.h"
#include "Render/SceneGatherer.h"
#include "Render/SkyboxRenderer.h"
#include "Render/ToneMapRenderer.h"
#include "Render/ViewMode.h"

class UWorld;

// 한 View를 그리는 데 필요한 카메라·렌더 대상·표시 설정. UE의 FSceneView에 해당한다.
// 카메라를 어디서 얻는지(에디터 ViewportClient, 게임 PlayerCameraManager)는 호출자가 정한다.
struct FSceneView
{
	FMatrix ViewMatrix;
	FMatrix ProjectionMatrix;
	FMatrix ViewProjection;
	FVector Location;
	FVector Forward;
	float NearZ = 0.1f;
	float FarZ = 10000.0f;
	bool bOrthographic = false;
	FLODViewContext LODView;

	// 그림자 작업을 View마다 구분하는 번호. 0은 World가 공유하는 지역광 작업이 쓴다.
	uint32 ViewId = 1;

	// 그림자 수신 범위의 꼭짓점 8개를 OutCorners에 더한다. 원근은 카메라 앞 [Near, Far] 구간,
	// 직교는 논리 원점 앞뒤 [-Distance, Distance](Near = -Far)다. 투영 규약은 카메라마다 달라 호출자가 계산한다.
	using FAppendFrustumCorners = bool (*)(const void* Context, float Near, float Far, TArray<FVector>& OutCorners);
	FAppendFrustumCorners AppendFrustumCorners = nullptr;
	const void* FrustumContext = nullptr;

	// ColorRenderTargets[0]은 HDR 장면 색, DepthStencil은 장면 깊이다.
	FRenderingInfo SceneTargets;

	EViewMode ViewMode = EViewMode::Lit;
	bool bWireframe = false;
	bool bDrawPrimitives = true;
	bool bDrawSkeletalMesh = true;
	bool bDynamicShadows = true;
	bool bFog = true;
	bool bDecals = true;
	bool bVisualizeShadowCascades = false;
	// 라이트 히트맵처럼 셰이더가 디버그 값을 쓰는 View는 톤 매핑 없이 그대로 출력한다.
	bool bDebugLightView = false;
	float Exposure = 1.0f;
	// 배경 파노라마 경로. 비우면 ResourcePaths::DefaultSkyPanorama (에셋 뷰어는 따로 지정)
	FString SkyPanorama;

	FTexture2D* GetSceneColor() const { return SceneTargets.ColorRenderTargets.Num() > 0 ? SceneTargets.ColorRenderTargets[0].Texture : nullptr; }
	FTexture2D* GetSceneDepth() const { return SceneTargets.DepthStencil.Texture; }
	// 이번 연결 범위에서 그림자를 받는 조명 모델은 Lambert·Blinn-Phong이다.
	bool NeedsShadows() const;
};

// 같은 World를 보는 View들. 지역광 그림자 작업은 이 View들이 공유한다.
struct FSceneShadowViews
{
	UWorld* World = nullptr;
	TArray<const FSceneView*> Views;
};

// World 하나를 View마다 그리는 장면 렌더 파이프라인. UE의 FSceneRenderer에 해당한다.
// 그림자 → 스카이·불투명 → 데칼 → 반투명 → 안개 → 톤 매핑 → FXAA 순서이며, 에디터 표시(Grid·Gizmo 등)는 호출자가 그 위에 그린다.
class FSceneRenderer
{
public:
	bool Init(FRenderer* InRenderer);

	// World Tick 뒤 프레임마다 World별로 한 번 부른다. Transform 반영과 동적 데이터 준비를 예약한다.
	void BeginRenderViews(UWorld& World);
	// View에 보이는 Primitive의 렌더 패킷을 모은다. Resolver가 있으면 BeginRenderViews가 먼저 불려 있어야 한다.
	void GatherRenderPackets(UWorld& World, FRenderQueue& RenderQueue, const FLODViewContext* LODView,
		const FFrustumPlanes* Frustum, FRenderer* InRenderer, FBillboardMatrixBuilder ResolveBillboard,
		const void* ViewContext, FRenderQueue* IconQueue = nullptr, FRenderQueue* ArrowQueue = nullptr,
		bool bDrawSkeletalMesh = true);
	const FRenderStats& GetRenderStats() const { return Gatherer.GetRenderStats(); }

	// 모든 World·View의 그림자 작업을 모아 Atlas 배정과 Depth를 한 번에 실행한다.
	// 빈 목록도 넘겨야 이전 프레임의 작업과 타일 할당이 정리된다.
	void RenderShadowDepths(const TArray<FSceneShadowViews>& ShadowViews);

	// 톤 매핑까지 그린다. 결과는 LDRTarget(SceneTargets와 같은 크기)에 남는다.
	void RenderView(UWorld& World, const FSceneView& View, FRenderQueue& RenderQueue, FTexture2D* LDRTarget);

	// RenderView를 나눠 쓰는 경우(에디터 Depth View 등)를 위한 단계별 함수.
	FViewConstants BuildViewConstants(UWorld& World, const FSceneView& View) const;
	void RenderBasePass(UWorld& World, const FSceneView& View, const FViewConstants& ViewConstants, FRenderQueue& RenderQueue);
	// RenderBasePass가 불투명과 반투명 사이에 부른다. 데칼과 겹치는 메시를 데칼 셰이더로 한 번 더 그린다.
	// 렌더 타깃은 Base Pass가 묶어 둔 것을 그대로 쓴다.
	void RenderDecals(UWorld& World, const FSceneView& View);
	void RenderFog(UWorld& World, const FSceneView& View);
	void RenderToneMap(UWorld& World, const FSceneView& View, FTexture2D* LDRTarget);

	// Source를 읽어 Target에 FXAA 결과를 그린다.
	void RenderFXAA(FTexture2D* Source, const FRenderingInfo& Target);
	// Temp에 그린 뒤 Source로 되돌려 복사한다. 결과 위에 다른 표시를 더 그릴 때 쓴다.
	void RenderFXAAInPlace(FTexture2D* Source, FTexture2D* Temp, const FViewportSettings& Viewport);

private:
	void GatherDirectionalShadows(UWorld& World, const FSceneView& View, float MinScreenRadiusSquared, TArray<FProjectedShadowInfo>& OutShadows);
	// 원근은 Cascade 분할 거리, 직교는 [-ShadowDistance, ShadowDistance]. 실패하면 빈 배열.
	static void GetDirectionalShadowSplits(const FSceneView& View, const class FDirectionalLightSceneProxy& Directional, TArray<float>& OutSplits);
	// View가 지정한 파노라마의 스카이 렌더러. 처음 쓸 때 불러 두고, 실패하면 기본 하늘
	FSkyboxRenderer& GetSkyboxRenderer(const FString& PanoramaPath);

	FRenderer* Renderer = nullptr;
	FSceneGatherer Gatherer;
	FSkyboxRenderer SkyboxRenderer;
	TMap<FString, TUniquePtr<FSkyboxRenderer>> ExtraSkyboxRenderers;
	FDecalRenderer DecalRenderer;
	FFogRenderer FogRenderer;
	FShadowViewRenderer ShadowViewRenderer;
	FToneMapRenderer ToneMapRenderer;
	FFXAARenderer FXAARenderer;
};
