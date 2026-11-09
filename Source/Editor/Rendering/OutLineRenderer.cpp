#include "EnginePCH.h"
#include "Editor/Rendering/OutLineRenderer.h"
#include "Asset/AssetManager.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"

// 스텐실 마스크·외곽선 두 패스의 Shader와 상수 버퍼를 준비한다.
// 두 패스 모두 깊이 테스트를 끄므로 다른 물체에 가려져도 외곽선이 보인다.
void FOutlineRenderer::Init(FRenderer* InRenderer)
{
	Renderer = InRenderer;
	for (const FVertexFactoryTypeInfo& VertexFactory : GVertexFactoryTypes)
	{
		Shaders[static_cast<int32>(VertexFactory.Type)] = FRenderResourceManager::FindOrLoadShaderProgram("Resources/Shader/OutlineShader.hlsl", nullptr, VertexFactory.Type);
	}

	// 1패스: 원본 크기 메시 영역을 색 없이 스텐실에만 기록한다.
	MaskPipelineState.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	MaskPipelineState.RasterizerState = ERasterizerState::SolidNone;
	MaskPipelineState.BlendState = EBlendState::NoColorWrite;
	MaskPipelineState.DepthStencilState = EDepthStencilState::StencilMask;

	// 2패스: 확장 메시를 스텐실이 비어 있는 곳에만 그려 테두리 띠만 남긴다.
	OutlinePipelineState.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	OutlinePipelineState.RasterizerState = ERasterizerState::SolidNone;
	OutlinePipelineState.BlendState = EBlendState::Opaque;
	OutlinePipelineState.DepthStencilState = EDepthStencilState::StencilOutline;

	static_assert(sizeof(FOutlineData) == 80, "Outline constant buffer layout mismatch");
	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FOutlineData));
	PrimitiveConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FPerObjectConstants));
}

// 카메라 거리 대신 View별 픽셀 크기와 고정 확장량으로 선택 Outline을 그린다.
void FOutlineRenderer::OnRender(const FOutline& InOutline, const FMatrix& InViewProj, const FViewportSettings& Viewport)
{
	UPrimitiveComponent* Target = InOutline.GetTarget();
	const FPrimitiveSceneProxy* Proxy = Target ? Target->GetSceneProxy() : nullptr;
	if (!Proxy || Viewport.Width == 0 || Viewport.Height == 0)
	{
		return;
	}

	Packets.Reset();
	Proxy->GetMeshPackets(Packets, 0);
	if (Packets.IsEmpty())
	{
		return;
	}

	const FMatrix World = Target->GetWorldMatrix();
	const FPerObjectConstants Primitive{
		.World = World,
		.NormalMatrix = World.Inverse().GetTransposed(),
		.ReceiveShadow = 0,
	};
	RenderCommand::UpdateBufferData(PrimitiveConstantBuffer.get(), &Primitive, sizeof(Primitive));
	RenderCommand::BindConstantBuffer(2, PrimitiveConstantBuffer.get(), EShaderBindFlagBits::Vertex);
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Vertex);

	const FVector4 Viewport2(static_cast<float>(Viewport.Width), static_cast<float>(Viewport.Height), 0.0f, 0.0f);
	const auto DrawPass = [&](FPipelineState& State, float Thickness)
	{
		FOutlineData Data {
			.ViewProj = InViewProj,
			.ViewportAndThickness = FVector4(Viewport2.X, Viewport2.Y, Thickness, 0.0f),
		};
		RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Data, sizeof(Data));
		for (const FRenderPacket& Packet : Packets)
		{
			State.Shader = Shaders[static_cast<int32>(Packet.VertexFactory->Type)];
			RenderCommand::BindPipelineState(State);
			RenderCommand::BindVertexFactory(*Packet.VertexFactory);
			RenderCommand::BindIndexBuffer(Packet.IndexBuffer);
			RenderCommand::DrawIndexed(Packet.IndexCount ? Packet.IndexCount : Packet.IndexBuffer->GetIndexCount(), Packet.StartIndex);
		}
	};

	DrawPass(MaskPipelineState, 0.0f);
	DrawPass(OutlinePipelineState, 2.0f);

	// 이후 패스(Gizmo 등)에 스텐실·블렌드 상태가 새지 않도록 기본값으로 되돌린다.
	RenderCommand::SetBlendState(EBlendState::Opaque);
	RenderCommand::SetDepthStencilState(EDepthStencilState::Default);
}
