#include "EnginePCH.h"
#include "Render/SceneGatherer.h"

#include "Asset/LOD/StaticMeshLODSelector.h"
#include "Components/BillboardComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/Async/TaskPool.h"
#include "Core/Stats/EditorStats.h"
#include "Core/Stats/LightweightStats.h"
#include "Core/Stats/ShadowStats.h"
#include "Engine/DirectionalLightSceneProxy.h"
#include "Engine/Scene.h"
#include "Math/Frustum.h"
#include "Render/ProjectedShadowInfo.h"
#include "Render/ShadowSettings.h"
#include "Components/SkinnedMeshComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

DECLARE_CYCLE_STAT("Gather Render Packets", STAT_GatherRenderPackets);
DECLARE_CYCLE_STAT("Frustum Cull", STAT_FrustumCull);
DECLARE_CYCLE_STAT("Gather Elements", STAT_GatherElements);
DECLARE_CYCLE_STAT("Gather - LOD", STAT_GatherLOD);
DECLARE_CYCLE_STAT("Gather - Submit", STAT_GatherSubmit);

// Tick 이후의 Scene 상태를 반영하고 새 캡처 구간의 동적 데이터 준비를 예약한다.
void FSceneGatherer::BeginRenderViews(FScene& Scene)
{
	Scene.UpdateAllTransforms();
	for (FPrimitiveSceneProxy* Proxy : Scene.PrimitiveSceneProxies)
	{
		Proxy->InvalidateDynamicData();
	}
}

// Adapter와 World Gather가 같은 Proxy 수집을 사용하며 Queue에는 추가만 한다.
bool FSceneGatherer::GatherDynamicRenderPackets(FPrimitiveSceneProxy& Proxy, FRenderQueue& RenderQueue,
	const FVector& CameraLocation, FBillboardMatrixBuilder ResolveBillboard,
	const void* ViewContext, FRenderQueue* IconQueue)
{
	Proxy.PrepareDynamicData();
	return Proxy.GatherDynamicRenderPackets(RenderQueue, IconQueue, CameraLocation,
		ResolveBillboard, ViewContext, ParticleOrder, ParticleDistances);
}

// TODO : 렌더링 파이프라인 개편해서 이 함수의 책임을 덜어내야함.
// TODO : 언리얼처럼 FSceneRenderer,MeshPassProcessor등의 클래스를 추가해서 해당 클래스로 책임을 덜어낼수있을것으로 보임

// 기존 정적 수집을 유지하고 View 입력이 있으면 동적 Proxy 수집을 연결한다.
void FSceneGatherer::GatherRenderPackets(FScene& Scene, FRenderQueue& RenderQueue, const FLODViewContext* LODView,
	const FFrustumPlanes* Frustum, FRenderer* Renderer, FBillboardMatrixBuilder ResolveBillboard,
	const void* ViewContext, FRenderQueue* IconQueue, FRenderQueue* ArrowQueue, bool bDrawSkeletalMesh)
{
	assert(!ResolveBillboard || LODView != nullptr);
	// 멤버로 두어 매 프레임 용량을 재사용한다.
	RenderStats.Reset();
	VisibleProxies.Reset();

	{
		SCOPE_CYCLE_COUNTER(STAT_FrustumCull);
		// BVH 방문은 호출 스레드에서 동기 실행하므로 에디터 Arrow도 여기서 제출한다.
		const auto Visit = [&](FPrimitiveSceneProxy* Proxy)
		{
			UPrimitiveComponent* Component = Proxy->GetComponent();
			if (!bDrawSkeletalMesh && Cast<USkinnedMeshComponent>(Component))
			{
				return;
			}

			if (ArrowQueue && Proxy->GetMesh())
			{
				UStaticMeshComponent* StaticComponent = Cast<UStaticMeshComponent>(Component);
				if (StaticComponent && StaticComponent->IsEditorIcon())
				{
					// TODO: 모든 프리미티브를 공통 큐로 수집하고 렌더 패스로 분류하도록 통합한 뒤 임시 ArrowQueue 분기를 제거한다.
					if (Proxy->IsVisible())
					{
						Proxy->GetMeshPackets(*ArrowQueue, 0);
					}
					return;
				}
			}
			// 일반 프리미티브는 기존 병렬·동적 수집 경로에서 처리한다.
			VisibleProxies.Add(Proxy);
		};

		if (Frustum)
		{
			Scene.BVH.QueryCull(
				FrustumAllPlanesMask,
				[Frustum](const FBox& Bounds, uint32& Mask)
				{
					return static_cast<EBVHCullResult>(ClassifyBoxInFrustum(Bounds, *Frustum, Mask));
				},
				Visit);
		}
		else
		{
			Scene.BVH.QueryCull(0, [](const FBox&, uint32&)
				{ return EBVHCullResult::Inside; }, Visit);
		}
	}

	RenderStats.TotalPrimitives = Scene.PrimitiveSceneProxies.Num();

	// GPU 오클루전: 프러스텀을 통과한 물체를 GPU에서 가림 판정하고 결과를 이번 프레임에 받아 온다.
	// Cull이 켜져 있으면 가려진 물체를 목록에서 빼서 이후 Gather·정렬·드로우를 모두 건너뛴다.
	// 꺼져 있으면(검증 모드) 목록은 그대로 두고 패킷에 판정만 표시한다.
	const uint8* OccludedMask = nullptr;
	if (Renderer && LODView && Renderer->GetGPUOcclusion().GetSettings().bEnabled)
	{
		FGPUOcclusion& Occlusion = Renderer->GetGPUOcclusion();
		if (Occlusion.Run(VisibleProxies.GetData(), VisibleProxies.Num(), *LODView))
		{
			const std::vector<uint8>& Occluded = Occlusion.GetOccluded();
			if (Occlusion.GetSettings().bCull)
			{
				uint32 Kept = 0;
				for (uint32 i = 0; i < VisibleProxies.Num(); ++i)
				{
					if (!Occluded[i])
					{
						VisibleProxies[Kept++] = VisibleProxies[i];
					}
				}
				VisibleProxies.SetNum(Kept);
			}
			else
			{
				OccludedMask = Occluded.data();
			}
		}
	}

	RenderStats.VisiblePrimitives = VisibleProxies.Num();

	constexpr uint32 ExtraSlots = 4096;
	const uint32 VisibleCount = VisibleProxies.Num();
	const uint32 MaxSlots = VisibleCount + ExtraSlots;
	uint8* SlotDest = Renderer ? Renderer->BeginObjectConstants(MaxSlots) : nullptr;
	uint32 NextExtraSlot = VisibleCount;

	// 조각 수 = 스레드 수 × 4. 잘게 나눠야 먼저 끝난 스레드가 남은 조각을 가져가서 부하가 고르게 된다.
	FTaskPool& Pool = FTaskPool::Get();
	const uint32 ChunkCount = FMath::Clamp(VisibleCount, 1u, Pool.GetNumThreads() * 4);

	if (GatherChunks.Num() < ChunkCount)
	{
		GatherChunks.SetNum(ChunkCount); // 늘릴 때만. 줄이지 않아야 배열 용량이 계속 재사용된다.
	}

	for (uint32 c = 0; c < GatherChunks.Num(); ++c) // 이번에 안 쓰는 조각의 묶음도 비워 둔다 (Renderer에 넘기지 않도록)
	{
		FGatherChunk& Chunk = GatherChunks[c];
		Chunk.Packets.Reset(); // 용량은 유지, 개수만 0
		Chunk.SlowPathIndices.Reset();
		std::fill(std::begin(Chunk.LODCounts), std::end(Chunk.LODCounts), 0u);
		std::fill(std::begin(Chunk.LODTriangles), std::end(Chunk.LODTriangles), 0ull);
		for (FStaticDrawGroup& Group : Chunk.Groups)
		{
			Group.Items.clear(); // 용량은 유지
		}
		Chunk.StaticDrawCount = 0;
	}

	// Renderer가 있으면 불투명 스태틱 메시는 패킷 대신 조각별 묶음에 작은 항목으로 넣는다.
	// 동일 바인딩의 개별 드로우를 묶어 패킷 복사와 항목별 정렬을 줄인다.
	const bool bStaticGroups = Renderer != nullptr;
	if (Renderer)
	{
		Renderer->ResetStaticDrawGroups();
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_GatherElements);

		LODInputs.Reset();
		if (LODView)
		{
			LODInputs.Reserve(VisibleProxies.Num());
			for (const FPrimitiveSceneProxy* Proxy : VisibleProxies)
			{
				LODInputs.Add({ Proxy->GetLODSphere(), Proxy->GetRenderState() });
			}
			SelectLODs(LODInputs, *LODView, SelectedLODs);
		}
		Pool.ParallelFor(VisibleCount, ChunkCount, [&](uint32 Begin, uint32 End, uint32 ChunkIndex)
			{
				FGatherChunk& Out = GatherChunks[ChunkIndex];      // 이 조각 전용. 다른 스레드는 절대 안 건드림
				// (머티리얼, 메시, LOD) 묶음 찾기. 조합이 몇 개뿐이라 선형 탐색이면 충분하고, 바로 전 묶음을 먼저 본다.
				const auto FindGroup = [&Out](UMaterial* Material, UStaticMesh* Mesh, uint8 LOD) -> FStaticDrawGroup&
					{
						if (Out.LastGroup < Out.Groups.size())
						{
							FStaticDrawGroup& Last = Out.Groups[Out.LastGroup];
							if (Last.Material == Material && Last.Mesh == Mesh && Last.LODIndex == LOD){
								return Last;
}
						}
						for (uint32 g = 0; g < Out.Groups.size(); ++g)
						{
							FStaticDrawGroup& Group = Out.Groups[g];
							if (Group.Material == Material && Group.Mesh == Mesh && Group.LODIndex == LOD)
							{
								Out.LastGroup = g;
								return Group;
							}
						}
						Out.LastGroup = static_cast<uint32>(Out.Groups.size());
						FStaticDrawGroup& Group = Out.Groups.emplace_back();
						Group.Material = Material;
						Group.Mesh = Mesh;
						Group.LODIndex = LOD;
						return Group;
					};

				for (uint32 VisibleIndex = Begin; VisibleIndex < End; ++VisibleIndex)
				{
					FPrimitiveSceneProxy* Proxy = VisibleProxies[VisibleIndex];   // 읽기만
					if (!Proxy->IsVisible()){ continue; }

					UStaticMesh* Mesh = Proxy->GetMesh();
					if (!Mesh)
					{
						Out.SlowPathIndices.Add(VisibleIndex);     // 컴포넌트를 건드리는 경로는 메인이 나중에
						continue;
					}

					const uint32 LOD = LODView ? SelectedLODs[VisibleIndex] : 0;
					const FCachedMeshLOD& CachedLOD = Proxy->GetLOD(LOD);
					++Out.LODCounts[LOD];                          // RenderStats 대신 조각 전용 통계

					uint32 Slot = InvalidObjectSlot;
					if (SlotDest)
					{
						// 칸 VisibleIndex는 이 반복만 쓴다 → 스레드끼리 겹치지 않음
						WritePerObjectConstants(SlotDest + size_t(VisibleIndex) * ObjectSlotBytes,
							Proxy->GetLocalToWorld(), Proxy->GetWorldToLocal(), Proxy->IsShadowReceiver());
						Slot = VisibleIndex;
					}

					const bool bOccludedByGpu = OccludedMask && OccludedMask[VisibleIndex];
					for (uint32 i = 0; i < CachedLOD.NumSections; ++i)
					{
						const FCachedMeshSection& Section = Proxy->GetSection(CachedLOD.FirstSection + i);
						Out.LODTriangles[LOD] += Section.IndexCount / 3;

						if (bStaticGroups && Section.Material && Section.Material->BlendState == EBlendState::Opaque)
						{
							FStaticDrawGroup& Group = FindGroup(Section.Material, Mesh, static_cast<uint8>(LOD));
							Group.Items.push_back({ Proxy, Slot, Section.StartIndex, Section.IndexCount, bOccludedByGpu ? 1u : 0u });
							++Out.StaticDrawCount;
							continue;
						}

						FRenderPacket& Packet = Out.Packets.AddDefaulted_GetRef();   // 조각 전용 배열에 추가
						Packet.Proxy = Proxy;
						Packet.VertexFactory = &Mesh->GetVertexFactory(LOD);
						Packet.IndexBuffer = Mesh->GetIndexBuffer(LOD);
						Packet.Material = Section.Material;
						Packet.StartIndex = Section.StartIndex;
						Packet.IndexCount = Section.IndexCount;
						Packet.LODIndex = static_cast<uint8>(LOD);
						Packet.Slot = Slot;
						Packet.bOccludedByGpu = bOccludedByGpu;
					}
				} });

		// 묶음은 World 메모리 그대로 Renderer에 넘긴다 (복사 없음). 정렬은 Renderer가 묶음 단위로 한다.
		uint32 StaticDraws = 0;
		if (Renderer)
		{
			for (uint32 c = 0; c < ChunkCount; ++c)
			{
				StaticDraws += GatherChunks[c].StaticDrawCount;
				for (const FStaticDrawGroup& Group : GatherChunks[c].Groups)
				{
					if (!Group.Items.empty())
					{
						Renderer->AddStaticDrawGroup(&Group);
					}
				}
			}
		}

		uint32 TotalPackets = 0;
		for (uint32 c = 0; c < ChunkCount; ++c)
		{
			TotalPackets += GatherChunks[c].Packets.Num();
		}

		RenderQueue.Reserve(TotalPackets + 256); // 느린 경로 몫 약간 여유
		for (uint32 c = 0; c < ChunkCount; ++c)
		{
			if (GatherChunks[c].Packets.Num() > 0)
			{
				RenderQueue.Append(GatherChunks[c].Packets); // 조각 순서대로 이어 붙이기
			}
		}

		for (uint32 c = 0; c < ChunkCount; ++c)
		{
			for (uint32 VisibleIndex : GatherChunks[c].SlowPathIndices)
			{
				FPrimitiveSceneProxy* Proxy = VisibleProxies[VisibleIndex];
				UPrimitiveComponent* Primitive = Proxy->GetComponent();
				if (!Primitive || !Primitive->IsVisible())
				{
					continue;
				}

				const uint32 FirstNew = RenderQueue.Num();

				// 동적 수집은 병렬 정적 경로가 끝난 뒤 메인 스레드에서만 실행한다.
				const bool bHandled = ResolveBillboard && GatherDynamicRenderPackets(*Proxy, RenderQueue, LODView->CameraPosition, ResolveBillboard, ViewContext, IconQueue);
				if (!bHandled)
				{
					Primitive->SubmitToRenderQueue(RenderQueue);
				}

				// continue 없이 항상 여기까지 와서 새 패킷에 여유 칸을 배정한다.
				for (uint32 p = FirstNew; p < RenderQueue.Num(); ++p)
				{
					if (!SlotDest || NextExtraSlot >= MaxSlots)
					{
						break;
					}
					FRenderPacket& Packet = RenderQueue[p];
					const FMatrix& Model = Packet.Proxy ? Packet.Proxy->GetLocalToWorld() : Packet.Model ? *Packet.Model
																										 : FMatrix::Identity;
					const FMatrix WorldToLocal = Packet.Proxy ? Packet.Proxy->GetWorldToLocal() : Model.Inverse();
					WritePerObjectConstants(SlotDest + size_t(NextExtraSlot) * ObjectSlotBytes,
						Model, WorldToLocal, Packet.Proxy && Packet.Proxy->IsShadowReceiver());
					Packet.Slot = NextExtraSlot++;
				}
			}
		}

		for (uint32 c = 0; c < ChunkCount; ++c)
		{
			for (uint32 L = 0; L < 4; ++L)
			{
				RenderStats.LODCounts[L] += GatherChunks[c].LODCounts[L];
				RenderStats.LODTriangles[L] += GatherChunks[c].LODTriangles[L];
			}
		}

		if (SlotDest)
		{
			Renderer->EndObjectConstants();
		}
		RenderStats.DrawCalls = RenderQueue.Num() + StaticDraws;
		for (uint64 T : RenderStats.LODTriangles)
		{
			RenderStats.Triangles += T;
		}
	}
}

// 지역광 그림자는 같은 World의 여러 View가 공유한다.
void FSceneGatherer::GatherLocalProjectedShadows(FScene& Scene, TArray<FProjectedShadowInfo>& OutShadows,
	const TArray<FFrustumPlanes>& ActiveViewFrustums,
	const TArray<FLODViewContext>& ActiveViews, float MinScreenRadiusSquared, bool bDrawSkeletalMesh)
{
	for (const FLocalLightSceneProxy* LightProxy : Scene.LocalLightProxies)
	{
		const ELightType LightType = LightProxy->GetLightComponent()->GetLightType();
		if ((LightType != ELightType::Point && LightType != ELightType::Spot) || !LightProxy->ShouldCastShadow())
		{
			continue;
		}
		FLightRenderParameters LightParameters{};
		LightProxy->GetLightShaderParameters(LightParameters);

		// 같은 World의 어느 활성 그림자 View에라도 영향이 있으면 유지한다.
		bool bAffectsAnyView = false;
		for (const FFrustumPlanes& ViewFrustum : ActiveViewFrustums)
		{
			if (IsSphereInFrustum(LightParameters.Position, LightParameters.Radius, ViewFrustum))
			{
				bAffectsAnyView = true;
				break;
			}
		}
		if (!bAffectsAnyView)
		{
			FStatRegistry::AddCounter(ShadowStats::STAT_LocalLightsOutsideViews);
			continue;
		}
		const bool bSpotLight = LightType == ELightType::Spot;
		FShadowProjection ShadowProjection;
		FFrustumPlanes SpotFrustum{};
		const FFrustumPlanes* ReceiverFrustum = nullptr;

		if (bSpotLight)
		{
			// 유효성 검증은 기존 라이트별 투영 계산 함수가 담당한다.
			if (!LightProxy->GetShadowProjection(ShadowProjection))
			{
				continue;
			}

			// 같은 여섯 평면을 Receiver 검사와 Caster 수집에 재사용한다.
			SpotFrustum = ExtractFrustumPlanes(ShadowProjection.ViewProjections[0]);
			ReceiverFrustum = &SpotFrustum;
		}

		// Point는 Receiver가 없으면 여섯 VP 계산도 생략한다.
		if (!HasVisibleShadowReceiver(Scene, ActiveViewFrustums, &LightParameters, ReceiverFrustum))
		{
			FStatRegistry::AddCounter(ShadowStats::STAT_LocalLightsWithoutReceivers);
			continue;
		}

		if (!bSpotLight && !LightProxy->GetShadowProjection(ShadowProjection))
		{
			continue;
		}

		const float CasterSphereRadius = GetLocalShadowCasterRadius(*LightProxy->GetLightComponent(), LightParameters, ShadowProjection.TanHalfFOV);
		// 행렬의 평면 계산 오차만 흡수하며, 그림자 Bias나 필터 폭을 바꾸지 않는다.
		const float ReceiverGuard = std::max(0.001f, 8.0f * std::numeric_limits<float>::epsilon() * (LightParameters.Position.Size() + LightParameters.Radius));

		uint32 ProjectionIndex = 0;
		for (const FMatrix& ViewProjection : ShadowProjection.ViewProjections)
		{
			const FFrustumPlanes ProjectionFrustum = bSpotLight ? SpotFrustum : ExtractFrustumPlanes(ViewProjection);

			FProjectedShadowInfo& ShadowInfo = OutShadows.AddDefaulted_GetRef();
			ShadowInfo.SetupProjection(*LightProxy, ShadowProjection.ViewOrigin,
				ViewProjection, ProjectionFrustum, FrustumAllPlanesMask, ProjectionIndex);

			// Point의 면 선택은 Normal Offset 전 WorldPos의 최대 절댓값 축으로 결정된다.
			// 옆면 4개만 검사해, Offset 후 유효해지는 Near/Far 경계 Receiver도 보존한다.
			FFrustumPlanes FaceReceiverFrustum = ProjectionFrustum;
			for (int32 PlaneIndex = 0; PlaneIndex < 4; ++PlaneIndex)
			{
				FaceReceiverFrustum.Planes[PlaneIndex].Distance += ReceiverGuard;
			}
			const uint32 FaceReceiverMask = FrustumAllPlanesMask & ~((1u << 4) | (1u << 5));
			// Spot은 위의 동일 투영 검사 결과를 재사용하고, Point는 모든 활성 View를 확인한다.
			ShadowInfo.SetHasReceivers(bSpotLight || HasVisibleShadowReceiver(Scene, ActiveViewFrustums, &LightParameters, &FaceReceiverFrustum, FaceReceiverMask));
			if (ShadowInfo.HasReceivers())
			{
				GatherShadowCasters(Scene, ShadowInfo, ActiveViews.GetData(), ActiveViews.Num(),
					MinScreenRadiusSquared, &LightParameters, CasterSphereRadius, bDrawSkeletalMesh);
			}
			// 생략한 면도 논리 슬롯을 남겨 ShadowIndex + FaceIndex 연결을 유지한다.
			++ProjectionIndex;
		}
	}
}

// 하나의 Receiver 구간마다 독립적인 Directional 작업을 추가한다.
void FSceneGatherer::GatherDirectionalShadow(FScene& Scene, TArray<FProjectedShadowInfo>& OutShadows,
	const TArray<FVector>& ReceiverCorners, const FLODViewContext& View,
	float ReceiverNearDistance, float ReceiverFarDistance,
	float MinScreenRadiusSquared, uint32 ProjectionIndex, uint32 ViewId,
	uint32 LogicalCascadeCount, bool bDrawSkeletalMesh)
{
	// 화면 조명과 동일한 Directional 하나를 사용한다.
	const FDirectionalLightSceneProxy* Directional = Scene.GetPrimaryDirectionalLight();
	if (!Directional)
	{
		return;
	}

	FProjectedShadowInfo ShadowInfo;
	if (!Directional->SetupShadowProjection(
			ReceiverCorners, ShadowInfo, ProjectionIndex, ViewId))
	{
		return;
	}

	// 투영 초기화가 끝난 뒤 실제 View의 논리 분할 개수를 기록한다.
	ShadowInfo.SetLogicalCascadeCount(LogicalCascadeCount);

	// Caster의 열린 광원 Frustum과 분리해, 실제 카메라의 Receiver 구간을 검사한다.
	FFrustumPlanes ReceiverFrustum = ExtractFrustumPlanes(View.ViewProjection);
	const float CameraDepth = FVector::Dot(View.CameraPosition, View.CameraForward);
	const float ReceiverGuard = std::max(0.001f, 8.0f * std::numeric_limits<float>::epsilon() * (View.CameraPosition.Size() + std::max(std::abs(ReceiverNearDistance), std::abs(ReceiverFarDistance))));
	// 입력 Near는 이전 Cascade의 보간 구간까지 포함한 RenderNear다.
	ReceiverFrustum.Planes[4] = { View.CameraForward, -CameraDepth - ReceiverNearDistance + ReceiverGuard };
	ReceiverFrustum.Planes[5] = { -View.CameraForward, CameraDepth + ReceiverFarDistance + ReceiverGuard };
	TArray<FFrustumPlanes> ReceiverFrustums;
	ReceiverFrustums.Add(ReceiverFrustum);
	ShadowInfo.SetHasReceivers(HasVisibleShadowReceiver(Scene, ReceiverFrustums));

	// Receiver가 있어야 Caster를 수집하되, 생략된 Cascade의 번호도 그대로 보존한다.
	if (ShadowInfo.HasReceivers())
	{
		GatherShadowCasters(Scene, ShadowInfo, &View, 1, MinScreenRadiusSquared, nullptr, 0.0f, bDrawSkeletalMesh);
	}
	OutShadows.Add(std::move(ShadowInfo));
}

// Bounds의 투영 반경을 근사하며 카메라 Frustum 가시성과는 별도로 판단한다.
bool FSceneGatherer::IsSmallShadowCaster(const FLODSphere& Sphere, const FLODViewContext* Views, int32 ViewCount, float MinScreenRadiusSquared)
{
	// 비활성 설정이나 사용할 수 없는 구 Bounds는 기존 제출 경로로 보낸다.
	if (MinScreenRadiusSquared <= 0.0f || Sphere.RadiusSquared <= 0.0f)
	{
		return false;
	}

	for (int32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex)
	{
		const FLODViewContext& View = Views[ViewIndex];

		// 기존 NDC 배율을 정규화된 화면 반경 제곱으로 변환한다.
		const float ProjectedRadiusSquared =
			0.25f * Sphere.RadiusSquared * View.ProjectionScaleSquared;

		float DistanceFactor = 1.0f;
		if (!View.bOrthographic)
		{
			DistanceFactor = (Sphere.Center - View.CameraPosition).SizeSquared();

			// 카메라가 Bounds 내부에 있으면 작은 물체로 생략하지 않는다.
			if (DistanceFactor <= Sphere.RadiusSquared)
			{
				return false;
			}
		}

		// 어느 View 하나라도 충분히 크면 공유 그림자에 남긴다.
		if (ProjectedRadiusSquared >= MinScreenRadiusSquared * DistanceFactor)
		{
			return false;
		}
	}

	// View가 없는 호출에서도 보수적으로 기존 제출을 유지한다.
	return ViewCount > 0;
}


float FSceneGatherer::GetLocalShadowCasterRadius(const ULightComponent& Light, const FLightRenderParameters& Parameters, float TanHalfFOV)
{
	const int32 Resolution = FShadowSettings::GetEffectiveResolution(Light.GetShadowMapResolution());
	if (Resolution <= 0)
	{
		return 0.0f;
	}

	const double Range = Parameters.Radius;
	const double Tan = TanHalfFOV;
	const double ProjectionRadius = Range * std::sqrt(1.0 + 2.0 * Tan * Tan);

	// 한 텍셀의 횡방향 여유와 작은 월드 공간 여유를 더한다.
	const double RasterGuard = 2.0 * 1.41421356 * Tan * Range / Resolution;
	const double Radius = ProjectionRadius + RasterGuard + 0.001;

	if (!std::isfinite(Radius) || Radius > std::numeric_limits<float>::max())
	{
		return 0.0f;
	}

	const float Result = std::nextafter(static_cast<float>(Radius), std::numeric_limits<float>::infinity());
	return std::isfinite(Result) ? Result : 0.0f;
}

// Light Frustum으로 후보를 찾고 기존 정적 메시 제출 경로를 재사용한다.
// 투영·타깃 상태는 유지하며 Caster 큐에 패킷만 추가한다.
void FSceneGatherer::GatherShadowCasters(FScene& Scene, FProjectedShadowInfo& ShadowInfo, const FLODViewContext* Views, int32 ViewCount,
	float MinScreenRadiusSquared, const FLightRenderParameters* LocalLight, float CasterSphereRadius, bool bDrawSkeletalMesh)
{
	// 한 Gather 안에서 여러 물체의 제출에 같은 임시 큐를 재사용한다.
	FRenderQueue MeshPackets;
	uint64 SmallCasterSkips = 0;
	uint64 SphereCasterSkips = 0;

	// QueryCull에서 처리할 함수
	const auto Visit = [&ShadowInfo, &MeshPackets, Views, ViewCount,
						   MinScreenRadiusSquared, &SmallCasterSkips, LocalLight,
						   CasterSphereRadius, &SphereCasterSkips, bDrawSkeletalMesh](FPrimitiveSceneProxy* Proxy)
	{
		// 그림자 생성 여부만 확인한다. ReceiveShadow는 Caster 선별과 무관하다.
		if (!Proxy->IsVisible() || !Proxy->IsShadowCaster())
		{
			return;
		}

		// 이번 범위는 정적 메시이며, 에디터 표시용 메시를 제외한다.
		auto* StaticComponent = Cast<UStaticMeshComponent>(Proxy->GetComponent());
		const bool bSkinned = Cast<USkinnedMeshComponent>(Proxy->GetComponent()) != nullptr;
		if ((!StaticComponent || StaticComponent->IsEditorIcon()) && !bSkinned)
		{
			return;
		}
		if (bSkinned && !bDrawSkeletalMesh)
		{
			return;
		}

		// BVH가 Frustum 내부 가지를 일괄 방문해도 이 검사는 수행한다.
		if (LocalLight && CasterSphereRadius > 0.0f && !IsAABBInSphere(Proxy->GetBounds(), LocalLight->Position, CasterSphereRadius))
		{
			++SphereCasterSkips;
			return;
		}

		// 월드 Sphere를 이용, 섹션마다 반복검사하지 않는다.
		if (IsSmallShadowCaster(Proxy->GetLODSphere(), Views, ViewCount, MinScreenRadiusSquared))
		{
			++SmallCasterSkips;
			return;
		}

		// 기초 그림자는 카메라와 독립적으로 LOD0를 사용한다.
		MeshPackets.Reset();
		Proxy->PrepareDynamicData();
		Proxy->GetMeshPackets(MeshPackets, 0);

		bool bPacketAdded = false;
		for (const FRenderPacket& Packet : MeshPackets)
		{
			// 불투명 섹션만 그림자 작업에 등록한다.
			// IndexCount 0은 Renderer에서 전체 인덱스 버퍼를 뜻하므로 제외한다.
			if (!Packet.Material || Packet.Material->BlendState != EBlendState::Opaque || Packet.IndexCount == 0)
			{
				continue;
			}

			ShadowInfo.AddCasterPacket(Packet);
			bPacketAdded = true;
		}

		// 헤싱으로 Shadow Caching 키를 만든다
		if (bPacketAdded)
		{
			uint32 UUID = Proxy->GetComponent()->GetUUID();
			uint64 Version = Proxy->GetShadowVersion();
			ShadowInfo.AddCasterKey(UUID, Version);
		}
	
	};


	// 전체 Scene BVH를 Light Frustum으로 탐색한다.
	const FFrustumPlanes& Frustum = ShadowInfo.GetCasterFrustum();
	Scene.BVH.QueryCull(
		ShadowInfo.GetCasterPlaneMask(),
		[&Frustum](const FBox& Bounds, uint32& Mask)
		{
			return static_cast<EBVHCullResult>(ClassifyBoxInFrustum(Bounds, Frustum, Mask));
		},
		Visit);
	if (SmallCasterSkips > 0)
	{
		FStatRegistry::AddCounter(ShadowStats::STAT_SmallCasterSkips, SmallCasterSkips);
	}
	if (SphereCasterSkips > 0)
	{
		FStatRegistry::AddCounter(ShadowStats::STAT_CasterSphereSkips, SphereCasterSkips);
	}
}


// 광원 종류와 무관한 Receiver 후보 검사이며, GPU 가림 결과는 사용하지 않는다.
bool FSceneGatherer::HasVisibleShadowReceiver(FScene& Scene, const TArray<FFrustumPlanes>& ActiveViewFrustums,
	const FLightRenderParameters* LocalLight, const FFrustumPlanes* ReceiverFrustum, uint32 ReceiverPlaneMask) const
{
	bool bHasReceiver = false;

	const auto BoundsTest = [&](const FBox& Box)
	{
		if (bHasReceiver)
		{
			return false;
		}
		// 지역광의 기존 영향 구 판정을 유지하고, Directional은 이 제한을 사용하지 않는다.
		if (LocalLight)
		{
			const FVector& Position = LocalLight->Position;
			const FVector ClosestPoint(FMath::Clamp(Position.X, Box.Min.X, Box.Max.X),
				FMath::Clamp(Position.Y, Box.Min.Y, Box.Max.Y),
				FMath::Clamp(Position.Z, Box.Min.Z, Box.Max.Z));
			if ((ClosestPoint - Position).SizeSquared() > LocalLight->Radius * LocalLight->Radius)
			{
				return false;
			}
		}
		// 영향 영역과 겹치는 Bounds가 활성 View 중 하나에서도 보일 수 있어야 한다.
		const FAABB Bounds = MakeWorldBounds(Box);

		// Spot 또는 후속 투영별 Receiver 검사에서 지정한 범위를 사용한다.
		uint32 PlaneMask = ReceiverPlaneMask;
		if (ReceiverFrustum && ClassifyBoxInFrustum(Box, *ReceiverFrustum, PlaneMask) == EFrustumTest::Outside)
		{
			return false;
		}

		for (const FFrustumPlanes& Frustum : ActiveViewFrustums)
		{
			if (IsAABBInFrustum(Bounds, Frustum))
			{
				return true;
			}
		}

		// 어떤 활성 View와도 겹치지 않는 가지는 탐색하지 않는다.
		return false;
	};
	Scene.BVH.Query(BoundsTest, [&bHasReceiver](FPrimitiveSceneProxy* Proxy)
		{
			if (!Proxy->IsVisible() || !Proxy->IsShadowReceiver())
			{
				return;
			}
			if (const auto* StaticMeshComponent = Cast<UStaticMeshComponent>(Proxy->GetComponent()))
			{
				if (StaticMeshComponent->IsEditorIcon() || !Proxy->GetMesh() || !Proxy->GetRenderState())
				{
					return;
				}
			}
			const auto* Billboard = Cast<UBillboardComponent>(Proxy->GetComponent());
			if (Billboard && Billboard->IsEditorIcon())
			{
				return;
			}
			bHasReceiver = true; });
	return bHasReceiver;
}
