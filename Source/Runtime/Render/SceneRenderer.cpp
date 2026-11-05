#include "EnginePCH.h"
#include "Render/SceneRenderer.h"

#include "Components/DirectionalLightComponent.h"
#include "Core/ResourcePaths.h"
#include "Core/Stats/LightweightStats.h"
#include "Core/Stats/ShadowStats.h"
#include "Engine/DirectionalLightSceneProxy.h"
#include "Engine/World.h"
#include "Math/Frustum.h"
#include "Render/ProjectedShadowInfo.h"
#include "Render/RenderCommand.h"
#include "Sequence/SequenceActor.h"

#include <algorithm>

bool FSceneView::NeedsShadows() const
{
	return bDrawPrimitives && bDynamicShadows && (ViewMode == EViewMode::Lit || ViewMode == EViewMode::Lit_Lambert || ViewMode == EViewMode::Lit_BlinnPhong);
}

bool FSceneRenderer::Init(FRenderer* InRenderer)
{
	Renderer = InRenderer;
	return Renderer && SkyboxRenderer.Init(ResourcePaths::DefaultSkyPanorama) && DecalRenderer.Init() && FogRenderer.Init(Renderer) && ShadowViewRenderer.Init() && ToneMapRenderer.Init() && FXAARenderer.Init();
}

FSkyboxRenderer& FSceneRenderer::GetSkyboxRenderer(const FString& PanoramaPath)
{
	if (PanoramaPath.empty())
	{
		return SkyboxRenderer;
	}

	// 실패해도 넣어 두어 매 프레임 다시 불러오지 않는다
	TUniquePtr<FSkyboxRenderer>& Found = ExtraSkyboxRenderers[PanoramaPath];
	if (!Found)
	{
		Found = MakeUnique<FSkyboxRenderer>();
		Found->Init(PanoramaPath);
	}
	return Found->IsValid() ? *Found : SkyboxRenderer;
}

void FSceneRenderer::BeginRenderViews(UWorld& World)
{
	Gatherer.BeginRenderViews(World.GetScene());
}

void FSceneRenderer::GatherRenderPackets(UWorld& World, FRenderQueue& RenderQueue, const FLODViewContext* LODView,
	const FFrustumPlanes* Frustum, FRenderer* InRenderer, FBillboardMatrixBuilder ResolveBillboard,
	const void* ViewContext, FRenderQueue* IconQueue, FRenderQueue* ArrowQueue, bool bDrawSkeletalMesh)
{
	Gatherer.GatherRenderPackets(World.GetScene(), RenderQueue, LODView, Frustum, InRenderer,
		ResolveBillboard, ViewContext, IconQueue, ArrowQueue, bDrawSkeletalMesh);
}

void FSceneRenderer::GetDirectionalShadowSplits(const FSceneView& View, const FDirectionalLightSceneProxy& Directional, TArray<float>& OutSplits)
{
	OutSplits.Reset();
	const auto* Light = static_cast<const UDirectionalLightComponent*>(Directional.GetLightComponent());
	if (View.bOrthographic)
	{
		// 직교 View는 논리 원점 앞뒤의 단일 구간을 쓴다.
		OutSplits.Add(-Light->GetShadowDistance());
		OutSplits.Add(Light->GetShadowDistance());
		return;
	}

	if (!Directional.CalculateCascadeSplitDistances(View.NearZ, View.FarZ, OutSplits))
	{
		OutSplits.Reset();
	}
}

void FSceneRenderer::GatherDirectionalShadows(UWorld& World, const FSceneView& View, float MinScreenRadiusSquared, TArray<FProjectedShadowInfo>& OutShadows)
{
	const FDirectionalLightSceneProxy* Directional = World.GetScene().GetPrimaryDirectionalLight();
	if (!Directional || !Directional->ShouldCastShadow() || !View.AppendFrustumCorners)
	{
		return;
	}

	const auto* Light = static_cast<const UDirectionalLightComponent*>(Directional->GetLightComponent());
	const uint32 ExpectedCascadeCount = !View.bOrthographic && Light->IsCSMEnabled()
		? static_cast<uint32>(Light->GetCascadeCount())
		: 1u;
	FScopeCycleCounter SetupCounter(ShadowStats::SetupCPUStats[ShadowStats::GetGroup(true, ExpectedCascadeCount)]);

	TArray<float> Splits;
	GetDirectionalShadowSplits(View, *Directional, Splits);
	if (Splits.Num() < 2)
	{
		return;
	}

	TArray<FVector> ReceiverCorners;
	ReceiverCorners.Reserve(8);

	if (View.bOrthographic)
	{
		if (View.AppendFrustumCorners(View.FrustumContext, Splits[0], Splits[1], ReceiverCorners))
		{
			Gatherer.GatherDirectionalShadow(World.GetScene(), OutShadows, ReceiverCorners, View.LODView,
				Splits[0], Splits[1], MinScreenRadiusSquared, 0, View.ViewId, 1, View.bDrawSkeletalMesh);
		}
		return;
	}

	// 빈 큐가 생겨도 논리 Cascade 개수와 번호는 압축하지 않는다.
	const uint32 LogicalCascadeCount = static_cast<uint32>(Splits.Num() - 1);
	for (int32 CascadeIndex = 0; CascadeIndex + 1 < Splits.Num(); ++CascadeIndex)
	{
		// 앞 Cascade와 겹치는 전환 구간만큼 앞으로 당겨 그린다.
		float RenderNear = Splits[CascadeIndex];
		if (CascadeIndex > 0)
		{
			RenderNear -= (Splits[CascadeIndex] - Splits[CascadeIndex - 1]) * Light->GetCascadeTransitionFraction();
		}

		ReceiverCorners.Reset();
		if (!View.AppendFrustumCorners(View.FrustumContext, RenderNear, Splits[CascadeIndex + 1], ReceiverCorners))
		{
			continue;
		}

		Gatherer.GatherDirectionalShadow(World.GetScene(), OutShadows, ReceiverCorners, View.LODView,
			RenderNear, Splits[CascadeIndex + 1], MinScreenRadiusSquared,
			static_cast<uint32>(CascadeIndex), View.ViewId, LogicalCascadeCount, View.bDrawSkeletalMesh);
	}
}

void FSceneRenderer::RenderShadowDepths(const TArray<FSceneShadowViews>& ShadowViews)
{
	const float MinScreenRadius = std::max(0.0f, GMinScreenRadiusForShadowCaster);
	const float MinScreenRadiusSquared = MinScreenRadius * MinScreenRadius;
	TArray<FProjectedShadowInfo> Shadows;

	for (const FSceneShadowViews& Group : ShadowViews)
	{
		if (!Group.World)
		{
			continue;
		}

		TArray<const FSceneView*> Views;
		for (const FSceneView* View : Group.Views)
		{
			if (View && View->NeedsShadows())
			{
				Views.Add(View);
			}
		}
		if (Views.Num() == 0)
		{
			continue;
		}

		SCOPE_CYCLE_COUNTER(ShadowStats::STAT_SetupCPU);
		{
			// 같은 World의 여러 View가 공유하는 지역광 작업을 한 번 만든다.
			SCOPE_CYCLE_COUNTER(ShadowStats::SetupCPUStats[ShadowStats::Local]);
			TArray<FLODViewContext> LODViews;
			TArray<FFrustumPlanes> Frustums;
			LODViews.Reserve(Views.Num());
			Frustums.Reserve(Views.Num());
			// 지역광 그림자는 View들이 공유하므로, 한 View라도 스켈레탈 메시를 그리면 Caster로 넣는다.
			bool bAnyDrawSkeletalMesh = false;
			for (const FSceneView* View : Views)
			{
				LODViews.Add(View->LODView);
				Frustums.Add(ExtractFrustumPlanes(View->LODView.ViewProjection));
				bAnyDrawSkeletalMesh |= View->bDrawSkeletalMesh;
			}
			Gatherer.GatherLocalProjectedShadows(Group.World->GetScene(), Shadows, Frustums, LODViews, MinScreenRadiusSquared, bAnyDrawSkeletalMesh);
		}

		for (const FSceneView* View : Views)
		{
			GatherDirectionalShadows(*Group.World, *View, MinScreenRadiusSquared, Shadows);
		}
	}

	Renderer->RenderShadowMaps(std::move(Shadows), ShadowViewRenderer);
}

FViewConstants FSceneRenderer::BuildViewConstants(UWorld& World, const FSceneView& View) const
{
	// GPU의 int4·float4와 Component의 분할 상한이 일치해야 한다.
	static_assert(UDirectionalLightComponent::MaxShadowCascades == 4);

	FViewConstants ViewConstants{};
	ViewConstants.ViewProjection = View.ViewProjection;
	ViewConstants.CameraWorldPos = View.Location;
	ViewConstants.CameraForward = View.Forward;
	ViewConstants.VisualizeShadowCascades = View.bVisualizeShadowCascades ? 1u : 0u;

	// 작업 생성과 같은 조건·분할 규칙으로 Directional 수신 데이터를 채운다.
	const FDirectionalLightSceneProxy* Directional = View.NeedsShadows() ? World.GetScene().GetPrimaryDirectionalLight() : nullptr;
	if (!Directional || !Directional->ShouldCastShadow())
	{
		return ViewConstants;
	}

	TArray<float> Splits;
	GetDirectionalShadowSplits(View, *Directional, Splits);
	if (Splits.Num() < 2)
	{
		return ViewConstants;
	}

	const uint32 CascadeCount = static_cast<uint32>(Splits.Num() - 1);
	ViewConstants.DirectionalCascadeCount = CascadeCount;
	ViewConstants.DirectionalShadowNearDistance = Splits[0];
	if (CascadeCount > 1)
	{
		const auto* Light = static_cast<const UDirectionalLightComponent*>(Directional->GetLightComponent());
		ViewConstants.DirectionalCascadeTransitionFraction = Light->GetCascadeTransitionFraction();
	}
	for (uint32 CascadeIndex = 0; CascadeIndex < CascadeCount; ++CascadeIndex)
	{
		// GPU 인덱스는 연속 번호라고 가정하지 않고 각각 조회한다.
		ViewConstants.DirectionalShadowIndices[CascadeIndex] = Renderer->GetShadowIndex(*Directional, CascadeIndex, View.ViewId);
		ViewConstants.DirectionalCascadeFarDistances[CascadeIndex] = Splits[CascadeIndex + 1];
	}
	return ViewConstants;
}

void FSceneRenderer::RenderBasePass(UWorld& World, const FSceneView& View, const FViewConstants& ViewConstants, FRenderQueue& RenderQueue)
{
	const FRenderingInfo& SceneInfo = View.SceneTargets;
	RenderCommand::BeginRenderPass(SceneInfo);

	// 삼각형 연결은 유지하고 View별 Fill Mode만 고른다.
	const ERasterizerState SceneRasterizerState = View.bWireframe ? ERasterizerState::Wireframe : ERasterizerState::SolidBack;
	// 불투명·반투명 모두 이 뷰모드로 UberLit 변형을 고른다.
	Renderer->SetViewMode(View.ViewMode);

	// 직교는 시선이 모두 평행해 파노라마의 한 점만 확대되므로 하늘을 그리지 않고 지우기 색을 배경으로 쓴다.
	if (!View.bOrthographic)
	{
		GetSkyboxRenderer(View.SkyPanorama).OnRender(View.ViewProjection, View.Location);
	}

	if (View.bDrawPrimitives)
	{
		RenderCommand::SetRasterizerState(SceneRasterizerState);
		RenderCommand::SetBlendState(EBlendState::Opaque);
		RenderCommand::SetDepthStencilState(EDepthStencilState::Default);
		RenderCommand::SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		Renderer->RenderQueueSorting(RenderQueue, ViewConstants);

		// 2.5D 컬링은 타일의 실제 깊이가 필요하므로 불투명 깊이를 먼저 채운 뒤 컬링한다.
		FTexture2D* DepthTexture = View.GetSceneDepth();
		const bool bDepthPrepass = Renderer->GetLightCullingMode() == ELightCullingMode::Tiled25D && DepthTexture;
		if (bDepthPrepass)
		{
			Renderer->RenderDepth(View.ViewProjection);
			// DSV로 묶인 텍스처는 SRV로 읽을 수 없으므로 렌더 타깃을 푼다.
			RenderCommand::EndRenderPass(SceneInfo);
		}

		Renderer->CullLights(View.ViewMatrix, View.ProjectionMatrix,
			SceneInfo.ViewportSetting.Width, SceneInfo.ViewportSetting.Height,
			bDepthPrepass ? DepthTexture : nullptr);

		if (bDepthPrepass)
		{
			// 스카이와 Prepass 깊이를 지우지 않도록 Load로 다시 묶는다.
			FRenderingInfo ResumeInfo = SceneInfo;
			for (FRenderingDesc& Color : ResumeInfo.ColorRenderTargets)
			{
				Color.LoadOp = ERenderTargetLoadOp::Load;
			}
			ResumeInfo.DepthStencil.LoadOp = ERenderTargetLoadOp::Load;
			RenderCommand::BeginRenderPass(ResumeInfo);
		}

		Renderer->RenderOpaque(View.ViewProjection);
		// 반투명은 깊이를 쓰지 않아 데칼을 가리지 못하므로, 데칼을 먼저 입히고 그 위에 반투명을 섞는다.
		RenderDecals(World, View);
		Renderer->RenderTranslucent(View.ViewProjection);
		// 장면 Wireframe이 뒤의 표시로 전파되지 않도록 복원한다.
		RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
	}

	RenderCommand::EndRenderPass(SceneInfo);
}

void FSceneRenderer::RenderDecals(UWorld& World, const FSceneView& View)
{
	const FScene& Scene = World.GetScene();
	if (!View.bDecals || View.bWireframe || Scene.Decals.IsEmpty())
	{
		return;
	}

	// 데칼도 UberLit과 같은 라이트·그림자로 조명한다.
	Renderer->BindLightingResources();
	DecalRenderer.OnRender(Scene, View.ViewMode, ExtractFrustumPlanes(View.ViewProjection));
}

void FSceneRenderer::RenderFog(UWorld& World, const FSceneView& View)
{
	// 직교 View에는 안개를 그리지 않는다. 스크립트 오버라이드는 표시 설정과 무관하게 적용한다.
	if (View.bOrthographic || (!View.bFog && !World.GetScene().HasFogOverride()))
	{
		return;
	}

	// 여러 개가 있어도 하나의 안개만 그린다.
	const FFogInfo* FogInfo = World.GetScene().GetFogInfo();
	if (!FogInfo)
	{
		return;
	}

	FRenderingInfo PassInfo = View.SceneTargets;
	for (FRenderingDesc& Color : PassInfo.ColorRenderTargets)
	{
		Color.LoadOp = ERenderTargetLoadOp::Load;
	}
	PassInfo.DepthStencil.Texture = nullptr;

	RenderCommand::BeginRenderPass(PassInfo);
	FogRenderer.OnRender(View.GetSceneDepth(), View.ViewProjection, View.Location, *FogInfo);
	RenderCommand::EndRenderPass(PassInfo);
}

void FSceneRenderer::RenderToneMap(UWorld& World, const FSceneView& View, FTexture2D* LDRTarget)
{
	FTexture2D* HDRColor = View.GetSceneColor();
	if (!LDRTarget || !HDRColor)
	{
		return;
	}

	// 출력 전체를 덮어쓰므로 Clear하지 않는다.
	FRenderingInfo PassInfo;
	PassInfo.ViewportSetting = View.SceneTargets.ViewportSetting;
	FRenderingDesc ColorDesc{};
	ColorDesc.Texture = LDRTarget;
	ColorDesc.LoadOp = ERenderTargetLoadOp::DontCare;
	PassInfo.ColorRenderTargets.Add(ColorDesc);
	PassInfo.DepthStencil.Texture = nullptr;

	// - Lit: 노출 → ACES → sRGB 인코딩
	// - Unlit: 텍스처 원본 색이 보이도록 sRGB 인코딩만
	// - WorldNormal·라이트 디버그: 셰이더가 쓴 값을 그대로
	EToneMapMode ToneMapMode = EToneMapMode::ACES;
	if (View.ViewMode == EViewMode::WorldNormal || View.bDebugLightView)
	{
		ToneMapMode = EToneMapMode::Passthrough;
	}
	else if (View.ViewMode == EViewMode::Unlit || View.ViewMode == EViewMode::Wireframe)
	{
		ToneMapMode = EToneMapMode::EncodeOnly;
	}

	RenderCommand::BeginRenderPass(PassInfo);
	ToneMapRenderer.SetExposure(View.Exposure);
	const FVignetteSettings& Vignette = World.GetVignetteSettings();
	ToneMapRenderer.SetVignette(Vignette.GetEffectiveIntensity(), Vignette.Radius, Vignette.Softness, Vignette.Color);
	ToneMapRenderer.OnRender(HDRColor, ToneMapMode, ASequenceActor::GetWorldFadeAlpha(&World));
	RenderCommand::EndRenderPass(PassInfo);

	// HDR 색이 SRV로 남아 있으면 다음 패스에서 RTV로 쓸 때 D3D가 강제로 해제하므로 먼저 비운다.
	RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);
}

void FSceneRenderer::RenderView(UWorld& World, const FSceneView& View, FRenderQueue& RenderQueue, FTexture2D* LDRTarget)
{
	const FViewConstants ViewConstants = BuildViewConstants(World, View);
	RenderBasePass(World, View, ViewConstants, RenderQueue);
	RenderFog(World, View);
	RenderToneMap(World, View, LDRTarget);
}

void FSceneRenderer::RenderFXAA(FTexture2D* Source, const FRenderingInfo& Target)
{
	if (!Source)
	{
		return;
	}

	RenderCommand::BeginRenderPass(Target);
	FXAARenderer.OnRender(Source);
	RenderCommand::EndRenderPass(Target);
	RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);
}

void FSceneRenderer::RenderFXAAInPlace(FTexture2D* Source, FTexture2D* Temp, const FViewportSettings& Viewport)
{
	if (!Source || !Temp)
	{
		return;
	}

	// 출력 전체를 덮어쓰므로 Clear하지 않는다.
	FRenderingInfo PassInfo;
	PassInfo.ViewportSetting = Viewport;
	FRenderingDesc ColorDesc{};
	ColorDesc.Texture = Temp;
	ColorDesc.LoadOp = ERenderTargetLoadOp::DontCare;
	PassInfo.ColorRenderTargets.Add(ColorDesc);
	PassInfo.DepthStencil.Texture = nullptr;

	RenderFXAA(Source, PassInfo);
	RenderCommand::CopyTexture(Source, Temp);
}
