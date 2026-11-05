#include "EnginePCH.h"
#include "Scene.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Collision/CollisionShape.h"

void FScene::AddPrimitive(UPrimitiveComponent* Component)
{
	if (!Component || Component->SceneProxy)
	{
		return;
	}

	FPrimitiveSceneProxy* Proxy = Component->CreateSceneProxy();
	Proxy->Scene = this;
	Proxy->PackedIndex = PrimitiveSceneProxies.Num();
	Component->SceneProxy = Proxy;
	MarkDirty(Proxy);
	MarkRenderStateDirty(Proxy);

	PrimitiveSceneProxies.Add(Proxy);
	PrimitiveBounds.Add(FAABB{});
	PrimitiveFlags.Add(0);

	bElementListChanged = true;
}

void FScene::RemovePrimitive(UPrimitiveComponent* Component)
{
	if (!Component || !Component->SceneProxy)
	{
		return;
	}

	FPrimitiveSceneProxy* Proxy = Component->SceneProxy;
	if (!Proxy)
	{
		return;
	}

	const uint32 Index = static_cast<uint32>(Proxy->PackedIndex);

	PrimitiveSceneProxies.RemoveAtSwap(Index);
	PrimitiveBounds.RemoveAtSwap(Index);
	PrimitiveFlags.RemoveAtSwap(Index);

	if (Index < static_cast<uint32>(PrimitiveSceneProxies.Num()))
	{
		PrimitiveSceneProxies[Index]->PackedIndex = static_cast<int32>(Index);
	}

	if (Proxy->bQueuedForUpdate)
	{
		const int32 Found = DirtyProxies.Find(Proxy); // 프로젝트 TArray의 Find 이름에 맞게
		if (Found != INDEX_NONE)
		{
			DirtyProxies.RemoveAtSwap(Found);
		}
	}

	if (Proxy->bRenderStateQueued)
	{
		const int32 Found = RenderStateDirtyProxies.Find(Proxy);
		if (Found != INDEX_NONE)
		{
			RenderStateDirtyProxies.RemoveAtSwap(Found);
		}
	}

	Component->SceneProxy = nullptr;
	delete Proxy;

	bElementListChanged = true;
}

void FScene::RemoveAllPrimitives()
{
	// 하나씩 RemovePrimitive하면 대기열 검색(Find)이 물체 수만큼 반복되므로 통째로 비운다.
	for (FPrimitiveSceneProxy* Proxy : PrimitiveSceneProxies)
	{
		if (UPrimitiveComponent* Component = Proxy->GetComponent())
		{
			Component->SceneProxy = nullptr;
		}
		delete Proxy;
	}
	PrimitiveSceneProxies.Reset();
	PrimitiveBounds.Reset();
	PrimitiveFlags.Reset();
	DirtyProxies.Reset();
	RenderStateDirtyProxies.Reset();
	BVH.Clear();
	bElementListChanged = true;
}

void FScene::AddSkyLight(USkyLightComponent* Component)
{
	if (Component && SkyLights.Find(Component) == INDEX_NONE)
	{
		SkyLights.Add(Component);
	}
}

void FScene::RemoveSkyLight(USkyLightComponent* Component)
{
	const int32 Found = SkyLights.Find(Component);
	if (Found != INDEX_NONE)
	{
		SkyLights.RemoveAtSwap(Found);
	}
}

void FScene::AddLight(ULightComponent* Component)
{
	if (!Component || Component->SceneProxy)
	{
		return;
	}

	FLightSceneProxy* Proxy = Component->CreateSceneProxy();
	if (!Proxy)
	{
		return;
	}

	Proxy->Scene = this;
	Component->SceneProxy = Proxy;

	if (Component->GetLightType() == ELightType::Directional)
	{
		Proxy->PackedIndex = DirectionalLightProxies.Num();
		DirectionalLightProxies.Add(static_cast<FDirectionalLightSceneProxy*>(Proxy));
	}
	else
	{
		Proxy->PackedIndex = LocalLightProxies.Num();
		LocalLightProxies.Add(static_cast<FLocalLightSceneProxy*>(Proxy));
		LocalLightParameters.Add(FLightRenderParameters{});
	}
}

void FScene::RemoveLight(ULightComponent* Component)
{
	if (!Component)
	{
		return;
	}

	FLightSceneProxy* Proxy = Component->SceneProxy;
	if (!Proxy)
	{
		return;
	}

	const int32 Index = Proxy->PackedIndex;
	if (Component->GetLightType() == ELightType::Directional)
	{
		DirectionalLightProxies.RemoveAtSwap(Index);
		if (Index < DirectionalLightProxies.Num())
		{
			DirectionalLightProxies[Index]->PackedIndex = Index;
		}
	}
	else
	{
		LocalLightProxies.RemoveAtSwap(Index);
		LocalLightParameters.RemoveAtSwap(Index); // 두 배열을 같이 움직여야 순서가 맞음
		if (Index < LocalLightProxies.Num())
		{
			LocalLightProxies[Index]->PackedIndex = Index;
		}
	}

	Component->SceneProxy = nullptr;
	delete Proxy;
}

void FScene::RemoveAllLights()
{
	for (FLightSceneProxy* Proxy : DirectionalLightProxies)
	{
		delete Proxy;
	}
	for (FLightSceneProxy* Proxy : LocalLightProxies)
	{
		delete Proxy;
	}
	DirectionalLightProxies.Reset();
	LocalLightProxies.Reset();
	LocalLightParameters.Reset();
	SkyLights.Reset();
}

// 등록된 라이트를 셰이더용 상수로 모아 Renderer에 올린다. 컴포넌트 값을 매 프레임 직접 읽으므로 별도 갱신 경로가 필요 없다.
void FScene::UpdateLights(FRenderer* Renderer)
{
	if (!Renderer)
	{
		return;
	}

	FLightingConstants Constants{};
	Constants.Ambient.Color = FVector(1, 1, 1);
	Constants.Ambient.Intensity = 0.0f;

	for (USkyLightComponent* Sky : SkyLights) // 기존 Sky 코드 그대로
	{
		if (!Sky->IsVisible())
		{
			continue;
		}
		const FLinearColor Color(Sky->GetLightColor());
		Constants.Ambient.Color = FVector(Color.R, Color.G, Color.B);
		Constants.Ambient.Intensity = Sky->GetIntensity();
		break;
	}

	if (const FDirectionalLightSceneProxy* Proxy = GetPrimaryDirectionalLight())
	{
		FLightRenderParameters P;
		Proxy->GetLightShaderParameters(P);
		Constants.Directional.Direction = P.Direction;
		Constants.Directional.Color = P.Color;
		Constants.Directional.Intensity = P.Intensity;
		Constants.Directional.SourceAngle = P.SourceAngle;
	}

	for (FLocalLightSceneProxy* Proxy : LocalLightProxies)
	{
		FLightRenderParameters& P = LocalLightParameters[Proxy->PackedIndex];
		Proxy->GetLightShaderParameters(P);
		P.ShadowIndex = Renderer->GetShadowIndex(*Proxy);
		if (!Proxy->IsVisible())
		{
			P.Radius = 0.0f; // 칸은 유지하고 영향만 없앤다
		}
		/*FTransform Transform;
		Transform.Rotation = Spot->GetWorldRotation();
		const FLinearColor Color(Spot->GetLightColor());

		Constants.SpotLights[Constants.NumSpotLights].Color = FVector(Color.R, Color.G, Color.B);
		Constants.SpotLights[Constants.NumSpotLights].Direction = Transform.GetForward();
		Constants.SpotLights[Constants.NumSpotLights].FalloffExponent = Spot->GetFalloff();
		Constants.SpotLights[Constants.NumSpotLights].Intensity = Spot->GetIntensity();
		Constants.SpotLights[Constants.NumSpotLights].Position = Spot->GetWorldLocation();
		Constants.SpotLights[Constants.NumSpotLights].Radius = Spot->GetAttenuationRadius();
		const float SafeOuterAngle = FMath::Clamp(Spot->GetOuterConeAngle(), 0.0f, 89.0f);
		const float SafeInnerAngle = FMath::Clamp(Spot->GetInnerConeAngle(), 0.0f, SafeOuterAngle);
		Constants.SpotLights[Constants.NumSpotLights].InnerConeCos = std::cos(FMath::DegreesToRadians(SafeInnerAngle));
		Constants.SpotLights[Constants.NumSpotLights].OuterConeCos = std::cos(FMath::DegreesToRadians(SafeOuterAngle));
		Constants.NumSpotLights++;
	}
	else if (UPointLightComponent* Point = Cast<UPointLightComponent>(Light))
	{
		const FLinearColor Color(Point->GetLightColor());
		FPointLightInfo Info{};
		Info.Position = Point->GetWorldLocation();
		Info.Radius = Point->GetAttenuationRadius();
		Info.Color = FVector(Color.R, Color.G, Color.B);
		Info.Intensity = Point->GetIntensity();
		Info.FalloffExponent = Point->GetFalloff();
		PointLights.Add(Info);
	}
	else if (URectLightComponent* Rect = Cast<URectLightComponent>(Light))
	{
		if (Constants.NumRectLights >= MaxRectLights)
			continue;

		FTransform Transform;
		Transform.Rotation = Rect->GetWorldRotation();
		const FLinearColor Color(Rect->GetLightColor());

		FRectLightInfo& Info = Constants.RectLights[Constants.NumRectLights++];
		Info.Position   = Rect->GetWorldLocation();
		Info.Radius     = Rect->GetAttenuationRadius();
		Info.Color      = FVector(Color.R, Color.G, Color.B);
		Info.Intensity  = Rect->GetIntensity();
		Info.Direction  = Transform.GetForward();
		Info.Right      = Transform.GetRight();
		Info.HalfWidth  = Rect->GetWidth() * 0.5f;
		Info.HalfHeight = Rect->GetHeight() * 0.5f;

		const float AngleRad = FMath::DegreesToRadians(FMath::Clamp(Rect->GetBarnDoorAngle(), 0.0f, 90.0f));
		Info.BarnDoorCos    = std::cos(AngleRad);
		Info.BarnDoorSin    = std::sin(AngleRad);
		Info.BarnDoorLength = std::max(Rect->GetBarnDoorLength(), 0.0f);
	}*/
	}

	Renderer->SetLighting(Constants, LocalLightParameters);
}

const FDirectionalLightSceneProxy* FScene::GetPrimaryDirectionalLight() const
{
	for (const FDirectionalLightSceneProxy* Proxy : DirectionalLightProxies)
	{
		if (Proxy->IsVisible())
		{
			return Proxy;
		}
	}
	return nullptr;
}
void FScene::AddFogInfo(uint32 Id, const FFogInfo& FogInfo)
{
	FExponentialHeightFogSceneInfo sceneFogInfo;
	sceneFogInfo.Id = Id;
	sceneFogInfo.FogInfo = FogInfo;


	FogInfos.Add(sceneFogInfo);
}

void FScene::RemoveFogInfo(uint32 Id)
{
	// 렌더러는 FogInfos[0]만 쓰므로, 순서를 유지하려고 RemoveAtSwap 대신 RemoveAt을 쓴다.
	for (int32 i = 0; i < FogInfos.Num(); ++i)
	{
		if (FogInfos[i].Id == Id)
		{
			FogInfos.RemoveAt(i);
			return;
		}
	}
}

void FScene::RemoveAllFogInfo()
{
	FogInfos.Reset();
	ResetFogOverride();
}

void FScene::AddDecal(UDecalComponent* Component)
{
	if (Component && Decals.Find(Component) == INDEX_NONE)
	{
		Decals.Add(Component);
	}
}

void FScene::RemoveDecal(UDecalComponent* Component)
{
	const int32 Found = Decals.Find(Component);
	if (Found != INDEX_NONE)
	{
		Decals.RemoveAtSwap(Found);
	}
}

void FScene::RemoveAllDecals()
{
	Decals.Reset();
}

namespace
{
// 로컬 상자에 월드 행렬을 적용한 OBB. 행렬의 0~2행은 회전된 축 × 스케일, 3행은 위치다.
Collision::FOrientedBox MakeWorldOrientedBox(const FBox& LocalBox, const FMatrix& LocalToWorld)
{
	constexpr float Epsilon = 1e-6f;
	const FVector LocalCenter = (LocalBox.Min + LocalBox.Max) * 0.5f;
	const FVector LocalExtent = (LocalBox.Max - LocalBox.Min) * 0.5f;
	const FVector DefaultAxes[3] = { FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f) };

	Collision::FOrientedBox Box;
	Box.Center = LocalToWorld.TransformPosition(LocalCenter);
	for (int32 i = 0; i < 3; ++i)
	{
		const FVector Row(LocalToWorld.M[i][0], LocalToWorld.M[i][1], LocalToWorld.M[i][2]);
		const float Scale = Row.Length();
		// 스케일이 0인 축은 방향을 알 수 없으므로 기본 축에 두께 0으로 둔다.
		Box.Axis[i] = Scale > Epsilon ? Row / Scale : DefaultAxes[i];
		Box.Extent[i] = LocalExtent[i] * Scale;
	}
	return Box;
}
} // namespace

void FScene::GatherDecalReceivers(const UDecalComponent& Decal, TArray<FPrimitiveSceneProxy*>& OutReceivers) const
{
	const FBox DecalBounds = Decal.CalcBounds();
	const Collision::FOrientedBox DecalBox = Decal.GetOrientedBox();

	// Broad Phase: 데칼의 월드 AABB와 안 겹치는 BVH 노드는 자식까지 통째로 버린다 (Pruning).
	BVH.Query(
		[&DecalBounds](const FBox& Bounds) { return Bounds.Intersects(DecalBounds); },
		[&](FPrimitiveSceneProxy* Proxy)
		{
			UPrimitiveComponent* Component = Proxy->GetComponent();
			if (!Proxy->IsVisible() || !Component || !Component->ReceivesDecals())
			{
				return;
			}

			// Decal Pass는 이 메시를 한 번 더 그리므로 GPU 메시가 없는 텍스트·빌보드·파티클은 받을 수 없다.
			UStaticMesh* Mesh = Component->GetRenderMesh();
			if (!Mesh || !Mesh->IndexBuffer)
			{
				return;
			}
			// 라이트 화살표 같은 에디터 전용 메시에는 찍지 않는다.
			if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
			{
				if (StaticMeshComponent->IsEditorIcon())
				{
					return;
				}
			}

			// Narrow Phase: 메시의 로컬 상자를 월드 OBB로 옮겨 15축 SAT로 검사한다.
			const Collision::FOrientedBox MeshBox = MakeWorldOrientedBox(Component->CalcLocalBounds(), Proxy->GetLocalToWorld());
			if (Collision::OverlapBoxBox(DecalBox, MeshBox, nullptr))
			{
				OutReceivers.Add(Proxy);
			}
		});
}

bool FScene::GatherDecalTriangles(const UDecalComponent& Decal, const FPrimitiveSceneProxy& Receiver, TArray<uint32>& OutIndices)
{
	const UPrimitiveComponent* Component = Receiver.GetComponent();
	const UStaticMesh* Mesh = Component ? Component->GetRenderMesh() : nullptr;
	if (!Mesh || !Mesh->MeshData.TriangleBVH)
	{
		return false;
	}

	const FStaticMeshData& Data = Mesh->MeshData;
	const FMatrix& LocalToWorld = Receiver.GetLocalToWorld();
	const Collision::FOrientedBox DecalBox = Decal.GetOrientedBox();

	// 피킹용 삼각형 BVH의 노드 상자는 메시 로컬 AABB라서, 월드 행렬을 곱하면 OBB가 된다 (OBBTree).
	// 데칼을 메시 로컬로 옮기지 않는 이유: 메시에 비균등 스케일과 회전이 함께 있으면 옮긴 데칼이 직육면체가 아니게 되어 SAT가 틀린다.
	Data.TriangleBVH->Query(
		[&](const FBox& LocalBounds)
		{
			return Collision::OverlapBoxBox(DecalBox, MakeWorldOrientedBox(LocalBounds, LocalToWorld), nullptr);
		},
		[&](const FMeshTriangleElement& Triangle)
		{
			OutIndices.Add(Data.Indices[Triangle.FirstIndex]);
			OutIndices.Add(Data.Indices[Triangle.FirstIndex + 1]);
			OutIndices.Add(Data.Indices[Triangle.FirstIndex + 2]);
		});
	return true;
}

void FScene::SetFogOverride(const FFogInfo& FogInfo)
{
	mFogOverride = FogInfo;
	mHasFogOverride = true;
}

void FScene::ResetFogOverride()
{
	mFogOverride = {};
	mHasFogOverride = false;
}

bool FScene::HasFogOverride() const
{
	return mHasFogOverride;
}

const FFogInfo* FScene::GetFogInfo() const
{
	if (mHasFogOverride)
	{
		return &mFogOverride;
	}
	return FogInfos.IsEmpty() ? nullptr : &FogInfos[0].FogInfo;
}

void FScene::UpdateFogInfo(uint32 Id, const FFogInfo& FogInfo)
{
	for (FExponentialHeightFogSceneInfo& SceneFogInfo : FogInfos)
	{
		if (SceneFogInfo.Id == Id)
		{
			SceneFogInfo.FogInfo = FogInfo;
			return;
		}
	}
}


void FScene::UpdateAllTransforms()
{
	for (FPrimitiveSceneProxy* Proxy : RenderStateDirtyProxies)
	{
		Proxy->UpdateRenderState();
		Proxy->bRenderStateQueued = false;
	}
	RenderStateDirtyProxies.Reset();

	for (FPrimitiveSceneProxy* Proxy : DirtyProxies)
	{
		Proxy->UpdateTransform();
		PrimitiveBounds[Proxy->PackedIndex] = Proxy->GetBounds();
		PrimitiveFlags[Proxy->PackedIndex] = Proxy->GetComponent()->IsVisible() ? 1 : 0;
		Proxy->bQueuedForUpdate = false;
	}

	const bool bAnyMoved = DirtyProxies.Num() > 0;
	DirtyProxies.Reset();


	if (bElementListChanged)
	{
		BuildBVH();
		bElementListChanged = false;
	}
	else if (bAnyMoved)
	{
		BVH.Refit();
	}
}

void FScene::BuildBVH()
{
	BVH.Clear();

	TArray<FPrimitiveSceneProxy*> Elements;
	Elements.Reserve(PrimitiveSceneProxies.Num());
	for (FPrimitiveSceneProxy* Proxy : PrimitiveSceneProxies)
	{
		if (Proxy && Proxy->GetComponent())
		{
			Elements.Add(Proxy);
		}
	}

	BVH.Build(std::span(Elements.GetData(), Elements.Num()));
}

void FScene::MarkDirty(FPrimitiveSceneProxy* Proxy)
{
	if (Proxy->bQueuedForUpdate)
	{
		return; // 중복 추가 방지
	}
	Proxy->bQueuedForUpdate = true;
	DirtyProxies.Add(Proxy);
}

void FScene::MarkRenderStateDirty(FPrimitiveSceneProxy* Proxy)
{
	if (!Proxy || Proxy->bRenderStateQueued)
	{
		return;
	}
	Proxy->bRenderStateQueued = true;
	RenderStateDirtyProxies.Add(Proxy);
}
