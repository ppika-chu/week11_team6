#pragma once

#include "Render/Renderer.h"
#include "Render/PipelineState.h"
#include "Editor/Rendering/Outline.h"

// Outline의 월드·투영 행렬과 View별 픽셀 크기·확장 두께를 담는다.
struct FOutlineData
{
	FMatrix ViewProj;
	FVector4 ViewportAndThickness;
};

class FOutlineRenderer
{
public:
	// 스텐실 마스크·외곽선 두 패스의 Shader와 상수 버퍼를 준비한다.
	void Init(FRenderer* InRenderer);
	// 원본 메시로 스텐실을 찍은 뒤, 확장 메시를 그 바깥에만 그려 외곽선만 남긴다.
	void OnRender(const FOutline& InOutline, const FMatrix& ViewProj, const FViewportSettings& Viewport);

private:
	FRenderer* Renderer;

	FShaderProgram* Shaders[static_cast<int32>(EVertexFactoryType::Count)]{};
	FPipelineState MaskPipelineState;
	FPipelineState OutlinePipelineState;

	TUniquePtr<FConstantBuffer> ConstantBuffer;
	TUniquePtr<FConstantBuffer> PrimitiveConstantBuffer; // b2
	FRenderQueue Packets;
};
