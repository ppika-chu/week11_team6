#pragma once

#include "Engine/Engine.h"
#include "Core/Window.h"
#include "Core/Types.h"

#include "Render/Renderer.h"
#include "Render/RenderDevice.h"
#include "Render/Swapchain.h"
#include "Editor/EditorUI/ImGuiRenderer.h"
#include "Editor/Rendering/GridRenderer.h"
#include "Editor/Gizmo/GizmoRenderer.h"
#include "Components/SceneComponent.h"
#include "Render/LineBatcher.h"
#include "Render/DepthViewRenderer.h"
#include "Render/SceneRenderer.h"
#include "Render/UIRenderer.h"


#include "Editor/EditorUI/EditorUI.h"
#include "Editor/OutputLog/OutputLogPanel.h"
#include "Editor/Details/DetailsPanel.h"
#include "Editor/EditorControls/EditorControlsPanel.h"
#include "Editor/Settings/SettingsPanel.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"

#include "Editor/Rendering/Outline.h"
#include "Editor/Rendering/OutLineRenderer.h"
#include "Editor/Outliner/OutlinerPanel.h"
#include "Editor/Persona/PersonaEditorManager.h"


#include "PlayInEditorDataTypes.h"
#include "Core/Misc/Optional.h"

#include "Core/CoroutineScheduler.h"

// Temp
#include "Text/Font.h"
#include "Text/TextRenderer.h"

class UEditorEngine : public UEngine
{
	DECLARE_CLASS(UEditorEngine, UEngine)

public:
	FEngineConfig GetConfig() const override;
	bool Init() override;
	void Tick(float DeltaTime) override;
	void PreExit() override;
	FGameViewportInfo GetGameViewportInfo(const UWorld* InWorld) const override;
	void RequestQuitGame(const UWorld* InWorld) override;
	// PIE 화면(별도 창 또는 패널 View)에 마우스를 가둔다. 일시정지 중에는 놓는다.
	bool SetGameMouseCapture(const UWorld* InWorld, bool Enabled) override;
	void OnActorContentDestroying(const AActor* Owner) override;

	// Active View의 입력과 Picking 결과만 Gizmo 및 선택 상태에 반영한다.
	void UpdateGizmoAndPicking();
	// 선택한 컴포넌트의 위치·회전·스케일을 Gizmo에 넣는다. 없으면 Gizmo를 숨긴다.
	void SyncGizmoPivot();
	// Gizmo 드래그 결과를 선택한 컴포넌트에 적용한다. 라이트는 스케일 대신 반지름을 바꾼다.
	void ApplyGizmoDragResult();
	// View 하나의 장면을 FSceneRenderer로 그리고 그 위에 Grid·Gizmo·텍스트 같은 에디터 표시를 그린다.
	void RenderFrame(int32 ViewIndex, UWorld& ViewWorld, const FRenderingInfo& ViewRenderingInfo, const FSceneView& SceneView, FRenderQueue& RenderQueue, FRenderQueue& IconQueue, FRenderQueue& ArrowQueue);
	// View의 ViewportClient와 에디터 공통 표시 설정으로 FSceneView를 만든다.
	FSceneView MakeSceneView(int32 ViewIndex, const FRenderingInfo& ViewRenderingInfo) const;
	// 열려 있고 보이는 Skeletal Mesh Editor들의 프리뷰 월드를 준비하고 View를 만든다. 그림자 작업보다 먼저 부른다
	void PrepareSkeletalMeshPreviews(TArray<const FPersonaAssetEditor*>& OutPreviews, TArray<FSceneView>& OutViews);
	// 준비한 프리뷰 View를 각자의 렌더 타깃에 그린다
	void RenderSkeletalMeshPreviews(const TArray<const FPersonaAssetEditor*>& Previews, const TArray<FSceneView>& Views);
	// 네 View 결과와 ImGui를 메인 Swapchain 백버퍼에 합성한다. Present는 FEngineLoop가 한다.
	void PresentFrame();
	void DeleteActor(AActor* Actor);
	void DeleteComponent(UActorComponent* Component);
	void MakeRootComponent(USceneComponent* NewRoot);

	// Play 버튼을 눌렀을때 Play Session 실행을 요청한다.
	inline void RequestPlaySession() { bPlaySessionRequested = true; }
	inline void RequestEndPlayMap()
	{
		bRequestEndPlayMapQueued = true;
		bIsPaused = false;
	}

	// 실제 PIE 를 실행
	void StartPlayInEditorSession();
	UWorld* CreatePIEWorldByDuplication(FWorldContext& PIEContext, UWorld* InEditorWorld);

	void OnActiveWorldChanged();

	void EndPlayMap();

	bool IsPlaying() const { return PlayWorld != nullptr; }

private:
	// View 인덱스 0~3은 패널 View, PIEWindowViewIndex는 별도 PIE 창 View다.
	static constexpr int32 PIEWindowViewIndex = FViewportsPanel::ViewCount;
	static constexpr int32 MaxViewCount = PIEWindowViewIndex + 1;

	bool ExecShadowCommand(const FString& CommandLine, FString& OutMessage);
	bool ExecShadowResolutionCommand(const FString& CommandLine, FString& OutMessage);
	// 이번 프레임 DeltaTime을 패널에 전달하고 에디터 단축키를 처리한다.
	void BeginFrame(float DeltaTime);
	// 뷰포트 패널의 레이아웃과 카메라 입력을 갱신한다.
	void UpdateViewports(float DeltaTime);
	// 월드를 정확히 한 번 Tick·Capture한 뒤 에디터 상호작용을 갱신한다.
	void TickWorldAndEditor(float DeltaTime);
	// 한 번 캡처한 월드 결과를 재사용해 현재 레이아웃의 각 View를 렌더한다.
	void RenderMultipleViewports();
	// PIE View가 PlayWorld 메인 카메라로 보이도록 ViewportClient Override를 갱신하고 게임 카메라 입력을 켜고 끈다.
	void UpdatePIEViewCamera();
	// PIE 중 Esc 종료, Shift+F1 커서 해제, 뷰포트 클릭 재잠금을 처리한다.
	void UpdatePIEInput();

	// 별도 PIE 창(New Editor Window)을 만들고 정리한다.
	bool CreatePIEWindow();
	void DestroyPIEWindow();
	// 창 크기에 맞춰 Swapchain과 오프스크린 타깃을 다시 만든다. 그릴 수 없으면 false다.
	bool UpdatePIEWindowTargets();
	// PIE 창의 오프스크린 결과를 백버퍼로 복사한다. Present는 EndFrame에서 한다.
	void CopyPIEWindowToBackbuffer();
	bool IsPIEInWindow() const { return PIEWindow != nullptr; }
	// View별 FXAA 임시 텍스처. 외부 View는 PIE 창 타깃을 쓴다.
	FTexture2D* GetFxaaTargetForView(int32 ViewIndex) const;
	// View별 HDR 임시 텍스처. 외부 View는 PIE 창 타깃을 쓴다.
	FTexture2D* GetHDRTargetForView(int32 ViewIndex) const;
	FEditorViewportClient& GetViewportClient(int32 ViewIndex);
	const FEditorViewportClient& GetViewportClient(int32 ViewIndex) const;
	// 패널 View는 레이아웃이, PIE 창 View는 창 크기와 게임 카메라가 활성 여부를 정한다.
	bool IsViewActive(int32 ViewIndex) const;
	// View 카메라로 컬링·LOD 입력을 준비하고 패킷 수집은 FSceneRenderer에 맡긴다.
	void BuildRenderQueue(const FEditorViewportClient& Client, UWorld& World);
	// 화면 합성과 View 설정 보관으로 프레임을 마무리한다.
	void EndFrame();

	// Overlay Pass
	void RenderOverlayPass(const int32 ViewIndex, UWorld& ViewWorld, const FRenderingInfo& ViewRenderingInfo, const FMatrix& ViewProjection, const FVector& ViewCameraLocation, const FVector& ViewCameraForward, FRenderQueue& RenderQueue, FRenderQueue& IconQueue, FRenderQueue& ArrowQueue);
	// Depth Pass
	void RenderDepthPass(const int32 ViewIndex, const FRenderingInfo& ViewRenderingInfo, const FMatrix& ViewProjection, const FVector& ViewCameraLocation, const FVector& ViewCameraForward);

	FWorldContext& GetEditorWorldContext();
	FWorldContext* GetPIEWorldContext(int32 WorldPIEInstance = 0);
	UWorld* GetActiveWorld() const { return PlayWorld ? PlayWorld : EditorWorld; }
	// PIE를 시작한 View만 PlayWorld를, 나머지 View는 EditorWorld를 그린다.
	UWorld* GetWorldForView(int32 ViewIndex) const { return PlayWorld && ViewIndex == PIEViewIndex ? PlayWorld : EditorWorld; }
	// PIE 중이고 일시정지가 아닌 View는 Gizmo·Picking 등 에디터 상호작용을 하지 않는다.
	bool IsPlayingView(int32 ViewIndex) const { return PlayWorld && ViewIndex == PIEViewIndex && !bIsPaused; }

	// FEngineLoop 소유. OnInit에서 받아 둔다.
	FWindow* MainWindow = nullptr;
	FSwapchain* MainWindowSC = nullptr;
	FRenderer* Renderer = nullptr;

	TUniquePtr<FEditorUI> EditorUI;

	TUniquePtr<FImGuiRenderer> ImGuiRenderer;
	TUniquePtr<FGridRenderer> GridRenderer;
	TUniquePtr<FGizmoRenderer> GizmoRenderer;
	TUniquePtr<FTextRenderer> TextRenderer;
	TUniquePtr<FLineBatcher> LineBatcher;
	TUniquePtr<FGizmo> Gizmo;
	// Gizmo가 조작하는 컴포넌트. Gizmo는 대상을 모르므로 에디터가 들고 있는다.
	USceneComponent* GizmoTarget = nullptr;
	TUniquePtr<FOutline> Outline;
	TUniquePtr<FOutlineRenderer> OutlineRenderer;
	// 장면 패스(그림자·스카이·불투명·반투명·안개·톤 매핑·FXAA). Executable과 같은 렌더러를 쓴다.
	TUniquePtr<FSceneRenderer> SceneRenderer;
	TUniquePtr<FDepthViewRenderer> DepthViewRenderer;
	TUniquePtr<FUIRenderer> UIRenderer;

	UFont* SystemFont;

	FOutputLogPanel* OutputLogPanel = nullptr;

	FDetailsPanel* DetailsPanel = nullptr;
	FEditorControlsPanel* EditorControlsPanel = nullptr;
	FSettingsPanel* SettingsPanel = nullptr;
	FViewportsPanel* ViewportsPanel = nullptr;
	FRenderQueue RenderQueue;
	FRenderQueue IconQueue; // Light Icon Queue
	FRenderQueue ArrowQueue;
	FOutlinerPanel* OutlinerPanel = nullptr;
	FContentDrawerPanel* ContentDrawerPanel = nullptr;
	FPersonaEditorManager* PersonaEditorManager = nullptr;

	UWorld* EditorWorld = nullptr;
	UWorld* PlayWorld = nullptr;
	// PIE 시작 시점의 편집 View. 별도 창이면 PIEWindowViewIndex다. PIE가 아니면 InvalidViewIndex다.
	int32 PIEViewIndex = InvalidViewIndex;
	// 별도 PIE 창의 View. 패널 레이아웃·입력에 참여하지 않고 게임 카메라 Override로만 그린다.
	FEditorViewportClient PIEWindowViewportClient;

	// Play 버튼이 다음 세션을 띄울 위치
	EPlayModeType PlayMode = EPlayModeType::InViewport;

	// New Editor Window(PIE) 모드에서만 존재한다. Swapchain이 창 핸들을 쓰므로 창보다 먼저 해제한다.
	TUniquePtr<FWindow> PIEWindow;
	TUniquePtr<FSwapchain> PIESwapchain;
	TUniquePtr<FTexture2D> PIEColorTarget;
	TUniquePtr<FTexture2D> PIEDepthTarget;
	TUniquePtr<FTexture2D> PIEFxaaTarget;
	TUniquePtr<FTexture2D> PIEHDRTarget;
	FRenderingInfo PIERenderingInfo{};
	uint32 PIETargetWidth = 0;
	uint32 PIETargetHeight = 0;
	bool bPIEWindowRendered = false;

	FVector UUIDLocation;

	FEditorCommand Save;

	FEditorCommand Play;
	FEditorCommand Pause;
	FEditorCommand Resume;
	FEditorCommand StepFrame;
	FEditorCommand Stop;

	bool bPlaySessionRequested = false;
	bool bRequestEndPlayMapQueued = false;
	bool bStepRequested = false;
	bool bIsPaused = false;
	// Shift+F1로 잠금을 풀었다. 뷰포트를 클릭하면 다시 잠근다.
	bool bPIEMouseReleased = false;

	void ResetSceneSelection();
	// 입력 대상 View의 카메라를 액터 전체가 들어오게 옮긴다. PIE 게임 카메라를 쓰는 View나 다른 월드를 그리는 View는 옮기지 않는다.
	void FocusViewportOnActor(AActor* Actor);

	void CreateNewScene();
	void OpenScene();
	void SaveCurrentScene();
	void SaveSceneAs();
};
