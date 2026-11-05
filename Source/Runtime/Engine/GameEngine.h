#pragma once

#include "Engine/Engine.h"
#include "Render/SceneRenderer.h"
#include "Render/UIRenderer.h"
#include "Text/TextRenderer.h"

struct FLODViewContext;
struct FMinimalViewInfo;

// 게임 실행 엔진. 시작 씬은 명령줄의 첫 .scene 인자, 없으면 기본 씬이다.
class UGameEngine : public UEngine
{
	DECLARE_CLASS(UGameEngine, UEngine)

public:
	FEngineConfig GetConfig() const override;
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnResize(uint32 Width, uint32 Height) override;
	void PreExit() override;
	bool SetGameMouseCapture(const UWorld* InWorld, bool Enabled) override;

private:
	void RenderFrame(const FMinimalViewInfo& Camera);
	void RenderText(const FMatrix& ViewProjection);
	static FMatrix BuildBillboardMatrix(const FVector& Position, float Width, float Height, const void* ViewContext);
	// FSceneView::AppendFrustumCorners. Context는 FMinimalViewInfo다.
	static bool AppendShadowFrustumCorners(const void* Context, float NearDistance, float FarDistance, TArray<FVector>& OutCorners);

private:
	UWorld* mWorld{};
	FSceneRenderer mSceneRenderer{};
	FTextRenderer mTextRenderer{};
	FUIRenderer mUIRenderer{};
	TUniquePtr<FTexture2D> mColorTarget{};
	TUniquePtr<FTexture2D> mDepthTarget{};
	TUniquePtr<FTexture2D> mHDRTarget{};
	FRenderingInfo mRenderingInfo{};
	FRenderQueue mRenderQueue{};
	FRenderQueue mIconQueue{};
	FRenderQueue mArrowQueue{};
};
