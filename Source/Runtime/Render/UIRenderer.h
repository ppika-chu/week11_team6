#pragma once

#include "EnginePCH.h"
#include "Text/TextRenderer.h"
#include "Text/Font.h"
#include "Render/PipelineState.h"

struct UITextInfo
{
	float mTextSize{};
	FString mText{};
	UFont* mAtlas{};
	// 화면의 비율 좌표(0~1), UI 자체의 정렬 기준점(0~1).
	FVector2 mAnchor{};
	FVector2 mAlignment{};
	FVector2 mOffset{}; // 앵커에서의 픽셀 여백
};

struct UIImageInfo
{
	FVector2 mSize{};
	UTexture2D* mTexture{};
	FVector2 mAnchor{};
	FVector2 mAlignment{};
	FVector2 mOffset{};
	float mRatio{ 1.0f }; // 0~1, 왼쪽부터 표시할 비율
};

// 게임이 준 셰이더로 화면에 사각형 하나를 그린다. UE의 UI 머티리얼에 해당한다.
// 셰이더 규약: b0 = { float2 ClipOrigin; float2 ClipSize; float2 PixelSize; float2 ViewportSize; },
//              b1 = { float4 Params[MaxParams]; }, 정점은 SV_VertexID 6개(사각형)로 만든다.
struct UIShaderInfo
{
	static constexpr int32 MaxParams{ 32 };

	FShaderProgram* mShader{};
	FVector2 mSize{};
	FVector2 mAnchor{};
	FVector2 mAlignment{};
	FVector2 mOffset{};
	FVector4 mParams[MaxParams]{};
};

// 한 프레임 동안 화면에 그릴 UI 요청. 게임에서는 PlayerController의 HUD가 가지며 World Tick마다 새로 채운다.
struct FUIDrawList
{
	TArray<UITextInfo> Texts{};
	TArray<UIImageInfo> Images{};
	TArray<UIShaderInfo> Shaders{};

	void Reset()
	{
		Texts.Reset();
		Images.Reset();
		Shaders.Reset();
	}
};

// FUIDrawList를 화면에 그리는 렌더러. 요청을 보관하지 않는다.
class FUIRenderer
{
public:
	void Init(FTextRenderer& TextRender);
	// 그리기 패스 안에서 호출한다. 이미지 → UI 셰이더 → 텍스트 순서이며 좌표 원점은 화면 왼쪽 위다.
	void Render(const FUIDrawList& DrawList, float Width, float Height);
	void DrawUIText(const UITextInfo& TextInfo, float Width, float Height);
	void DrawUIImage(const UIImageInfo& ImageInfo, float Width, float Height);
	void DrawUIShader(const UIShaderInfo& ShaderInfo, float Width, float Height);

private:
	FTextRenderer* mTextRenderer{};
	FPipelineState mUIPipeline{};
	FPipelineState mUIImagePipeline{};
	TUniquePtr<FConstantBuffer> mUIImageConstants{};
	FPipelineState mUIShaderPipeline{};
	TUniquePtr<FConstantBuffer> mUIShaderFrameConstants{};
	TUniquePtr<FConstantBuffer> mUIShaderParamConstants{};
};
