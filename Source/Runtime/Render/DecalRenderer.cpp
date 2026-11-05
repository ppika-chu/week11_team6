#include "EnginePCH.h"
#include "DecalRenderer.h"
#include "RenderCommand.h"
#include "RenderResourceManager.h"
#include "Asset/AssetManager.h"
#include "Render/Material.h"
#include "Engine/Scene.h"
#include "Components/DecalComponent.h"
#include "Core/Stats/DecalStats.h"
#include "Math/Frustum.h"

#include <algorithm>

bool FDecalRenderer::Init()
{
	// UberLit처럼 같은 파일을 조명 모델 매크로별로 따로 컴파일한다.
	static const D3D_SHADER_MACRO Unlit[] = { { "LIGHTING_MODEL_UNLIT", "1" }, { nullptr, nullptr } };
	static const D3D_SHADER_MACRO Lambert[] = { { "LIGHTING_MODEL_LAMBERT", "1" }, { nullptr, nullptr } };
	static const D3D_SHADER_MACRO BlinnPhong[] = { { "LIGHTING_MODEL_BLINN_PHONG", "1" }, { nullptr, nullptr } };

	const FString Path = "Resources/Shader/DecalShader.hlsl";
	FShaderProgram* LambertShader = FRenderResourceManager::FindOrLoadShaderProgram(Path, Lambert);
	Shaders[static_cast<int32>(EViewMode::Lit_Gouraud)] = LambertShader;
	Shaders[static_cast<int32>(EViewMode::Lit_Lambert)] = LambertShader;
	Shaders[static_cast<int32>(EViewMode::Lit_BlinnPhong)] = FRenderResourceManager::FindOrLoadShaderProgram(Path, BlinnPhong);
	Shaders[static_cast<int32>(EViewMode::Lit)] = Shaders[static_cast<int32>(EViewMode::Lit_BlinnPhong)];
	Shaders[static_cast<int32>(EViewMode::Unlit)] = FRenderResourceManager::FindOrLoadShaderProgram(Path, Unlit);
	if (!LambertShader)
	{
		HTR_LOG(Error, "[Decal] shader not found");
		return false;
	}

	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FDecalConstants));

	PipelineState.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	PipelineState.RasterizerState = ERasterizerState::DecalDepthBias;
	PipelineState.DepthStencilState = EDepthStencilState::ReadOnly;
	PipelineState.BlendState = EBlendState::AlphaBlend;

	return true;
}

void FDecalRenderer::EnsureTriangleIndexCapacity(uint32 Count)
{
	if (TriangleIndexBuffer && Count <= TriangleIndexCapacity)
	{
		return;
	}

	uint32 NewCapacity = std::max(TriangleIndexCapacity, 3072u);
	while (NewCapacity < Count)
	{
		NewCapacity *= 2;
	}
	TriangleIndexBuffer = RenderCommand::CreateDynamicIndexBuffer(NewCapacity);
	TriangleIndexCapacity = TriangleIndexBuffer ? NewCapacity : 0;
}

void FDecalRenderer::OnRender(const FScene& Scene, EViewMode ViewMode, const FFrustumPlanes& Frustum)
{
	SCOPE_CYCLE_COUNTER(DecalStats::STAT_PassCPU);
	FStatRegistry::AddCounter(DecalStats::STAT_SceneDecals, Scene.Decals.Num());
	const int32 ModeIndex = static_cast<int32>(ViewMode);
	FShaderProgram* Shader = ModeIndex >= 0 && ModeIndex < static_cast<int32>(EViewMode::Count) ? Shaders[ModeIndex] : nullptr;
	if (Scene.Decals.IsEmpty() || !Shader)
	{
		return;
	}
	PipelineState.Shader = Shader;

	// Additive(빛)는 맨 뒤에 그린다. 빛을 먼저 더하면 그 위의 Alpha Blend 데칼이 빛을 덮는다.
	// Additive끼리는 더하는 순서가 결과를 바꾸지 않는다.
	auto IsAdditive = [](const UDecalComponent* Decal)
	{
		const UMaterial* Material = Decal->GetDecalMaterial();
		return Material && Material->BlendState == EBlendState::Additive;
	};
	SortedDecals = Scene.Decals;
	std::stable_sort(SortedDecals.begin(), SortedDecals.end(),
		[&IsAdditive](const UDecalComponent* A, const UDecalComponent* B)
		{
			const bool bAdditiveA = IsAdditive(A);
			const bool bAdditiveB = IsAdditive(B);
			if (bAdditiveA != bAdditiveB)
			{
				return bAdditiveB;
			}
			return A->GetSortOrder() < B->GetSortOrder();
		});

	FDecalConstants Constants;

	bool bPipelineBound = false;
	EBlendState CurrentBlend = PipelineState.BlendState;
	for (UDecalComponent* Decal : SortedDecals)
	{
		UMaterial* Material = Decal->GetDecalMaterial();
		if (!Material || Material->Textures.IsEmpty() || !Material->Textures[0])
		{
			continue;
		}

		// 페이드 인 전이거나 페이드 아웃이 끝난 데칼은 수신자를 찾을 필요도 없다.
		if (Decal->GetFadeAlpha() <= 0.0f)
		{
			continue;
		}

		// 상자가 화면 밖이면 데칼이 찍힌 픽셀도 화면에 없다.
		if (!IsAABBInFrustum(MakeWorldBounds(Decal->CalcBounds()), Frustum))
		{
			continue;
		}

		Receivers.Reset();
		{
			SCOPE_CYCLE_COUNTER(DecalStats::STAT_ReceiverCPU);
			Scene.GatherDecalReceivers(*Decal, Receivers);
		}
		if (Receivers.IsEmpty())
		{
			continue;
		}

		FStatRegistry::AddCounter(DecalStats::STAT_RenderedDecals);
		FStatRegistry::AddCounter(DecalStats::STAT_Receivers, Receivers.Num());

		if (!bPipelineBound)
		{
			RenderCommand::BindPipelineState(PipelineState);
			RenderCommand::BindSamplerState(0, ESamplerState::LinearClamp, EShaderBindFlagBits::Pixel);
			RenderCommand::BindConstantBuffer(3, ConstantBuffer.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
			bPipelineBound = true;
		}

		const EViewMode MaterialMode = ResolveMaterialViewMode(ViewMode, Material->ShadingModel);
		if (FShaderProgram* MaterialShader = Shaders[static_cast<int32>(MaterialMode)]; MaterialShader && MaterialShader != Shader)
		{
			RenderCommand::BindShaderProgram(MaterialShader);
			Shader = MaterialShader;
		}

		const bool bAdditive = Material->BlendState == EBlendState::Additive;
		const EBlendState Blend = bAdditive ? EBlendState::Additive : EBlendState::AlphaBlend;
		if (Blend != CurrentBlend)
		{
			RenderCommand::SetBlendState(Blend);
			CurrentBlend = Blend;
		}

		RenderCommand::BindShaderResource(0, Material->Textures[0], EShaderBindFlagBits::Pixel);

		// 노멀맵이 없으면 평평한 노멀을 묶는다. 그러지 않으면 앞서 그린 메시의 노멀맵이 t1에 남아 있다.
		UTexture2D* NormalTexture = Material->Textures.Num() > 1 ? Material->Textures[1] : nullptr;
		if (!NormalTexture)
		{
			NormalTexture = UAssetManager::GetAssetByPath<UTexture2D>("FlatNormalTexture");
		}
		RenderCommand::BindShaderResource(1, NormalTexture, EShaderBindFlagBits::Pixel);

		const FVector4 ComponentColor(Decal->GetDecalColor());
		Constants.DecalColor = FVector4(
			Material->DiffuseOrFactor.X * ComponentColor.X,
			Material->DiffuseOrFactor.Y * ComponentColor.Y,
			Material->DiffuseOrFactor.Z * ComponentColor.Z,
			Material->DiffuseOrFactor.W * ComponentColor.W);
		Constants.DecalVP = Decal->GetDecalViewProjection();
		Constants.FadeAlpha = Decal->GetFadeAlpha();
		Constants.bAdditive = bAdditive ? 1.0f : 0.0f;
		Constants.Shininess = Material->Shininess;
		Constants.SpecularColor = Material->SpecularColor;
		Constants.SpecularFactor = Material->SpecularFactor;
		Constants.EmissiveColor = Material->EmissiveColor;
		Constants.EmissiveFactor = Material->EmissiveFactor;

		for (FPrimitiveSceneProxy* Receiver : Receivers)
		{
			UStaticMesh* Mesh = Receiver->GetComponent()->GetRenderMesh();

			Constants.World = Receiver->GetLocalToWorld();
			// 노멀은 (World^-1)^T로 옮긴다. 프록시가 역행렬을 들고 있으니 전치만 한다.
			Constants.WorldInverseTranspose = Receiver->GetWorldToLocal().GetTransposed();
			Constants.ReceiveShadow = Receiver->IsShadowReceiver() ? 1u : 0u;
			RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Constants, sizeof(Constants));
			RenderCommand::BindVertexBuffer(Mesh->GetVertexBuffer(0));

			// OBBTree로 데칼과 겹치는 삼각형만 골라 그린다. 삼각형 BVH가 없는 메시는 통째로 그린다.
			TriangleIndices.Reset();
			bool bGatheredTriangles = false;
			{
				SCOPE_CYCLE_COUNTER(DecalStats::STAT_TriangleCPU);
				bGatheredTriangles = FScene::GatherDecalTriangles(*Decal, *Receiver, TriangleIndices);
			}
			if (bGatheredTriangles)
			{
				if (TriangleIndices.IsEmpty())
				{
					continue;
				}
				const uint32 IndexCount = static_cast<uint32>(TriangleIndices.Num());
				EnsureTriangleIndexCapacity(IndexCount);
				if (!TriangleIndexBuffer)
				{
					continue;
				}
				RenderCommand::UpdateBufferData(TriangleIndexBuffer.get(), TriangleIndices.GetData(), sizeof(uint32) * IndexCount);
				RenderCommand::BindIndexBuffer(TriangleIndexBuffer.get());
				RenderCommand::DrawIndexed(IndexCount);
				FStatRegistry::AddCounter(DecalStats::STAT_DrawCalls);
				FStatRegistry::AddCounter(DecalStats::STAT_Triangles, IndexCount / 3);
			}
			else
			{
				FIndexBuffer* IndexBuffer = Mesh->GetIndexBuffer(0);
				RenderCommand::BindIndexBuffer(IndexBuffer);
				RenderCommand::DrawIndexed(IndexBuffer->GetIndexCount());
				FStatRegistry::AddCounter(DecalStats::STAT_DrawCalls);
				FStatRegistry::AddCounter(DecalStats::STAT_Triangles, IndexBuffer->GetIndexCount() / 3);
			}
		}
	}

	if (bPipelineBound)
	{
		RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);
		RenderCommand::UnbindShaderResource(1, EShaderBindFlagBits::Pixel);
		RenderCommand::SetBlendState(EBlendState::Opaque);
		RenderCommand::SetDepthStencilState(EDepthStencilState::Default);
		RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
	}
}
