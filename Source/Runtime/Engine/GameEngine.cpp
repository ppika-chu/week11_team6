#include "EnginePCH.h"
#include "Engine/GameEngine.h"

// UE의 UGameEngine처럼 게임 World 하나를 Tick하고 PlayerCameraManager의 시점으로 화면을 그린다.
// 패키징된 실행 파일(Executable 구성)이 이 엔진으로 시작한다.
#include "Asset/LOD/StaticMeshLODSelector.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Sequence/SequenceActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Core/ResourcePaths.h"
#include "Core/Stats/LightweightStats.h"
#include "Engine/DirectionalLightSceneProxy.h"
#include "Engine/SceneTransitionManager.h"
#include "Core/Window.h"
#include "GameFramework/PlayerController.h"
#include "Input/MouseCapture.h"
#include "Launch/LaunchEngineLoop.h"
#include "Render/RenderCommand.h"
#include "Render/Swapchain.h"
#include "Serialization/JsonArchive.h"
#include "UObject/UObjectIterator.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
// 명령줄에 .scene 경로가 없을 때 여는 씬. UE의 GameDefaultMap에 해당한다.
constexpr const char* DefaultScenePath{ "Assets/Scenes/Default.scene" };
constexpr uint32 MainViewId{ 1 };

// Executable.exe Assets/Scenes/Demo/02_CameraShake.scene 처럼 첫 번째 .scene 인자를 시작 씬으로 쓴다.
FString GetStartScenePath()
{
	int32 ArgumentCount{};
	LPWSTR* Arguments{ ::CommandLineToArgvW(::GetCommandLineW(), &ArgumentCount) };
	FString ScenePath{ DefaultScenePath };
	for (int32 Index{ 1 }; Arguments && Index < ArgumentCount; ++Index)
	{
		const std::filesystem::path Argument{ Arguments[Index] };
		if (Argument.extension() == L".scene")
		{
			ScenePath = Argument.generic_string();
			break;
		}
	}
	::LocalFree(Arguments);
	return ScenePath;
}
}

// 에디터 없이 World 하나를 전체 화면으로 실행하는 설정.
FEngineConfig UGameEngine::GetConfig() const
{
	FEngineConfig Config{};
	Config.Title = L"Hitori";
	Config.bBorderless = true;
	Config.SyncInterval = 1;
	// 장면은 별도 타깃에 그린 뒤 FXAA로 백버퍼에 옮기므로 백버퍼 깊이는 쓰지 않는다.
	Config.bCreateDepthBuffer = false;
	Config.bExitOnEscape = true;
	Config.SplashImage = ResourcePaths::SplashImage;
	return Config;
}

bool UGameEngine::Init()
{
	if (!Super::Init())
	{
		return false;
	}

	FRenderer* Renderer{ GetEngineLoop().GetRenderer() };
	if (!mSceneRenderer.Init(Renderer))
	{
		HTR_LOG(Error, "[Game] Failed to initialize rendering");
		return false;
	}
	mTextRenderer.Init();
	mUIRenderer.Init(mTextRenderer);
	Renderer->SetViewMode(EViewMode::Lit);

	mWorld = UWorld::CreateWorld(EWorldType::Game, false);
	if (!mWorld || !mWorld->GetPersistentLevel())
	{
		HTR_LOG(Error, "[Game] Failed to create game world");
		return false;
	}

	const FString ScenePath{ GetStartScenePath() };
	mWorld->SetScenePath(ScenePath);
	if (!FJsonArchive::LoadWorld(mWorld, ScenePath))
	{
		HTR_LOG(Error, "[Game] Failed to load scene: {}", ScenePath);
		return false;
	}

	FWorldContext& WorldContext{ CreateNewWorldContext(EWorldType::Game) };
	WorldContext.SetCurrentWorld(mWorld);
	OnResize(GetEngineLoop().GetViewportWidth(), GetEngineLoop().GetViewportHeight());
	mWorld->BeginPlay();
	HTR_LOG(Info, "[Game] Playing scene: {}", ScenePath);
	return true;
}

void UGameEngine::Tick(float DeltaTime)
{
	mWorld->Tick(DeltaTime);
	if (FSceneTransitionManager* SceneTransitions = GetSceneTransitionManager())
	{
		SceneTransitions->ProcessPendingTransition();
	}

	// 창이 포커스를 잃었다 돌아와도 잠금이 다시 걸리게 프레임마다 맞춘다.
	if (APlayerController* PlayerController{ mWorld->GetPlayerController() }; PlayerController && PlayerController->GetCursorMode() == ECursorMode::Locked)
	{
		SetGameMouseCapture(mWorld, true);
	}

	APlayerCameraManager* CameraManager{ mWorld->GetPlayerCameraManager() };
	const uint32 Width{ GetEngineLoop().GetViewportWidth() };
	const uint32 Height{ GetEngineLoop().GetViewportHeight() };
	if (CameraManager && CameraManager->HasValidCameraView() && Width > 0 && Height > 0)
	{
		FMinimalViewInfo Camera{ CameraManager->GetCameraView() };
		Camera.SetAspectRatio(static_cast<float>(Width) / static_cast<float>(Height));
		CameraManager->GetViewTargetCamera()->SetAspectRatio(Camera.GetAspectRatio());
		RenderFrame(Camera);
	}
	FStatRegistry::EndFrame();
}

void UGameEngine::OnResize(uint32 Width, uint32 Height)
{
	if (Width == 0 || Height == 0)
	{
		return;
	}

	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = Width;
	Desc.Height = Height;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	mColorTarget = RenderCommand::CreateTexture2D(Desc);

	Desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	mHDRTarget = RenderCommand::CreateTexture2D(Desc);

	Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	mDepthTarget = RenderCommand::CreateTexture2D(Desc);

	mRenderingInfo = {};
	mRenderingInfo.ViewportSetting = { 0, 0, Width, Height, 0.0f, 1.0f };
	FRenderingDesc ColorDesc{};
	ColorDesc.Texture = mHDRTarget.get();
	mRenderingInfo.ColorRenderTargets.Add(ColorDesc);
	mRenderingInfo.DepthStencil.Texture = mDepthTarget.get();
}

void UGameEngine::PreExit()
{
	if (mWorld)
	{
		if (FSceneTransitionManager* SceneTransitions = GetSceneTransitionManager())
		{
			SceneTransitions->CancelForWorld(mWorld);
		}
		mWorld->EndPlay();
		mWorld->ClearWorld();
		DestroyWorldContext(mWorld);
		mWorld = nullptr;
	}

	mRenderingInfo = {};
	mColorTarget.reset();
	mDepthTarget.reset();
	mHDRTarget.reset();
	Super::PreExit();
}

void UGameEngine::RenderFrame(const FMinimalViewInfo& Camera)
{
	FRenderer* Renderer{ GetEngineLoop().GetRenderer() };
	const FMatrix Projection{ Camera.GetProjectionMatrix() };

	FSceneView View{};
	View.ViewMatrix = Camera.GetViewMatrix();
	View.ProjectionMatrix = Projection;
	View.ViewProjection = Camera.GetViewProjectionMatrix();
	View.Location = Camera.GetLocation();
	View.Forward = Camera.GetForwardVector();
	View.NearZ = Camera.GetNearZ();
	View.FarZ = Camera.GetFarZ();
	View.bOrthographic = Camera.GetIsOrthogonal();
	View.ViewId = MainViewId;
	View.AppendFrustumCorners = &AppendShadowFrustumCorners;
	View.FrustumContext = &Camera;
	View.SceneTargets = mRenderingInfo;

	View.LODView = FLODViewContext{ GetEngineLoop().GetViewportWidth(), GetEngineLoop().GetViewportHeight() };
	View.LODView.ViewProjection = View.ViewProjection;
	View.LODView.CameraPosition = View.Location;
	View.LODView.CameraForward = View.Forward;
	View.LODView.ProjectionScaleSquared = std::max(Projection.M[1][0] * Projection.M[1][0], Projection.M[2][1] * Projection.M[2][1]);
	View.LODView.NearZ = View.NearZ;
	View.LODView.bOrthographic = View.bOrthographic;
	View.LODView.Prepare();

	mSceneRenderer.BeginRenderViews(*mWorld);
	FSceneShadowViews ShadowGroup{};
	ShadowGroup.World = mWorld;
	ShadowGroup.Views.Add(&View);
	TArray<FSceneShadowViews> ShadowViews;
	ShadowViews.Add(ShadowGroup);
	mSceneRenderer.RenderShadowDepths(ShadowViews);
	mWorld->GetScene().UpdateLights(Renderer);

	mRenderQueue.Reset();
	mIconQueue.Reset();
	mArrowQueue.Reset();
	const FFrustumPlanes Frustum{ ExtractFrustumPlanes(View.ViewProjection) };
	mSceneRenderer.GatherRenderPackets(*mWorld, mRenderQueue, &View.LODView, &Frustum, Renderer, &BuildBillboardMatrix, &Camera, &mIconQueue, &mArrowQueue);

	mSceneRenderer.RenderView(*mWorld, View, mRenderQueue, mColorTarget.get());
	mSceneRenderer.RenderFXAA(mColorTarget.get(), GetEngineLoop().GetSwapchain()->GetRenderingInfo());
	RenderText(View.ViewProjection);
}

void UGameEngine::RenderText(const FMatrix& ViewProjection)
{
	FRenderingInfo TextPass{ GetEngineLoop().GetSwapchain()->GetRenderingInfo() };
	TextPass.ColorRenderTargets[0].LoadOp = ERenderTargetLoadOp::Load;
	TextPass.DepthStencil.Texture = mDepthTarget.get();
	TextPass.DepthStencil.LoadOp = ERenderTargetLoadOp::Load;
	RenderCommand::BeginRenderPass(TextPass);
	for (TObjectIterator<UTextRenderComponent> TextComponent{}; TextComponent; ++TextComponent)
	{
		if (!TextComponent->GetFont() || !TextComponent->IsVisible() || TextComponent->GetWorld() != mWorld)
		{
			continue;
		}
		mTextRenderer.OnRender(TextComponent->GetText(), TextComponent->GetWorldMatrix(), TextComponent->GetTextSize(), *TextComponent->GetFont(), ViewProjection);
	}
	if (APlayerController * PlayerController{ mWorld->GetPlayerController() })
	{
		mUIRenderer.Render(PlayerController->GetHUD(), static_cast<float>(TextPass.ViewportSetting.Width), static_cast<float>(TextPass.ViewportSetting.Height));
	}
	RenderCommand::EndRenderPass(TextPass);
}

bool UGameEngine::SetGameMouseCapture(const UWorld* InWorld, bool Enabled)
{
	FWindow* Window{ GetEngineLoop().GetMainWindow() };
	if (!Window || !InWorld || InWorld != mWorld)
	{
		return false;
	}
	RECT Bounds{};
	::GetClientRect(Window->GetHandle(), &Bounds);
	::MapWindowPoints(Window->GetHandle(), nullptr, reinterpret_cast<POINT*>(&Bounds), 2);
	return FMouseCapture::Update(Window->GetHandle(), Bounds, Enabled);
}

FMatrix UGameEngine::BuildBillboardMatrix(const FVector& Position, float Width, float Height, const void* ViewContext)
{
	const FMinimalViewInfo& Camera{ *static_cast<const FMinimalViewInfo*>(ViewContext) };
	const FQuat CameraRotation{ Camera.GetRotation().Quaternion() };
	FVector Forward{ CameraRotation.RotateVector(FVector{ -1.0f, 0.0f, 0.0f }) };
	FVector Right{ CameraRotation.RotateVector(FVector{ 0.0f, 1.0f, 0.0f }) };
	FVector Up{ CameraRotation.RotateVector(FVector{ 0.0f, 0.0f, 1.0f }) };
	if (!Camera.GetIsOrthogonal())
	{
		const FVector ToCamera{ Camera.GetLocation() - Position };
		if (ToCamera.SizeSquared() > 1.0e-12f)
		{
			Forward = ToCamera.Normalized();
		}
		const FVector ReferenceUp{ std::fabs(Forward.Z) > 0.999f ? FVector{ 0.0f, 1.0f, 0.0f } : FVector{ 0.0f, 0.0f, 1.0f } };
		Right = FVector::Cross(ReferenceUp, Forward).Normalized();
		Up = FVector::Cross(Forward, Right).Normalized();
		Right *= -1.0f;
	}
	Right *= Width;
	Up *= Height;
	return FMatrix{
		Forward.X, Forward.Y, Forward.Z, 0.0f,
		Right.X, Right.Y, Right.Z, 0.0f,
		Up.X, Up.Y, Up.Z, 0.0f,
		Position.X, Position.Y, Position.Z, 1.0f
	};
}

bool UGameEngine::AppendShadowFrustumCorners(const void* Context, float NearDistance, float FarDistance, TArray<FVector>& OutCorners)
{
	const FMinimalViewInfo& Camera{ *static_cast<const FMinimalViewInfo*>(Context) };
	const FQuat CameraRotation{ Camera.GetRotation().Quaternion() };
	const FVector Forward{ CameraRotation.RotateVector(FVector{ 1.0f, 0.0f, 0.0f }).Normalized() };
	const FVector Right{ CameraRotation.RotateVector(FVector{ 0.0f, 1.0f, 0.0f }).Normalized() };
	const FVector Up{ CameraRotation.RotateVector(FVector{ 0.0f, 0.0f, 1.0f }).Normalized() };
	const float TanHalfFov{ std::tan(FMath::DegreesToRadians(Camera.GetFieldOfView()) * 0.5f) };
	for (const float Distance : { NearDistance, FarDistance })
	{
		const FVector Center{ Camera.GetLocation() + Forward * Distance };
		const float HalfWidth{ Camera.GetIsOrthogonal() ? Camera.GetOrthoWidth() * 0.5f : Distance * TanHalfFov * Camera.GetAspectRatio() };
		const float HalfHeight{ HalfWidth / Camera.GetAspectRatio() };
		for (const float Horizontal : { -1.0f, 1.0f })
		{
			for (const float Vertical : { -1.0f, 1.0f })
			{
				OutCorners.Add(Center + Right * (HalfWidth * Horizontal) + Up * (HalfHeight * Vertical));
			}
		}
	}
	return true;
}
