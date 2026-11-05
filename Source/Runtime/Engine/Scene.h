#pragma once

#include "PrimitiveSceneProxy.h"
#include "LightSceneProxy.h"
#include "LocalLightSceneProxy.h"
#include "DirectionalLightSceneProxy.h"
#include "Components/BillboardComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Math/Frustum.h"
#include "Math/BVH.h"
#include "Render/FogInfo.h"
#include "Render/Renderer.h"

class UDecalComponent;

struct FExponentialHeightFogSceneInfo
{
	uint32 Id;
	FFogInfo FogInfo;
};

class FScene
{
public:
	void AddPrimitive(UPrimitiveComponent* Component);
	void RemovePrimitive(UPrimitiveComponent* Component);
	// 모든 프록시를 한 번에 지운다. 액터를 통째로 지우기 전(ClearWorld)에 불러야 지워진 컴포넌트를 가리키는 프록시가 남지 않는다.
	void RemoveAllPrimitives();

	void AddLight(ULightComponent* Component);
	void RemoveLight(ULightComponent* Component);
	void AddSkyLight(USkyLightComponent* Component);
	void RemoveSkyLight(USkyLightComponent* Component);

	void RemoveAllLights();
	void UpdateLights(FRenderer* Renderer);
	const FDirectionalLightSceneProxy* GetPrimaryDirectionalLight() const;
	void AddFogInfo(uint32 Id, const FFogInfo& FogInfo);
	void RemoveFogInfo(uint32 Id);
	void RemoveAllFogInfo();
	void UpdateFogInfo(uint32 Id, const FFogInfo& FogInfo);
	void SetFogOverride(const FFogInfo& FogInfo);
	void ResetFogOverride();
	bool HasFogOverride() const;
	const FFogInfo* GetFogInfo() const;

	// 데칼은 렌더러가 매 프레임 컴포넌트에서 행렬을 읽으므로 복사본 없이 포인터만 둔다.
	void AddDecal(UDecalComponent* Component);
	void RemoveDecal(UDecalComponent* Component);
	void RemoveAllDecals();
	// 데칼 상자와 겹쳐서 Decal Pass로 다시 그릴 프록시를 모은다.
	// Broad Phase(BVH + AABB)로 후보를 추리고, Narrow Phase(OBB 대 OBB SAT)로 실제 겹침만 남긴다.
	void GatherDecalReceivers(const UDecalComponent& Decal, TArray<FPrimitiveSceneProxy*>& OutReceivers) const;
	// 받는 메시 안에서 데칼과 겹치는 삼각형의 인덱스만 OutIndices에 3개씩 붙인다 (OBBTree 질의).
	// 메시에 삼각형 BVH가 없으면 false를 돌려주고, 호출한 쪽은 메시 전체를 그린다.
	static bool GatherDecalTriangles(const UDecalComponent& Decal, const FPrimitiveSceneProxy& Receiver, TArray<uint32>& OutIndices);

	void UpdateAllTransforms();

	void BuildBVH();

	void MarkDirty(FPrimitiveSceneProxy* Proxy);
	void MarkRenderStateDirty(FPrimitiveSceneProxy* Proxy);

	TArray<FPrimitiveSceneProxy*> PrimitiveSceneProxies;
	TArray<FPrimitiveSceneProxy*> DirtyProxies;
	TArray<FPrimitiveSceneProxy*> RenderStateDirtyProxies;
	TArray<FAABB> PrimitiveBounds;
	TArray<uint8> PrimitiveFlags;

	TArray<FExponentialHeightFogSceneInfo> FogInfos;

	// 그리는 순서는 Decal Pass가 SortOrder로 정렬해서 정하므로 이 배열의 순서는 의미가 없다.
	TArray<UDecalComponent*> Decals;

	// 컬링 결과로 프록시를 바로 내보내 컴포넌트를 역참조하지 않는다. 경계는 Build/Refit 때만 계산한다.
	TBVH<FPrimitiveSceneProxy*> BVH{
		[](const FPrimitiveSceneProxy* Proxy) -> FBox
		{
			const FAABB& B = Proxy->GetBounds();
			return FBox{ B.Center - B.Extent, B.Center + B.Extent };
		}
	};
	bool bElementListChanged = false;

	TArray<FLocalLightSceneProxy*> LocalLightProxies;    // ↔ PackedIndex ↔
	TArray<FLightRenderParameters> LocalLightParameters; // GPU 업로드용, 같은 순서
	TArray<FDirectionalLightSceneProxy*> DirectionalLightProxies;
	TArray<USkyLightComponent*> SkyLights; // 프록시 없음

private:
	FFogInfo mFogOverride{};
	bool mHasFogOverride{};
};
