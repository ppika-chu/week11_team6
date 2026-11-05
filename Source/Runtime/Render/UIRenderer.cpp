#include "EnginePCH.h"
#include "UIRenderer.h"

#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/Texture2D.h"

#include <algorithm>

namespace
{
struct FUIImageConstants
{
	FVector2 mClipOrigin{};
	FVector2 mClipSize{};
	float mRatio{};
	float mPadding[3]{};
};
static_assert(sizeof(FUIImageConstants) == 32);

struct FUIShaderFrameConstants
{
	FVector2 mClipOrigin{};
	FVector2 mClipSize{};
	FVector2 mPixelSize{};
	FVector2 mViewportSize{};
};
static_assert(sizeof(FUIShaderFrameConstants) == 32);

FVector2 ResolveUIPosition(const FVector2& Size, const FVector2& Anchor, const FVector2& Alignment, const FVector2& Offset, float Width, float Height)
{
	return FVector2{ Width * Anchor.X + Offset.X - Size.X * Alignment.X, Height * Anchor.Y + Offset.Y - Size.Y * Alignment.Y };
}
}

void FUIRenderer::Init(FTextRenderer& TextRender)
{
	mTextRenderer = &TextRender;
	mUIPipeline = TextRender.GetPipelineState();
	mUIPipeline.RasterizerState = ERasterizerState::SolidNone;
	mUIPipeline.BlendState = EBlendState::AlphaBlend;
	mUIPipeline.DepthStencilState = EDepthStencilState::Disabled;

	mUIImagePipeline = mUIPipeline;
	mUIImagePipeline.Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/UIImageShader.hlsl");
	mUIImageConstants = RenderCommand::CreateConstantBuffer(sizeof(FUIImageConstants));

	mUIShaderPipeline = mUIPipeline;
	mUIShaderFrameConstants = RenderCommand::CreateConstantBuffer(sizeof(FUIShaderFrameConstants));
	mUIShaderParamConstants = RenderCommand::CreateConstantBuffer(sizeof(UIShaderInfo::mParams));
}

void FUIRenderer::DrawUIText(const UITextInfo& TextInfo, float Width, float Height)
{
	if (!mTextRenderer || !TextInfo.mAtlas || !TextInfo.mAtlas->AtlasTexture || TextInfo.mAtlas->Width <= 0.0f || TextInfo.mAtlas->Height <= 0.0f || TextInfo.mTextSize <= 0.0f || Width <= 0.0f || Height <= 0.0f)
	{
		return;
	}

	float MinY{};
	float MaxY{};
	float MinZ{};
	float MaxZ{};
	if (!FTextRenderer::ComputeTextBounds(TextInfo.mText, TextInfo.mTextSize, *TextInfo.mAtlas, MinY, MaxY, MinZ, MaxZ))
	{
		return;
	}

	const FVector2 TextSize{ MaxY - MinY, MaxZ - MinZ };
	const FVector2 Position{ ResolveUIPosition(TextSize, TextInfo.mAnchor, TextInfo.mAlignment, TextInfo.mOffset, Width, Height) };

	// 기존 글자 메시의 로컬 Y/Z를 화면의 X/Y 픽셀 좌표로 바꾼다.
	FMatrix UIWorld{};
	UIWorld.SetIdentity();
	for (int Row{ 0 }; Row < 3; ++Row)
	{
		for (int Col{ 0 }; Col < 3; ++Col)
		{
			UIWorld.M[Row][Col] = 0.0f;
		}
	}
	UIWorld.M[1][0] = 1.0f;
	UIWorld.M[2][1] = -1.0f;
	UIWorld.M[0][2] = 1.0f;
	UIWorld.M[3][0] = Position.X - MinY;
	UIWorld.M[3][1] = Position.Y + MaxZ;
	FMatrix UIProjection{};
	UIProjection.SetIdentity();
	UIProjection.M[0][0] = 2.0f / Width;
	UIProjection.M[1][1] = -2.0f / Height;
	UIProjection.M[3][0] = -1.0f;
	UIProjection.M[3][1] = 1.0f;
	mTextRenderer->OnRender(TextInfo.mText, UIWorld, TextInfo.mTextSize, *TextInfo.mAtlas, UIProjection, mUIPipeline);
}

void FUIRenderer::DrawUIImage(const UIImageInfo& ImageInfo, float Width, float Height)
{
	if (!mUIImagePipeline.Shader || !mUIImageConstants || !ImageInfo.mTexture || !ImageInfo.mTexture->GetResource() || ImageInfo.mSize.X <= 0.0f || ImageInfo.mSize.Y <= 0.0f || Width <= 0.0f || Height <= 0.0f || ImageInfo.mRatio <= 0.0f)
	{
		return;
	}

	const FVector2 Position{ ResolveUIPosition(ImageInfo.mSize, ImageInfo.mAnchor, ImageInfo.mAlignment, ImageInfo.mOffset, Width, Height) };
	FUIImageConstants Constants{};
	Constants.mClipOrigin = FVector2{ 2.0f * Position.X / Width - 1.0f, 1.0f - 2.0f * Position.Y / Height };
	Constants.mClipSize = FVector2{ 2.0f * ImageInfo.mSize.X / Width, -2.0f * ImageInfo.mSize.Y / Height };
	Constants.mRatio = std::clamp(ImageInfo.mRatio, 0.0f, 1.0f);

	RenderCommand::BindPipelineState(mUIImagePipeline);
	RenderCommand::UpdateBufferData(mUIImageConstants.get(), &Constants);
	RenderCommand::BindConstantBuffer(0, mUIImageConstants.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(0, ImageInfo.mTexture, EShaderBindFlagBits::Pixel);
	RenderCommand::BindSamplerState(0, ESamplerState::LinearClamp, EShaderBindFlagBits::Pixel);
	// 정점은 셰이더의 SV_VertexID로 만든다.
	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::BindIndexBuffer(nullptr);
	RenderCommand::Draw(6);
	RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);
}

void FUIRenderer::Render(const FUIDrawList& DrawList, float Width, float Height)
{
	if (Width <= 0.0f || Height <= 0.0f)
	{
		return;
	}
	for (const UIImageInfo& ImageInfo : DrawList.Images)
	{
		DrawUIImage(ImageInfo, Width, Height);
	}
	// 이미지 위에 그린다.
	for (const UIShaderInfo& ShaderInfo : DrawList.Shaders)
	{
		DrawUIShader(ShaderInfo, Width, Height);
	}
	if (mTextRenderer)
	{
		for (const UITextInfo& TextInfo : DrawList.Texts)
		{
			DrawUIText(TextInfo, Width, Height);
		}
	}
}

void FUIRenderer::DrawUIShader(const UIShaderInfo& ShaderInfo, float Width, float Height)
{
	if (!ShaderInfo.mShader || !mUIShaderFrameConstants || !mUIShaderParamConstants || ShaderInfo.mSize.X <= 0.0f || ShaderInfo.mSize.Y <= 0.0f)
	{
		return;
	}

	const FVector2 Position{ ResolveUIPosition(ShaderInfo.mSize, ShaderInfo.mAnchor, ShaderInfo.mAlignment, ShaderInfo.mOffset, Width, Height) };
	FUIShaderFrameConstants Frame{};
	Frame.mClipOrigin = FVector2{ 2.0f * Position.X / Width - 1.0f, 1.0f - 2.0f * Position.Y / Height };
	Frame.mClipSize = FVector2{ 2.0f * ShaderInfo.mSize.X / Width, -2.0f * ShaderInfo.mSize.Y / Height };
	Frame.mPixelSize = ShaderInfo.mSize;
	Frame.mViewportSize = FVector2{ Width, Height };

	mUIShaderPipeline.Shader = ShaderInfo.mShader;
	RenderCommand::BindPipelineState(mUIShaderPipeline);
	RenderCommand::UpdateBufferData(mUIShaderFrameConstants.get(), &Frame);
	RenderCommand::UpdateBufferData(mUIShaderParamConstants.get(), ShaderInfo.mParams);
	RenderCommand::BindConstantBuffer(0, mUIShaderFrameConstants.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	RenderCommand::BindConstantBuffer(1, mUIShaderParamConstants.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::BindIndexBuffer(nullptr);
	RenderCommand::Draw(6);
}
