#include "EnginePCH.h"

#include "Editor/HitoriEd/EditorEngine.h"

#include "Core/EngineStatics.h"
#include "Core/EngineTimer.h"
#include "Launch/LaunchEngineLoop.h"
#include "Core/StatOverlay.h"
#include "Input/InputSystem.h"


#include "Render/GeometryGenerator.h"

#include "Engine/World.h"
#include "Engine/Level.h"

#include "Render/Renderer.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
// 피킹 결과와 View별 Billboard 행렬 공급에 사용한다.
#include "Collision/HitResult.h"
#include "Components/BillboardComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor/LightActorBase.h"

#include "Asset/AssetManager.h"
#include "Animation/AnimSequence.h"
#include "Render/SkeletalMesh.h"
#include "Render/RenderResourceManager.h"

#include "Render/RenderCommand.h"
#include "Editor/Outliner/OutlinerPanel.h"
#include "Editor/Sequence/SequencePanel.h"
#include "Sequence/SequenceActor.h"
#include "Editor/HitoriEd/EditorFileUtils.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "Core/ResourcePaths.h"
#include "UObject/UObjectIterator.h"

#include "Core/EngineLog.h"
#include "Core/Stats/LightweightStats.h"

#include <utility>
#include <algorithm>
#include "Components/DirectionalLightComponent.h"
#include "Engine/SceneTransitionManager.h"
#include "GameFramework/PlayerController.h"
#include "Input/MouseCapture.h"

#include "Core/CoroutineScheduler.h"
#include <sol/sol.hpp>
#include "Components/ShapeComponent.h"
#include "Components/DecalComponent.h"
#include "Components/LocalLightComponent.h"

namespace
{
DECLARE_CYCLE_STAT("Viewport Update", STAT_ViewportUpdate);
DECLARE_CYCLE_STAT("World Tick", STAT_WorldTick);
DECLARE_CYCLE_STAT("Editor Tick", STAT_EditorTick);
DECLARE_CYCLE_STAT("Prepare Render Views", STAT_PrepareRenderViews);
DECLARE_CYCLE_STAT("Build Render Queue", STAT_BuildRenderQueue);
DECLARE_CYCLE_STAT("ImGui", STAT_ImGui);

// Radius가 있는 라이트(Point, Spot, Rect) 액터의 컴포넌트면 그 라이트. Gizmo 스케일을 반지름으로 바꿀 때 쓴다.
ULocalLightComponent* GetGizmoTargetLight(const USceneComponent* Target)
{
	ALightActorBase* Actor = Target ? Cast<ALightActorBase>(Target->GetOwner()) : nullptr;
	return Actor ? Cast<ULocalLightComponent>(Actor->GetLightComponent()) : nullptr;
}
}

#include "Core/SplashScreen.h"

FEngineConfig UEditorEngine::GetConfig() const
{
	FEngineConfig Desc;
	Desc.Title = L"Hitori Engine";
	Desc.Width = 1920;
	Desc.Height = 1080;
	Desc.bBorderless = false;
	Desc.SyncInterval = 0;
	// View는 각자 깊이 버퍼를 쓰고 백버퍼에는 ImGui만 그린다.
	Desc.bCreateDepthBuffer = false;
	Desc.bExitOnEscape = false;
	// 파일이 없으면 검은 배경에 상태 텍스트만 표시된다.
	Desc.SplashImage = ResourcePaths::SplashImage;
	return Desc;
}

// 렌더 자원·월드·에디터와 MultipleViewports 연결을 초기화한다.
// Device·Window·Swapchain·AssetManager는 FEngineLoop가 먼저 만들어 둔다.
bool UEditorEngine::Init()
{
	if (!Super::Init())
	{
		return false;
	}

	FWorldContext& InitialWorldContext = CreateNewWorldContext(EWorldType::Editor);
	InitialWorldContext.SetCurrentWorld(UWorld::CreateWorld(EWorldType::Editor, true));
	EditorWorld = InitialWorldContext.World();

	MainWindow = GetEngineLoop().GetMainWindow();
	MainWindowSC = GetEngineLoop().GetSwapchain();
	Renderer = GetEngineLoop().GetRenderer();
	FRenderDevice* RenderDevice = GetEngineLoop().GetRenderDevice();

	EditorUI = MakeUnique<FEditorUI>();
	EditorUI->Init();

	EditorUI->SetNewSceneCallback([this]()
		{ CreateNewScene(); });
	EditorUI->SetOpenSceneCallback([this]()
		{ OpenScene(); });
	EditorUI->SetSaveSceneCallback([this]()
		{ SaveCurrentScene(); });
	EditorUI->SetSaveSceneAsCallback([this]()
		{ SaveSceneAs(); });

	OutputLogPanel = EditorUI->AddEditorPanel<FOutputLogPanel>();
	FLog::AddSink(OutputLogPanel);

	// 로그 패널이 생긴 뒤에 돌려야 결과가 Output Log에 보인다.
	OutputLogPanel->SetCommandHandler(
		[this](const FString& CommandLine, FString& OutMessage)
		{
			if (ExecShadowCommand(CommandLine, OutMessage))
			{
				return true;
			}

			if (ExecShadowResolutionCommand(CommandLine, OutMessage))
			{
				return true;
			}

			return false;
		});
	HTR_LOG(Info, "Editor Initialize...");

	HTR_LOG(Info, "Initialize ImGui...");
	ImGuiRenderer = MakeUnique<FImGuiRenderer>();
	if (!ImGuiRenderer->Init(MainWindow->GetHandle(), RenderDevice->GetDevice(), RenderDevice->GetContext()))
	{
		HTR_LOG(Error, "Failed To Initialize ImGui!");
	}
	HTR_LOG(Info, "Initialize ImGui Success!");

	GridRenderer = MakeUnique<FGridRenderer>();
	GridRenderer->Init(Renderer);

	GizmoRenderer = MakeUnique<FGizmoRenderer>();
	GizmoRenderer->Init(Renderer);

	Gizmo = MakeUnique<FGizmo>();

	SceneRenderer = MakeUnique<FSceneRenderer>();
	SceneRenderer->Init(Renderer);

	DepthViewRenderer = MakeUnique<FDepthViewRenderer>();
	DepthViewRenderer->Init();


	// 필요한 Panel들 추가후 raw pointer 반환(소유권 = EditorUI)
	DetailsPanel = EditorUI->AddEditorPanel<FDetailsPanel>();
	// 패널에는 조회 대상만 전달하고, 그림자 작업이나 타깃을 보관하지 않는다.
	DetailsPanel->SetRenderer(Renderer);

	DetailsPanel->SetShadowCascadeDebugCallbacks(
		[this](uint32 ViewId)
		{
			if (ViewId == 0 || ViewId > MaxViewCount)
			{
				return false;
			}

			return GetViewportClient(static_cast<int32>(ViewId - 1)).GetShowFlags().IsEnabled(EEngineShowFlag::ShadowCascades);
		},
		[this](uint32 ViewId, bool bEnabled)
		{
			if (ViewId == 0 || ViewId > MaxViewCount)
			{
				return;
			}

			// 다른 View 플래그는 유지하고 색상 표시만 변경한다.
			FEditorViewportClient& Client = GetViewportClient(static_cast<int32>(ViewId - 1));
			FEngineShowFlags Flags = Client.GetShowFlags();
			Flags.SetEnabled(EEngineShowFlag::ShadowCascades, bEnabled);
			Client.SetShowFlags(Flags);
		});

	EditorControlsPanel = EditorUI->AddEditorPanel<FEditorControlsPanel>();
	ViewportsPanel = EditorUI->AddEditorPanel<FViewportsPanel>();
	ContentDrawerPanel = EditorUI->AddEditorPanel<FContentDrawerPanel>();
	PersonaEditorManager = EditorUI->AddEditorPanel<FPersonaEditorManager>();
	// 콘텐츠 드로어에서 Skeletal Mesh를 더블클릭하면 뷰어로 연다 (언리얼 콘텐츠 브라우저 더블클릭)
	ContentDrawerPanel->SetOpenAssetCallback([this](URenderAsset* Asset)
		{
			if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset))
			{
				PersonaEditorManager->OpenEditor(SkeletalMesh);
			}
			else if (UAnimSequence* Sequence = Cast<UAnimSequence>(Asset))
			{
				PersonaEditorManager->OpenEditor(Sequence);
			}
		});
	FSequencePanel* SequencePanel = EditorUI->AddEditorPanel<FSequencePanel>();
	SequencePanel->SetSequenceActorProvider([this]() -> ASequenceActor*
		{
		ASequenceActor* Actor = Cast<ASequenceActor>(OutlinerPanel->GetSelectedActor());
		return Actor && Actor->GetWorld() == EditorWorld && !Actor->IsActorBeingDestroyed() ? Actor : nullptr; });
	SequencePanel->SetCreateSequenceActorCallback([this]()
		{
		if (IsPlaying() || !EditorWorld){ return;
}
		if (ASequenceActor* Actor = EditorWorld->SpawnActor<ASequenceActor>()){
			OutlinerPanel->SelectActor(Actor);
} });
	SequencePanel->SetViewportCameraProvider([this](CameraKey& OutKey)
		{
		int32 ViewIndex = ViewportsPanel->GetActiveViewIndex();
		if (!ViewportsPanel->IsViewActive(ViewIndex)){
			ViewIndex = ViewportsPanel->GetEditorViewIndex();
}
		if (!ViewportsPanel->IsViewActive(ViewIndex)){
			return false;
}

		const FViewCamera& Camera = ViewportsPanel->GetViewportClient(ViewIndex).GetEffectiveCamera();
		OutKey.Location = Camera.Transform.Location;
		OutKey.Rotation = Camera.Transform.Rotation.ToFRotator();
		OutKey.FieldOfView = Camera.Projection.FovDegrees;
		return true; });

	// OutLine
	OutlineRenderer = MakeUnique<FOutlineRenderer>();
	OutlineRenderer->Init(Renderer);

	SettingsPanel = EditorUI->AddEditorPanel<FSettingsPanel>();

	Outline = MakeUnique<FOutline>();

	SystemFont = UAssetManager::GetAssetByPath<UFont>(ResourcePaths::DefaultFont);

	TextRenderer = MakeUnique<FTextRenderer>();
	TextRenderer->Init();

	UIRenderer = MakeUnique<FUIRenderer>();
	UIRenderer->Init(*TextRenderer);

	// 기본 카메라 투영으로 네 View를 초기화한다. 저장된 레이아웃·카메라는 SettingsPanel이 연결될 때 적용한다.
	const FMinimalViewInfo& DefaultView{ GetActiveWorld()->GetPlayStartView() };
	ViewportsPanel->InitializeViewports({ EProjectionMode::Perspective,
		DefaultView.GetFieldOfView(),
		DefaultView.GetOrthoWidth(),
		DefaultView.GetNearZ(),
		DefaultView.GetFarZ() });

	OutlinerPanel = EditorUI->AddEditorPanel<FOutlinerPanel>();
	OutlinerPanel->SetWorld(GetActiveWorld());
	OutlinerPanel->SetSelectionCallback(
		[this](USceneComponent* Root)
		{
			GizmoTarget = Root;
			DetailsPanel->SetTarget(Root ? Root->GetOwner() : nullptr);

			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Root);
			if (!Primitive && Root && Root->GetOwner())
			{
				for (UActorComponent* C : Root->GetOwner()->GetComponents())
				{
					if ((Primitive = Cast<UPrimitiveComponent>(C)))
					{
						break;
					}
				}
			}
			Outline->SetTarget(Primitive);
		});
	DetailsPanel->SetSelectionCallback(
		[this](UActorComponent* Component)
		{
			// 액터 바운드 피벗(루트일 때)은 쓰이지 않던 기능이라 컴포넌트 위치만 쓴다.
			GizmoTarget = Cast<USceneComponent>(Component);
		});

	OutlinerPanel->SetDeleteActorCallback(
		[this](AActor* Actor)
		{
			DeleteActor(Actor);
		});
	OutlinerPanel->SetActorDoubleClickCallback(
		[this](AActor* Actor)
		{
			FocusViewportOnActor(Actor);
		});
	DetailsPanel->SetDeleteComponentCallback(
		[this](UActorComponent* Component)
		{
			DeleteComponent(Component);
		});
	DetailsPanel->SetMakeRootCallback(
		[this](USceneComponent* NewRoot)
		{
			MakeRootComponent(NewRoot);
		});

	LineBatcher = MakeUnique<FLineBatcher>();
	LineBatcher->Init(Renderer, GetActiveWorld());

	DetailsPanel->SetWorld(GetActiveWorld());

	EditorControlsPanel->SetWorld(GetActiveWorld());
	EditorControlsPanel->SetIsPlayingQuery([this]()
		{ return IsPlaying(); });
	EditorControlsPanel->SetKeyboardBlockedQuery([this]()
		{ return PersonaEditorManager->HasFocusedEditor(); });
	EditorControlsPanel->SetGizmo(Gizmo.get());
	EditorControlsPanel->SetViewportsPanel(ViewportsPanel);

	SettingsPanel->SetWorld(GetActiveWorld());
	SettingsPanel->SetViewportsPanel(ViewportsPanel);

	const ImVec4 White = { 1, 1, 1, 1 };
	const ImVec4 Green = { 0.40f, 0.80f, 0.30f, 1.0f };

	// Resources는 시작할 때 스캔하지 않으므로 툴바 아이콘은 경로로 직접 불러온다.
	// Save 아이콘 파일은 아직 없어서 아이콘 없이 둔다 (이전에도 찾지 못해 nullptr였다).
	Save = { "Save", "Save Current Scene", nullptr, White, { EKeyCode::S, true } };

	Play = { "Play", "Play World", UAssetManager::Get().LoadTexture(EditorResourcePaths::ToolbarPlayIcon), Green, { EKeyCode::P, false, true } };

	Play.CanExecute = [this]() -> bool
	{ return !PlayWorld; };
	Play.Execute = [this]()
	{ RequestPlaySession(); };

	Pause = { "Pause", "Pause", UAssetManager::Get().LoadTexture(EditorResourcePaths::ToolbarPauseIcon), White, { EKeyCode::Pause } };

	Pause.CanExecute = [this]() -> bool
	{ return !bIsPaused && PlayWorld; };
	Pause.Execute = [this]()
	{ bIsPaused = true; };


	Resume = { "Resume", "Resume", UAssetManager::Get().LoadTexture(EditorResourcePaths::ToolbarPlayIcon), White, { EKeyCode::Pause } };

	Resume.CanExecute = [this]() -> bool
	{ return bIsPaused && PlayWorld; };
	Resume.Execute = [this]()
	{ bIsPaused = false; };

	StepFrame = { "Step", "Step One Frame", UAssetManager::Get().LoadTexture(EditorResourcePaths::ToolbarStepIcon), White, {} };

	StepFrame.CanExecute = [this]() -> bool
	{ return bIsPaused && PlayWorld; };
	StepFrame.Execute = [this]()
	{ bStepRequested = true; };


	Stop = { "Stop", "Stop", UAssetManager::Get().LoadTexture(EditorResourcePaths::ToolbarStopIcon), White, { EKeyCode::Escape } };

	Stop.CanExecute = [this]() -> bool
	{ return PlayWorld; };
	Stop.Execute = [this]()
	{ RequestEndPlayMap(); };

	EditorUI->GetToolbar()->SetCommands({ Play, Pause, Resume, StepFrame, Stop });
	EditorUI->GetToolbar()->SetPlayModeBinding(
		[this]()
		{ return PlayMode; },
		[this](EPlayModeType Mode)
		{ PlayMode = Mode; },
		[this]()
		{ return !IsPlaying(); });

	return true;
}

// 프레임 시작·View 상태·월드 갱신 후 View별 오프스크린 렌더와 UI 합성을 진행한다.
// 입력·창 메시지는 FEngineLoop가 먼저 처리하고 Present는 호출 직후에 한다.
void UEditorEngine::Tick(const float DeltaTime)
{
	// PIE 창의 닫기 버튼은 앱이 아니라 PIE 세션만 끝낸다.
	if (PIEWindow && PIEWindow->ConsumeCloseRequest())
	{
		RequestEndPlayMap();
	}
	if (bRequestEndPlayMapQueued)
	{
		bRequestEndPlayMapQueued = false;
		EndPlayMap();
	}
	if (bPlaySessionRequested)
	{
		bPlaySessionRequested = false;
		if (!PlayWorld)
		{
			StartPlayInEditorSession();
		}
	}

	ImGuiIO& IO = ImGui::GetIO();
	if (IsPlaying() && !bIsPaused)
	{
		IO.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
	}
	else
	{
		IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	}

	BeginFrame(DeltaTime);
	UpdateViewports(DeltaTime);
	UpdatePIEViewCamera();
	TickWorldAndEditor(DeltaTime);
	if (PlayWorld)
	{
		if (FSceneTransitionManager* SceneTransitions = GetSceneTransitionManager())
		{
			SceneTransitions->ProcessPendingTransition();
		}
	}
	RenderMultipleViewports();
	EndFrame();
}

// 기존 Directional 설정을 변경하여 다음 프레임의 그림자 준비에 반영한다.
bool UEditorEngine::ExecShadowCommand(const FString& CommandLine, FString& OutMessage)
{
	std::istringstream Input(CommandLine);
	FString Command, Action, Value, Extra;
	Input >> Command >> Action >> Value >> Extra;

	// 다른 명령은 기존 콘솔 처리 경로에서 계속 판정한다.
	if (_stricmp(Command.c_str(), "CSM") != 0)
	{
		return false;
	}

	// 잘못된 입력은 아래 검증에서 설정 변경 없이 이 메시지를 반환한다.
	OutMessage = "Usage: CSM | CSM ON | CSM OFF | CSM SET <0-4>";
	if (!Extra.empty())
	{
		return true;
	}

	// -1은 개수 변경이 없는 토글·ON·OFF 명령을 의미한다.
	int32 RequestedCount = -1;
	if (_stricmp(Action.c_str(), "SET") == 0)
	{
		// 현재 지원 범위인 한 자리 정수 0~4만 허용한다.
		if (Value.size() != 1 || Value[0] < '0' || Value[0] > '0' + UDirectionalLightComponent::MaxShadowCascades)
		{
			return true;
		}

		RequestedCount = Value[0] - '0';
	}
	else
	{
		// SET 이외에는 인자가 없거나 ON/OFF만 사용할 수 있다.
		if (!Value.empty() || (!Action.empty() && _stricmp(Action.c_str(), "ON") != 0 && _stricmp(Action.c_str(), "OFF") != 0))
		{
			return true;
		}
	}

	// 평소에는 EditorWorld, PIE 중에는 PlayWorld를 대상으로 한다.
	UWorld* World = GetActiveWorld();
	const FDirectionalLightSceneProxy* Primary = World->GetScene().GetPrimaryDirectionalLight();

	if (!Primary)
	{
		OutMessage = "No active Directional Light in the current world.";
		return true;
	}

	// 실제 Primary의 Owner에서 수정 가능한 동일 Component를 찾는다.
	const ULightComponent* PrimaryComponent = Primary->GetLightComponent();
	UDirectionalLightComponent* Light = nullptr;
	for (UActorComponent* Component : PrimaryComponent->GetOwner()->GetComponents())
	{
		if (Component == PrimaryComponent)
		{
			Light = Cast<UDirectionalLightComponent>(Component);
			break;
		}
	}

	if (!Light)
	{
		OutMessage = "Directional Light component was not found.";
		return true;
	}

	if (RequestedCount >= 0)
	{
		// SET 0은 마지막 개수를 보존하며 CSM만 비활성화한다.
		if (RequestedCount > 0)
		{
			Light->SetCascadeCount(RequestedCount);
		}

		Light->SetCSMEnabled(RequestedCount > 0);
	}
	else if (Action.empty())
	{
		// 단독 CSM 입력은 기존 STAT 명령처럼 현재 상태를 반전한다.
		Light->SetCSMEnabled(!Light->IsCSMEnabled());
	}
	else
	{
		// 입력 검증을 통과했으므로 남은 Action은 ON 또는 OFF다.
		Light->SetCSMEnabled(_stricmp(Action.c_str(), "ON") == 0);
	}

	// OFF에서는 출력된 개수가 저장된 설정값임을 명시한다.
	const bool bEnabled = Light->IsCSMEnabled();
	OutMessage = std::format(
		"CSM: {}, {}: {} (World: {})",
		bEnabled ? "ON" : "OFF",
		bEnabled ? "Cascades" : "Saved Cascades",
		Light->GetCascadeCount(),
		World->IsPlayInEditor() ? "PIE" : "Editor");
	return true;
}

bool UEditorEngine::ExecShadowResolutionCommand(const FString& CommandLine, FString& OutMessage)
{
	std::istringstream Input(CommandLine);
	FString Command, Extra;
	Input >> Command;

	// 다른 명령은 기존 콘솔 처리 경로에서 계속 판정한다.
	if (_stricmp(Command.c_str(), "ShadowResolution") != 0)
	{
		return false;
	}

	// 잘못된 입력은 아래 검증에서 설정 변경 없이 이 메시지를 반환한다.
	OutMessage = "Usage: ShadowResolution <128 | 256 | 512 | 1024 | 2048 | 4096>";
	int32 Resolution = 0;
	if (!(Input >> Resolution) || (Input >> Extra) || !Renderer->SetShadowMapResolution(Resolution))
	{
		return true;
	}
	OutMessage = std::format("ShadowMap global maximum: {}", Resolution);

	return true;
}

// DeltaTime을 패널에 전달하고 에디터 단축키를 처리한다.
void UEditorEngine::BeginFrame(const float DeltaTime)
{
	FStatOverlay::Tick(DeltaTime);
	EditorControlsPanel->FEditorControlsPanel::DeltaTime = DeltaTime;

	// 뷰어에 포커스가 있으면 레벨 액터를 지우지 않는다
	if (!ImGui::GetIO().WantTextInput && !PersonaEditorManager->HasFocusedEditor() && FInputSystem::IsKeyPressed(EKeyCode::Delete))
	{
		DeleteActor(OutlinerPanel->GetSelectedActor());
	}

	UpdatePIEInput();
}

// UE의 PIE 조작: Esc로 끝내고, Shift+F1로 게임을 돌린 채 커서를 꺼내며, 뷰포트를 클릭하면 다시 들어간다.
void UEditorEngine::UpdatePIEInput()
{
	if (!PlayWorld)
	{
		return;
	}

	// 이름 편집의 취소나 FBX 뷰어 조작까지 PIE 종료로 받지 않는다.
	if (!ImGui::GetIO().WantTextInput && !PersonaEditorManager->HasFocusedEditor() && Stop.Shortcut.IsPressedThisFrame())
	{
		Stop.TryExecute();
		return;
	}

	APlayerController* PlayerController{ PlayWorld->GetPlayerController() };
	if (!PlayerController)
	{
		return;
	}

	// 커서를 꺼낸 동안 스크립트가 입력을 무시할 수 있도록 커서 모드를 함께 바꾼다.
	if (PlayerController->GetCursorMode() == ECursorMode::Locked)
	{
		bPIEMouseReleased = false;
		const FInputChord ReleaseMouse{ EKeyCode::F1, false, false, true };
		if (ReleaseMouse.IsPressedThisFrame())
		{
			bPIEMouseReleased = true;
			PlayerController->SetCursorMode(ECursorMode::Free);
		}
	}
	else if (bPIEMouseReleased && !bIsPaused && FInputSystem::IsMousePressed(EMouseButton::Left) && GetGameViewportInfo(PlayWorld).bAcceptsMouseInput)
	{
		bPIEMouseReleased = false;
		PlayerController->SetCursorMode(ECursorMode::Locked);
	}
}

void UEditorEngine::UpdateViewports(const float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_ViewportUpdate);
	ViewportsPanel->UpdateViewports(DeltaTime, SettingsPanel->GetSettings().CameraSpeed, SettingsPanel->GetSettings().MouseSensitivity);
}

// 월드를 한 번 Tick한 뒤 에디터와 피킹을 갱신한다.
void UEditorEngine::TickWorldAndEditor(const float DeltaTime)
{
	// 월드 상태는 프레임마다 정확히 한 번 갱신한다.

	{
		SCOPE_CYCLE_COUNTER(STAT_WorldTick);
		if (bStepRequested && bIsPaused)
		{
			GetActiveWorld()->Tick(1.0f / 60.0f);
			bStepRequested = false;
		}
		else if (!bIsPaused)
		{
			GetActiveWorld()->Tick(DeltaTime);
		}
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_EditorTick);
		EditorUI->Tick(DeltaTime);
	}
	// 렌더 준비는 View마다 그릴 월드가 다르므로 RenderMultipleViewports에서 한다.
	// PIE 중에도 PIE가 아닌 View에서는 EditorWorld 편집을 계속한다.
	UpdateGizmoAndPicking();
	EditorWorld->GetScene().UpdateAllTransforms();
	if (PlayWorld && bIsPaused)
	{
		PlayWorld->GetScene().UpdateAllTransforms();
	}
}

// 활성 World를 먼저 준비하고 그림자를 생성한 뒤 View별 카메라 수집·렌더를 수행한다.
void UEditorEngine::RenderMultipleViewports()
{
	// PIE 창 크기를 외부 View에 먼저 알려야 카메라 Override의 직교 폭 보정이 맞는다.
	bPIEWindowRendered = false;
	const bool bRenderPIEWindow = IsPIEInWindow() && UpdatePIEWindowTargets();

	// 월드 Tick으로 움직인 게임 카메라를 같은 프레임에 반영한다.
	UpdatePIEViewCamera();

	UWorld* PreparedWorld = nullptr;
	Renderer->SetLightDebugMode(SettingsPanel->GetSettings().LightDebugMode);
	Renderer->SetLightCullingMode(static_cast<ELightCullingMode>(SettingsPanel->GetSettings().LightCullingMode));

	// 패널 View 4개와 별도 PIE 창 View의 FSceneView. 그림자 작업이 포인터를 쓰므로 프레임 동안 자리를 유지한다.
	FSceneView SceneViews[MaxViewCount]{};
	bool bSceneViewReady[MaxViewCount]{};
	TArray<UWorld*> PreparedWorlds;

	const auto PrepareView = [&](int32 ViewIndex, const FRenderingInfo& ViewRenderingInfo)
	{
		if (!IsViewActive(ViewIndex))
		{
			return;
		}

		UWorld* ViewWorld = GetWorldForView(ViewIndex);

		// Transform·BVH 갱신과 동적 데이터 준비는 활성 World마다 한 번 수행한다.
		if (PreparedWorlds.Find(ViewWorld) == INDEX_NONE)
		{
			SCOPE_CYCLE_COUNTER(STAT_PrepareRenderViews);
			SceneRenderer->BeginRenderViews(*ViewWorld);
			PreparedWorlds.Add(ViewWorld);
		}

		SceneViews[ViewIndex] = MakeSceneView(ViewIndex, ViewRenderingInfo);
		bSceneViewReady[ViewIndex] = true;
	};

	// 패널 View의 렌더 타깃은 UpdateViewports에서 이번 프레임 크기로 맞춰 두었다.
	for (int32 ViewIndex = 0; ViewIndex < FViewportsPanel::ViewCount; ++ViewIndex)
	{
		PrepareView(ViewIndex, ViewportsPanel->GetRenderingInfo(ViewIndex));
	}

	// 별도 PIE는 패널과 분리하여 준비하며, 활성 여부는 PrepareView에서 확인한다.
	if (bRenderPIEWindow)
	{
		PrepareView(PIEViewIndex, PIERenderingInfo);
	}

	// Skeletal Mesh Editor 프리뷰도 같은 그림자 작업에 넣으므로 그림자보다 먼저 준비한다.
	TArray<const FPersonaAssetEditor*> Previews;
	TArray<FSceneView> PreviewViews;
	PrepareSkeletalMeshPreviews(Previews, PreviewViews);

	// 같은 World를 보는 View를 묶어 지역광 작업을 공유하고, 모든 World의 그림자를 한 번에 그린다.
	// 빈 목록도 넘겨 이전 프레임의 작업·바인딩·타일 할당을 정리한다.
	TArray<FSceneShadowViews> ShadowViews;
	for (UWorld* World : PreparedWorlds)
	{
		FSceneShadowViews Group{};
		Group.World = World;
		for (int32 ViewIndex = 0; ViewIndex < MaxViewCount; ++ViewIndex)
		{
			if (bSceneViewReady[ViewIndex] && GetWorldForView(ViewIndex) == World)
			{
				Group.Views.Add(&SceneViews[ViewIndex]);
			}
		}
		ShadowViews.Add(Group);
	}
	// 프리뷰는 에디터마다 월드가 따로라 View 하나씩 묶는다.
	for (int32 PreviewIndex = 0; PreviewIndex < Previews.Num(); ++PreviewIndex)
	{
		FSceneShadowViews Group{};
		Group.World = Previews[PreviewIndex]->GetPreviewWorld();
		Group.Views.Add(&PreviewViews[PreviewIndex]);
		ShadowViews.Add(Group);
	}
	SceneRenderer->RenderShadowDepths(ShadowViews);

	for (int32 ViewIndex = 0; ViewIndex < FViewportsPanel::ViewCount; ++ViewIndex)
	{
		if (!bSceneViewReady[ViewIndex])
		{
			continue;
		}

		UWorld* ViewWorld = GetWorldForView(ViewIndex);

		if (ViewWorld != PreparedWorld)
		{
			SCOPE_CYCLE_COUNTER(STAT_PrepareRenderViews);

			ViewWorld->GetScene().UpdateLights(Renderer);
			PreparedWorld = ViewWorld;
		}

		{
			SCOPE_CYCLE_COUNTER(STAT_BuildRenderQueue);
			BuildRenderQueue(GetViewportClient(ViewIndex), *ViewWorld);
		}

		RenderFrame(
			ViewIndex,
			*ViewWorld,
			ViewportsPanel->GetRenderingInfo(ViewIndex),
			SceneViews[ViewIndex],
			RenderQueue, IconQueue, ArrowQueue);
	}

	// 별도 PIE 창은 패널 View 뒤에 PlayWorld의 데이터로 그린다.
	if (bRenderPIEWindow && IsViewActive(PIEViewIndex))
	{
		UWorld* ViewWorld = GetWorldForView(PIEViewIndex);
		if (ViewWorld != PreparedWorld)
		{
			SCOPE_CYCLE_COUNTER(STAT_PrepareRenderViews);

			ViewWorld->GetScene().UpdateLights(Renderer);
			PreparedWorld = ViewWorld;
		}

		{
			SCOPE_CYCLE_COUNTER(STAT_BuildRenderQueue);
			BuildRenderQueue(GetViewportClient(PIEViewIndex), *ViewWorld);
		}

		RenderFrame(
			PIEViewIndex,
			*ViewWorld,
			PIERenderingInfo,
			SceneViews[PIEViewIndex],
			RenderQueue, IconQueue, ArrowQueue);
		CopyPIEWindowToBackbuffer();
	}

	// Skeletal Mesh Editor 프리뷰는 레벨 View와 월드가 달라 따로 그린다. ImGui가 그리기 전에 끝나야 한다.
	RenderSkeletalMeshPreviews(Previews, PreviewViews);
}

void UEditorEngine::PrepareSkeletalMeshPreviews(TArray<const FPersonaAssetEditor*>& OutPreviews, TArray<FSceneView>& OutViews)
{
	PersonaEditorManager->GetPreviewsToRender(OutPreviews);

	for (const FPersonaAssetEditor* Preview : OutPreviews)
	{
		// 그림자 작업이 최신 Transform을 쓰도록 먼저 반영한다
		SceneRenderer->BeginRenderViews(*Preview->GetPreviewWorld());

		// MakeSceneView와 같지만 ViewIndex 대신 에디터 값을 쓴다
		FSceneView View{};
		Preview->GetViewportClient().CalcSceneView(View);
		View.ViewId = Preview->GetViewId();
		View.SceneTargets = Preview->GetRenderingInfo();
		View.SceneTargets.ColorRenderTargets[0].Texture = Preview->GetHDRTarget();
		View.bDrawPrimitives = true;
		View.bDynamicShadows = true;
		View.bFog = false;
		View.Exposure = SettingsPanel->GetSettings().Exposure;
		View.SkyPanorama = EditorResourcePaths::AssetViewerSkyPanorama;
		OutViews.Add(View);
	}
}

void UEditorEngine::RenderSkeletalMeshPreviews(const TArray<const FPersonaAssetEditor*>& Previews, const TArray<FSceneView>& Views)
{
	for (int32 PreviewIndex = 0; PreviewIndex < Previews.Num(); ++PreviewIndex)
	{
		const FPersonaAssetEditor* Preview = Previews[PreviewIndex];
		const FSceneView& View = Views[PreviewIndex];
		UWorld& PreviewWorld = *Preview->GetPreviewWorld();
		const FEditorViewportClient& Client = Preview->GetViewportClient();
		const FRenderingInfo& PreviewRenderingInfo = Preview->GetRenderingInfo();

		// 레벨 View 뒤라 라이트 버퍼를 이 월드 것으로 다시 채운다. 다음 프레임 레벨 View도 다시 채운다.
		PreviewWorld.GetScene().UpdateLights(Renderer);

		BuildRenderQueue(Client, PreviewWorld);
		SceneRenderer->RenderView(PreviewWorld, View, RenderQueue, PreviewRenderingInfo.ColorRenderTargets[0].Texture);

		// 본은 톤 매핑이 끝난 결과 위에 그림
		FRenderingInfo BoneOverlayInfo = PreviewRenderingInfo;
		BoneOverlayInfo.ColorRenderTargets[0].LoadOp = ERenderTargetLoadOp::Load;
		BoneOverlayInfo.DepthStencil.Texture = nullptr;
		RenderCommand::BeginRenderPass(BoneOverlayInfo);
		LineBatcher->BeginFrame();
		Preview->DrawBones(*LineBatcher);
		LineBatcher->OnRender(View.ViewProjection);
		// 다음에 ImGui가 이 타깃을 SRV로 읽으므로 RTV 바인딩을 푼다
		RenderCommand::EndRenderPass(BoneOverlayInfo);

		// 기즈모는 본보다 위. 레벨 View처럼 깊이를 비워 기즈모끼리만 가리게 한다
		if (const FGizmo* PreviewGizmo = Preview->GetGizmo(); PreviewGizmo && PreviewGizmo->HasPivot())
		{
			FRenderingInfo GizmoInfo = PreviewRenderingInfo;
			GizmoInfo.ColorRenderTargets[0].LoadOp = ERenderTargetLoadOp::Load;
			GizmoInfo.DepthStencil.LoadOp = ERenderTargetLoadOp::Clear;
			RenderCommand::BeginRenderPass(GizmoInfo);
			GizmoRenderer->OnRender(*PreviewGizmo, View.ViewProjection, Client.GetViewLocation(), Client.IsOrthographic());
			RenderCommand::EndRenderPass(GizmoInfo);
		}
	}
}

void UEditorEngine::UpdatePIEViewCamera()
{
	if (PlayWorld)
	{
		APlayerController* PlayerController{ PlayWorld->GetPlayerController() };
		if (PlayerController && PlayerController->GetCursorMode() == ECursorMode::Locked)
		{
			SetGameMouseCapture(PlayWorld, !bIsPaused);
		}
	}

	if (!PlayWorld || PIEViewIndex == InvalidViewIndex)
	{
		return;
	}

	APlayerCameraManager* CameraManager{ PlayWorld->GetPlayerCameraManager() };
	UCameraComponent* GameCamera{ CameraManager ? CameraManager->GetViewTargetCamera() : nullptr };
	if (!GameCamera || !CameraManager->HasValidCameraView())
	{
		GetViewportClient(PIEViewIndex).ClearCameraOverride();
		return;
	}

	const FRect& Rect{ GetViewportClient(PIEViewIndex).GetViewRect() };
	if (Rect.Width > 0.0f && Rect.Height > 0.0f)
	{
		GameCamera->SetAspectRatio(Rect.Width / Rect.Height);
	}

	// 엔진 View 행렬에서 깊이축(View X)을 꺼내면 회전 규약과 무관하게 실제 렌더 방향과 같다.
	FMinimalViewInfo CameraView{ CameraManager->GetCameraView() };
	if (Rect.Width > 0.0f && Rect.Height > 0.0f)
	{
		CameraView.SetAspectRatio(Rect.Width / Rect.Height);
	}
	const FMatrix View{ CameraView.GetViewMatrix() };
	const FVector Forward{ View.M[0][0], View.M[1][0], View.M[2][0] };
	const FQuat Rotation{ CameraView.GetRotation().Quaternion() };

	const FCameraProjection Projection{ CameraView.GetIsOrthogonal() ? EProjectionMode::Orthographic : EProjectionMode::Perspective, CameraView.GetFieldOfView(), CameraView.GetOrthoWidth(), CameraView.GetNearZ(), CameraView.GetFarZ() };
	GetViewportClient(PIEViewIndex).SetCameraOverride(CameraView.GetLocation(), Forward, Projection, &Rotation);
}

// 화면을 표시하고 UI 변경 후 View 설정을 보관한다.
void UEditorEngine::EndFrame()
{
	PresentFrame();
	// 메인 Swapchain은 FEngineLoop가 VSync 설정대로 Present하므로 PIE 창은 기다리지 않고 올린다.
	if (bPIEWindowRendered && PIESwapchain)
	{
		PIESwapchain->SwapBuffers(0);
	}
	// UI 변경 후 설정을 복사해 종료 시 카메라 수명에 의존하지 않는다.
	SettingsPanel->CaptureViewportSettings();
	// 프로파일러 반영
	FStatRegistry::EndFrame();
}

FWorldContext& UEditorEngine::GetEditorWorldContext()
{
	for (int32 i = 0; i < WorldList.Num(); ++i)
	{
		if (WorldList[i].WorldType == EWorldType::Editor)
		{
			return WorldList[i];
		}
	}

	return CreateNewWorldContext(EWorldType::Editor);
}

FWorldContext* UEditorEngine::GetPIEWorldContext(int32 WorldPIEInstance)
{
	for (FWorldContext& WorldContext : WorldList)
	{
		if (WorldContext.WorldType == EWorldType::PIE)
		{
			return &WorldContext;
		}
	}

	return nullptr;
}

void UEditorEngine::RenderOverlayPass(const int32 ViewIndex, UWorld& ViewWorld, const FRenderingInfo& ViewRenderingInfo, const FMatrix& ViewProjection, const FVector& ViewCameraLocation, const FVector& ViewCameraForward, FRenderQueue& RenderQueue, FRenderQueue& IconQueue, FRenderQueue& ArrowQueue)
{
	// 실행 중인 PIE View에는 Grid·Gizmo·Outline 같은 에디터 표시를 그리지 않는다.
	// 별도 PIE 창은 편집 입력이 없으므로 일시정지 중에도 게임 화면만 보인다.
	const bool bShowEditorOverlay = !IsPlayingView(ViewIndex) && ViewIndex != PIEWindowViewIndex;
	const FEditorViewportClient& ViewportClient = GetViewportClient(ViewIndex);
	const FEngineShowFlags& ShowFlags = ViewportClient.GetShowFlags();
	FRenderingInfo PassInfo = ViewRenderingInfo;

	for (FRenderingDesc& Color : PassInfo.ColorRenderTargets)
	{
		Color.LoadOp = ERenderTargetLoadOp::Load;
	}

	PassInfo.DepthStencil.LoadOp = ERenderTargetLoadOp::Load;


	RenderCommand::BeginRenderPass(PassInfo);

	if (bShowEditorOverlay && ViewportClient.GetViewMode() != EViewMode::SceneDepth)
	{
		// 라인 배처는 매 프레임 한 번만 비우고 한 번만 그린다.
		// 바운딩박스는 그 안에 쌓이는 여러 항목 중 하나일 뿐이다.
		LineBatcher->BeginFrame();

		if (ShowFlags.IsEnabled(EEngineShowFlag::Bounds))
		{
			LineBatcher->BuildVertexBuffer(ViewWorld);
		}

		// 선택된 액터가 라이트면 원뿔을 같이 쌓는다
		if (GizmoTarget)
		{
			if (ALightActorBase* LightActor = Cast<ALightActorBase>(GizmoTarget->GetOwner()))
			{
				LightActor->GetLightComponent()->DrawDebug(LineBatcher.get());
			}
		}

		TArray<UShapeComponent*> Shapes;
		for (auto Iter = TObjectIterator<UShapeComponent>(); Iter; Iter++)
		{
			if (Iter->GetWorld() == &ViewWorld)
			{
				Shapes.Add(*Iter);
			}
		}

		const AActor* SelectedActor = GizmoTarget ? GizmoTarget->GetOwner() : nullptr;
		for (UShapeComponent* Shape : Shapes)
		{
			if (Shape->ShouldDrawOnlyIfSelected() && Shape->GetOwner() != SelectedActor)
			{
				continue;
			}

			bool bOverlapping = false;
			for (UShapeComponent* Other : Shapes)
			{
				if (Shape->IsOverlapping(*Other))
				{
					bOverlapping = true;
					break;
				}
			}

			Shape->DrawDebug(*LineBatcher, bOverlapping);
		}

		// 데칼 상자(Decal Volume)와 투영 방향(X축)을 주황색 선으로 표시한다.
		const FVector4 DecalColor(1.0f, 0.55f, 0.1f, 1.0f);
		const FVector4 ReceiverColor(1.0f, 0.15f, 0.15f, 1.0f);
		const FVector4 TriangleColor(1.0f, 0.9f, 0.1f, 1.0f);
		// 큰 메시에서 선이 너무 많아지지 않도록 표시할 삼각형 수를 제한한다.
		constexpr int32 MaxDebugTriangles = 2048;
		TArray<FPrimitiveSceneProxy*> DecalReceivers;
		TArray<uint32> DecalTriangleIndices;

		if (ShowFlags.IsEnabled(EEngineShowFlag::Decals))
		{
			for (UDecalComponent* Decal : ViewWorld.GetScene().Decals)
			{
				// CalcLocalBounds()가 {-DecalSize, DecalSize}라서 컴포넌트 월드 행렬만 곱하면 된다.
				LineBatcher->AddOrientedBox(Decal->CalcLocalBounds(), Decal->GetWorldMatrix(), DecalColor);

				const Collision::FOrientedBox Box = Decal->GetOrientedBox();
				LineBatcher->AddLine(Box.Center, Box.Center + Box.Axis[0] * Box.Extent.X, DecalColor);

				// 선택한 데칼이 데칼을 받을 메시로 고른 것을 빨간 상자로 보여 준다.
				if (Decal->GetOwner() == SelectedActor)
				{
					DecalReceivers.Reset();
					ViewWorld.GetScene().GatherDecalReceivers(*Decal, DecalReceivers);
					for (FPrimitiveSceneProxy* Receiver : DecalReceivers)
					{
						LineBatcher->AddOrientedBox(Receiver->GetComponent()->CalcLocalBounds(), Receiver->GetLocalToWorld(), ReceiverColor);

						// OBBTree가 고른 삼각형만 노란 선으로 그린다. 데칼 근처에만 보여야 정상이다.
						DecalTriangleIndices.Reset();
						if (!FScene::GatherDecalTriangles(*Decal, *Receiver, DecalTriangleIndices))
						{
							continue;
						}
						const FStaticMeshData* Data = Receiver->GetComponent()->GetMeshData();
						if (!Data)
						{
							continue;
						}
						const FMatrix& LocalToWorld = Receiver->GetLocalToWorld();
						const int32 IndexCount = std::min(DecalTriangleIndices.Num(), MaxDebugTriangles * 3);
						for (int32 i = 0; i + 2 < IndexCount; i += 3)
						{
							const FVector P0 = LocalToWorld.TransformPosition(Data->Vertices[DecalTriangleIndices[i]].Position);
							const FVector P1 = LocalToWorld.TransformPosition(Data->Vertices[DecalTriangleIndices[i + 1]].Position);
							const FVector P2 = LocalToWorld.TransformPosition(Data->Vertices[DecalTriangleIndices[i + 2]].Position);
							LineBatcher->AddLine(P0, P1, TriangleColor);
							LineBatcher->AddLine(P1, P2, TriangleColor);
							LineBatcher->AddLine(P2, P0, TriangleColor);
						}
					}
				}
			}
		}

		LineBatcher->OnRender(ViewProjection);
	}

	// 그리드
	// TODO : 그리드도 안개가 적용되어야하나..?
	//  언리얼처럼 투영에 따라 방식을 고정한다. 원근은 PS 그리드, 직교는 보는 평면에 맞춘 배치 라인 그리드다.
	if (bShowEditorOverlay && ShowFlags.IsEnabled(EEngineShowFlag::Grid))
	{
		const EGridPlane GridPlane = ViewportClient.GetGridPlane();

		if (!ViewportClient.IsOrthographic())
		{
			GridRenderer->OnRenderPSGrid(ViewProjection, ViewCameraLocation, SettingsPanel->GetSettings(), PassInfo.ViewportSetting);
		}
		else
		{
			GridRenderer->OnRenderBatchGrid(
				ViewProjection,
				ViewCameraLocation,
				ViewCameraForward,
				GridPlane,
				static_cast<float>(SettingsPanel->GetSettings().GridSpacing),
				SettingsPanel->GetSettings().GridRange,
				// 이 분기는 항상 직교다. 자유 직교 View만 세 축을 모두 그린다.
				ViewportClient.GetViewportType() == ELevelViewportType::OrthographicView,
				PassInfo.ViewportSetting);
		}
	}

	// Editor Icon (안개, Light Actor) + 빛 방향 Arrow
	// TODO: 모든 프리미티브를 공통 큐로 통합하고 패스별로 렌더하도록 별도 IconQueue·ArrowQueue를 제거한다.
	if (bShowEditorOverlay)
	{
		RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
		RenderCommand::SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// 아이콘 먼저 그리기 (반투명)
		Renderer->RenderQueueSorting(IconQueue, ViewProjection, ViewCameraLocation);
		Renderer->RenderTranslucent(ViewProjection);

		// 화살표 그리기 (불투명)
		Renderer->RenderQueueSorting(ArrowQueue, ViewProjection, ViewCameraLocation);
		Renderer->RenderOpaque(ViewProjection);
		Renderer->RenderTranslucent(ViewProjection); // 패킷 뒷정리
	}

	// TextRenderComponent 렌더링
	for (TObjectIterator<UTextRenderComponent> TextComponent; TextComponent; ++TextComponent)
	{
		if (!TextComponent || !TextComponent->GetFont() || !TextComponent->IsVisible())
		{
			continue;
		}
		// 에디터·PIE 월드의 컴포넌트가 함께 순회되므로 이 View의 월드만 그린다.
		if (!TextComponent->GetOwner() || TextComponent->GetOwner()->GetWorld() != &ViewWorld)
		{
			continue;
		}

		TextRenderer->OnRender(
			TextComponent->GetText(),
			TextComponent->GetWorldMatrix(),
			TextComponent->GetTextSize(),
			*TextComponent->GetFont(),
			ViewProjection);
	}

	// 선택 대상은 한 월드에만 속하므로 그 월드를 그리는 View에만 표시한다.
	const auto IsInViewWorld = [&ViewWorld](const UActorComponent* Component)
	{
		return Component && Component->GetOwner() && Component->GetOwner()->GetWorld() == &ViewWorld;
	};

	// 스텐실 기반이라 선택 대상의 가시성이 꺼져 있어도 외곽선만 그린다.
	if (bShowEditorOverlay && IsInViewWorld(Outline->GetTarget()))
	{
		OutlineRenderer->OnRender(*Outline, ViewProjection, PassInfo.ViewportSetting);
	}

	if (bShowEditorOverlay && IsInViewWorld(GizmoTarget))
	{
		RenderCommand::ClearDepthStencil(ViewRenderingInfo.DepthStencil.Texture);

		GizmoRenderer->OnRender(
			*Gizmo,
			ViewProjection,
			ViewCameraLocation,
			ViewportClient.IsOrthographic());
	}

	RenderCommand::ClearDepthStencil(PassInfo.DepthStencil.Texture);

	if (ShowFlags.IsEnabled(EEngineShowFlag::UUID))
	{
		// 월드 +Z가 아니라 화면 위쪽으로 띄워야 Top·Bottom처럼 Z축을 따라 보는 View에서도 물체를 가리지 않는다.
		const FVector ScreenUp = ViewportClient.GetViewUp();

		for (AActor* Actor : ViewWorld.GetPersistentLevel()->GetActors())
		{
			if (!Actor)
			{
				continue;
			}

			FBox ActorBounds;
			if (!Actor->TryGetActorBounds(ActorBounds))
			{
				const FVector ActorLocation = Actor->GetActorLocation();
				ActorBounds.Min = ActorLocation;
				ActorBounds.Max = ActorLocation;
			}

			const FVector Center = (ActorBounds.Min + ActorBounds.Max) * 0.5f;
			const FVector HalfExtent = (ActorBounds.Max - ActorBounds.Min) * 0.5f;
			// 바운드 박스가 화면 위쪽으로 뻗은 길이만큼 올린 뒤 여백을 더한다.
			const float ExtentAlongUp = std::fabs(ScreenUp.X) * HalfExtent.X + std::fabs(ScreenUp.Y) * HalfExtent.Y + std::fabs(ScreenUp.Z) * HalfExtent.Z;
			UUIDLocation = Center + ScreenUp * (ExtentAlongUp + 0.5f);

			FString Text =
				"UUID : " + std::to_string(Actor->GetUUID());

			TextRenderer->BuildTextMesh(
				Text,
				0.5f,
				*SystemFont);

			const FMatrix BillboardWorld = ViewportClient.BuildBillboardMatrix(UUIDLocation, 1.0f, 1.0f);
			TextRenderer->OnRender(Text, BillboardWorld, 0.5f, *SystemFont, ViewProjection);
		}
	}

	// PIE View에는 게임 플레이어의 HUD를 그린다.
	if (PlayWorld && ViewIndex == PIEViewIndex)
	{
		if (APlayerController* PlayerController = PlayWorld->GetPlayerController())
		{
			UIRenderer->Render(PlayerController->GetHUD(),
				static_cast<float>(ViewRenderingInfo.ViewportSetting.Width),
				static_cast<float>(ViewRenderingInfo.ViewportSetting.Height));
		}
	}

	RenderCommand::EndRenderPass(PassInfo);
}

void UEditorEngine::RenderDepthPass(const int32 ViewIndex, const FRenderingInfo& ViewRenderingInfo, const FMatrix& ViewProjection, const FVector& ViewCameraLocation, const FVector& ViewCameraForward)
{
	FRenderingInfo PassInfo = ViewRenderingInfo;

	for (FRenderingDesc& Color : PassInfo.ColorRenderTargets)
	{
		Color.LoadOp = ERenderTargetLoadOp::DontCare;
	}
	PassInfo.DepthStencil.Texture = nullptr;

	RenderCommand::BeginRenderPass(PassInfo);
	DepthViewRenderer->OnRender(ViewRenderingInfo.DepthStencil.Texture, ViewProjection, ViewCameraLocation, ViewCameraForward);
	RenderCommand::EndRenderPass(PassInfo);
}

// 입력 View의 Ray와 피킹으로 Gizmo·공유 선택을 갱신한다.
void UEditorEngine::UpdateGizmoAndPicking()
{
	// 입력이 없어도 렌더에 쓰이므로 매 프레임 선택 대상 위치로 맞춘다.
	SyncGizmoPivot();

	// Delete는 BeginFrame에서 한 번만 처리하고 여기서는 View 입력만 다룬다.
	const int32 ViewIndex = ViewportsPanel->GetActiveViewIndex();
	if (ViewIndex == InvalidViewIndex || !ViewportsPanel->IsHovered())
	{
		return;
	}
	// 실행 중인 PIE View는 편집하지 않는다. 일시정지 중이면 PlayWorld를 피킹한다.
	if (IsPlayingView(ViewIndex))
	{
		return;
	}

	FRay Ray{};
	if (!ViewportsPanel->TryGetActiveViewRay(Ray))
	{
		return;
	}

	const FEditorViewportClient& ViewportClient = ViewportsPanel->GetViewportClient(ViewIndex);
	const FRect& Rect = ViewportClient.GetViewRect();
	const FVector2 LocalMousePosition = ViewportsPanel->GetLocalMousePosition();
	const FVector2 ViewLocalMouse(
		LocalMousePosition.X - Rect.X,
		LocalMousePosition.Y - Rect.Y);
	const FMatrix ViewProjection = ViewportClient.CalcViewProjection();
	bool bMouseDown = FInputSystem::IsMouseDown(EMouseButton::Left);

	// 다른 월드의 선택 대상은 이 View에 Gizmo가 보이지 않으므로 조작하지 않는다.
	UWorld* ViewWorld = GetWorldForView(ViewIndex);
	const bool bGizmoInView = !GizmoTarget ||
		(GizmoTarget->GetOwner() && GizmoTarget->GetOwner()->GetWorld() == ViewWorld);

	if (bGizmoInView)
	{
		Gizmo->Update(
			Ray,
			ViewLocalMouse,
			ViewProjection,
			static_cast<int>(Rect.Width),
			static_cast<int>(Rect.Height),
			bMouseDown,
			ViewportClient.GetViewLocation(),
			ViewportClient.IsOrthographic());

		// Gizmo는 결과만 계산하므로 적용은 여기서 한다. 같은 프레임 렌더에 보이도록 위치를 다시 맞춘다.
		ApplyGizmoDragResult();
		SyncGizmoPivot();
	}

	const bool bGizmoBlocksPick = bGizmoInView && (Gizmo->IsUsing() || Gizmo->GetHoveredAxis() >= 0);
	if (FInputSystem::IsMousePressed(EMouseButton::Left) && !bGizmoBlocksPick)
	{
		// Billboard는 렌더링에 사용한 View와 동일한 행렬로 교차를 판정한다.
		const auto ResolveBillboardTransform = [](const UBillboardComponent& Billboard, const void* Context) -> FMatrix
		{
			const auto& Client = *static_cast<const FEditorViewportClient*>(Context);
			const FVector2 Size = Billboard.GetRenderSize();
			return Client.BuildBillboardMatrix(Billboard.GetWorldLocation(), Size.X, Size.Y);
		};

		// Gizmo에 사용한 Ray를 재사용하고, 월드의 피킹 결과를 선택에 직접 반영한다.
		FHitResult Hit;
		const bool bHit = ViewWorld->LineTraceSingle(Ray, Hit, ResolveBillboardTransform, &ViewportClient);
		OutlinerPanel->SelectActor(bHit ? Hit.HitComponent->GetOwner() : nullptr);
	}
}

void UEditorEngine::SyncGizmoPivot()
{
	if (!GizmoTarget)
	{
		Gizmo->ClearPivot();
		return;
	}

	// 이동은 월드 축, 회전·스케일은 Relative 값 기준으로 조작한다.
	const FRotator Rotation = Gizmo->GetMode() == EGizmoMode::Location
		? GizmoTarget->GetWorldRotation()
		: GizmoTarget->GetRelativeRotation();

	// 라이트는 스케일 대신 반지름을 키우므로 반지름을 스케일 자리에 넣는다.
	const ULocalLightComponent* Light = GetGizmoTargetLight(GizmoTarget);
	const FVector Scale = Light
		? FVector(Light->GetAttenuationRadius(), Light->GetAttenuationRadius(), Light->GetAttenuationRadius())
		: GizmoTarget->GetRelativeScale3D();

	Gizmo->SetPivot(GizmoTarget->GetWorldLocation(), Rotation, Scale);
}

void UEditorEngine::ApplyGizmoDragResult()
{
	if (!GizmoTarget || !Gizmo->HasDragResult())
	{
		return;
	}

	const FGizmoDragResult& Result = Gizmo->GetDragResult();
	switch (Gizmo->GetMode())
	{
	case EGizmoMode::Location:
		GizmoTarget->SetWorldLocation(Result.Location, false);
		break;
	case EGizmoMode::Rotation:
		GizmoTarget->SetRelativeRotation(Result.Rotation);
		break;
	case EGizmoMode::Scale:
		if (ULocalLightComponent* Light = GetGizmoTargetLight(GizmoTarget))
		{
			Light->SetAttenuationRadius(Gizmo->GetDragStartScale().X * Result.ScaleFactor);
		}
		else
		{
			GizmoTarget->SetRelativeScale3D(Result.Scale);
		}
		break;
	}
}

FSceneView UEditorEngine::MakeSceneView(const int32 ViewIndex, const FRenderingInfo& ViewRenderingInfo) const
{
	const auto& Settings = SettingsPanel->GetSettings();
	const FEngineShowFlags& ShowFlags = GetViewportClient(ViewIndex).GetShowFlags();

	FSceneView View{};
	GetViewportClient(ViewIndex).CalcSceneView(View);
	// 0은 공유 작업에 남겨두고 View 슬롯으로 안정적인 ID를 만든다.
	View.ViewId = static_cast<uint32>(ViewIndex + 1);

	// 장면은 View별 HDR 텍스처에 그리고, 톤 매핑 결과를 View의 색 텍스처로 받는다.
	View.SceneTargets = ViewRenderingInfo;
	View.SceneTargets.ColorRenderTargets[0].Texture = GetHDRTargetForView(ViewIndex);

	View.bDrawPrimitives = ShowFlags.IsEnabled(EEngineShowFlag::Primitives);
	View.bDrawSkeletalMesh = ShowFlags.IsEnabled(EEngineShowFlag::SkeletalMesh);
	View.bDynamicShadows = ShowFlags.IsEnabled(EEngineShowFlag::DynamicShadows);
	View.bFog = ShowFlags.IsEnabled(EEngineShowFlag::Fog);
	View.bDecals = ShowFlags.IsEnabled(EEngineShowFlag::Decals);
	View.bDebugLightView = Settings.LightDebugMode == 1;
	View.Exposure = Settings.Exposure;
	return View;
}

void UEditorEngine::BuildRenderQueue(const FEditorViewportClient& Client, UWorld& World)
{
	RenderQueue.Reset();
	IconQueue.Reset();
	ArrowQueue.Reset();

	// Runtime은 이 동기 호출 동안만 Client를 Billboard 계산 Context로 사용한다.
	const auto ResolveBillboard = [](const FVector& Position, float Width, float Height,
									  const void* Context) -> FMatrix
	{
		return static_cast<const FEditorViewportClient*>(Context)->BuildBillboardMatrix(Position, Width, Height);
	};

	FLODViewContext LODContext = Client.CalcLODViewContext();
	FFrustumPlanes Frustum = ExtractFrustumPlanes(LODContext.ViewProjection);

	// Renderer를 넘기지 않아 에디터의 GPU 오클루전·상수 업로드 정책을 유지한다.
	SceneRenderer->GatherRenderPackets(World, RenderQueue, &LODContext, &Frustum, nullptr,
		ResolveBillboard, &Client, &IconQueue, &ArrowQueue, Client.GetShowFlags().IsEnabled(EEngineShowFlag::SkeletalMesh));
}

// View의 장면을 그린 뒤 Grid·Gizmo·텍스트·Outline 같은 에디터 표시를 그 위에 그린다.
void UEditorEngine::RenderFrame(const int32 ViewIndex, UWorld& ViewWorld, const FRenderingInfo& ViewRenderingInfo,
	const FSceneView& SceneView, FRenderQueue& RenderQueue, FRenderQueue& IconQueue, FRenderQueue& ArrowQueue)
{
	const FEngineShowFlags& ShowFlags = GetViewportClient(ViewIndex).GetShowFlags();

	if (GetViewportClient(ViewIndex).GetViewMode() == EViewMode::SceneDepth)
	{
		// 깊이 시각화는 톤 매핑 대신 장면 깊이를 그대로 보여준다.
		SceneRenderer->RenderBasePass(ViewWorld, SceneView, SceneRenderer->BuildViewConstants(ViewWorld, SceneView), RenderQueue);
		RenderDepthPass(ViewIndex, ViewRenderingInfo, SceneView.ViewProjection, SceneView.Location, SceneView.Forward);
	}
	else
	{
		FTexture2D* ViewColor = ViewRenderingInfo.ColorRenderTargets[0].Texture;
		SceneRenderer->RenderView(ViewWorld, SceneView, RenderQueue, ViewColor);
		// 결과 위에 에디터 표시를 그리므로 FXAA 결과를 View 색 텍스처에 되돌려 쓴다.
		if (ShowFlags.IsEnabled(EEngineShowFlag::FXAA))
		{
			SceneRenderer->RenderFXAAInPlace(ViewColor, GetFxaaTargetForView(ViewIndex), ViewRenderingInfo.ViewportSetting);
		}
	}

	RenderOverlayPass(ViewIndex, ViewWorld, ViewRenderingInfo, SceneView.ViewProjection, SceneView.Location, SceneView.Forward, RenderQueue, IconQueue, ArrowQueue);
}


// View Texture가 포함된 UI를 Swapchain 백버퍼에 합성한다. Present는 FEngineLoop가 한다.
void UEditorEngine::PresentFrame()
{
	// Swapchain 렌더링
	RenderCommand::BeginRenderPass(MainWindowSC->GetRenderingInfo());

	{
		SCOPE_CYCLE_COUNTER(STAT_ImGui);

		ImGuiRenderer->Begin();

		EditorUI->OnRender();

		ImGuiRenderer->End();
	}

	RenderCommand::EndRenderPass(MainWindowSC->GetRenderingInfo());
}

// ImGui를 정리한다. UObject 일괄 삭제와 공용 자원·Device 정리는 FEngineLoop가 이어서 한다.
void UEditorEngine::PreExit()
{
	// 프리뷰 월드도 UObject라 FEngineLoop::Exit의 일괄 삭제에 먼저 지워진다.
	// 그 뒤 EditorUI 소멸 때 에디터가 같은 월드를 다시 지우지 않도록 여기서 먼저 닫는다.
	PersonaEditorManager->CloseAllEditors();

	EndPlayMap();
	EditorWorld->ClearWorld();
	// PIE 창이 열린 채 종료하면 Device가 살아 있을 때 Swapchain과 타깃을 먼저 정리한다.
	DestroyPIEWindow();
	ImGuiRenderer->Shutdown();

	Super::PreExit();
}

// 선택과 Gizmo 참조를 정리한 뒤 Actor를 삭제한다.
void UEditorEngine::DeleteActor(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}
	// 실행 중 삭제는 게임 로직이 들고 있는 참조를 깨뜨릴 수 있어 Pause 상태에서만 허용한다.
	if (IsPlaying() && !bIsPaused)
	{
		return;
	}

	OutlinerPanel->SelectActor(nullptr);
	DetailsPanel->SelectComponent(nullptr);

	Actor->Destroy();
}

void UEditorEngine::DeleteComponent(UActorComponent* Component)
{
	if (!Component)
	{
		return;
	}
	if (IsPlaying() && !bIsPaused)
	{
		return;
	}

	AActor* Owner = Component->GetOwner();
	if (!Owner || !Owner->CanUserDestroyComponent(Component))
	{
		return;
	}

	USceneComponent* NextSelection = nullptr;
	if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
	{
		NextSelection = SceneComponent->GetAttachParent();
		if (NextSelection && NextSelection->GetOwner() != Owner)
		{
			NextSelection = nullptr;
		}
	}

	DetailsPanel->SelectComponent(nullptr);
	Outline->SetTarget(nullptr);
	Owner->DestroyComponent(Component);

	OutlinerPanel->SelectActor(Owner);
	if (NextSelection && NextSelection != Owner->GetRootComponent())
	{
		DetailsPanel->SelectComponent(NextSelection);
	}
}

void UEditorEngine::MakeRootComponent(USceneComponent* NewRoot)
{
	AActor* Owner = NewRoot ? NewRoot->GetOwner() : nullptr;
	if (!Owner || Owner->GetRootComponent() == NewRoot)
	{
		return;
	}
	if (IsPlaying() && !bIsPaused)
	{
		return;
	}

	USceneComponent* OldRoot = Owner->GetRootComponent();

	DetailsPanel->SelectComponent(nullptr);
	Outline->SetTarget(nullptr);
	Owner->ReplaceRootComponent(NewRoot);
	if (Owner->IsDefaultSceneRoot(OldRoot))
	{
		Owner->DestroyComponent(OldRoot);
	}

	OutlinerPanel->SelectActor(Owner);
}

void UEditorEngine::StartPlayInEditorSession()
{
	// 별도 창 모드면 패널 View는 모두 에디터 월드로 두고 창 하나에만 PIE 월드를 그린다.
	// 창을 만들지 못하면 선택한 View에서 실행한다.
	if (PlayMode == EPlayModeType::InEditorFloating && CreatePIEWindow())
	{
		PIEViewIndex = PIEWindowViewIndex;
	}
	else
	{
		// 지금 편집 중인 View 하나만 PIE 월드를 보여준다.
		PIEViewIndex = ViewportsPanel->GetEditorViewIndex();
	}

	FWorldContext& PIEContext = CreateNewWorldContext(EWorldType::PIE);
	PlayWorld = CreatePIEWorldByDuplication(PIEContext, EditorWorld);

	// 씬에 카메라가 없으면 기본 카메라가 생기므로, 지금 편집 중인 View의 시점에서 시작하게 맞춘다.
	if (PlayWorld)
	{
		const FViewCamera& EditorCam = ViewportsPanel->GetViewportClient(ViewportsPanel->GetEditorViewIndex()).GetCamera();
		FMinimalViewInfo StartView{};
		StartView.SetLocation(EditorCam.Transform.Location);
		StartView.SetRotation(EditorCam.Transform.Rotation.ToFRotator());
		StartView.SetFieldOfView(EditorCam.Projection.FovDegrees);
		StartView.SetNearZ(EditorCam.Projection.NearClip);
		StartView.SetFarZ(EditorCam.Projection.FarClip);
		StartView.SetIsOrthogonal(EditorCam.Projection.Mode == EProjectionMode::Orthographic);
		StartView.SetOrthoWidth(EditorCam.Projection.OrthoWidth);
		PlayWorld->SetPlayStartView(StartView);
	}

	OnActiveWorldChanged();
}

UWorld* UEditorEngine::CreatePIEWorldByDuplication(FWorldContext& PIEContext, UWorld* InEditorWorld)
{
	UWorld* NewPIEWorld = UWorld::GetDuplicatedWorldForPIE(InEditorWorld);
	PIEContext.SetCurrentWorld(NewPIEWorld);
	return NewPIEWorld;
}

// 라이트 아이콘처럼 화면 크기를 위한 에디터 표시와 메시가 없어 크기가 0인 프리미티브는 액터의 크기로 치지 않는다.
static bool TryGetActorFramingBounds(const AActor& Actor, FBox& OutBounds)
{
	constexpr float MinimumExtent = 1.0e-4f;
	bool bHasBounds = false;
	for (UActorComponent* Component : Actor.GetComponents())
	{
		const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
		if (!Primitive)
		{
			continue;
		}

		const UBillboardComponent* Billboard = Cast<UBillboardComponent>(Component);
		const UStaticMeshComponent* StaticMesh = Cast<UStaticMeshComponent>(Component);
		if ((Billboard && Billboard->IsEditorIcon()) || (StaticMesh && StaticMesh->IsEditorIcon()))
		{
			continue;
		}

		const FBox Bounds = Primitive->CalcBounds();
		const FVector Size = Bounds.Max - Bounds.Min;
		if (Size.X <= MinimumExtent && Size.Y <= MinimumExtent && Size.Z <= MinimumExtent)
		{
			continue;
		}

		if (bHasBounds)
		{
			OutBounds.Expand(Bounds);
		}
		else
		{
			OutBounds = Bounds;
			bHasBounds = true;
		}
	}
	return bHasBounds;
}

void UEditorEngine::FocusViewportOnActor(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	int32 ViewIndex = ViewportsPanel->GetActiveViewIndex();
	if (!ViewportsPanel->IsViewActive(ViewIndex))
	{
		ViewIndex = ViewportsPanel->GetEditorViewIndex();
	}
	if (!ViewportsPanel->IsViewActive(ViewIndex))
	{
		return;
	}

	// PIE 중 아웃라이너는 PlayWorld를 보여주지만 PIE가 아닌 View는 EditorWorld를 그리므로 같은 월드일 때만 옮긴다.
	FEditorViewportClient& Client = ViewportsPanel->GetViewportClient(ViewIndex);
	if (GetWorldForView(ViewIndex) != Actor->GetWorld() || Client.HasCameraOverride())
	{
		HTR_LOG(Info, "Cannot frame {}: the active view is not showing its world with an editor camera.", Actor->GetName());
		return;
	}

	FBox Bounds;
	if (TryGetActorFramingBounds(*Actor, Bounds))
	{
		Client.FrameBounds((Bounds.Min + Bounds.Max) * 0.5f, (Bounds.Max - Bounds.Min) * 0.5f);
	}
	else
	{
		// 라이트·빈 액터는 크기가 없으므로 한 변 2m 상자를 비추는 거리로 위치를 보여준다.
		constexpr float DefaultHalfExtent = 1.0f;
		Client.FrameBounds(Actor->GetActorLocation(), FVector(DefaultHalfExtent, DefaultHalfExtent, DefaultHalfExtent));
	}
}

void UEditorEngine::OnActiveWorldChanged()
{
	ResetSceneSelection();
	OutlinerPanel->SetWorld(GetActiveWorld());
	SettingsPanel->SetWorld(GetActiveWorld());
}

void UEditorEngine::EndPlayMap()
{
	if (!PlayWorld)
	{
		return;
	}

	ResetSceneSelection();
	if (PIEViewIndex != InvalidViewIndex)
	{
		GetViewportClient(PIEViewIndex).ClearCameraOverride();
	}
	if (FSceneTransitionManager* SceneTransitions = GetSceneTransitionManager())
	{
		SceneTransitions->CancelForWorld(PlayWorld);
	}
	PlayWorld->EndPlay();
	// 스크립트가 마우스를 잠근 채 끝나도 에디터가 마우스를 되찾는다.
	FMouseCapture::Release();
	bPIEMouseReleased = false;
	PlayWorld->ClearWorld();
	DestroyWorldContext(PlayWorld);
	delete PlayWorld;
	PlayWorld = nullptr;
	PIEViewIndex = InvalidViewIndex;
	DestroyPIEWindow();
	OnActiveWorldChanged();
}

bool UEditorEngine::CreatePIEWindow()
{
	constexpr int32 DefaultWidth = 1280;
	constexpr int32 DefaultHeight = 720;

	TUniquePtr<FWindow> NewWindow = MakeUnique<FWindow>();
	if (!NewWindow->Create(::GetModuleHandleW(nullptr), DefaultWidth, DefaultHeight, L"Hitori Engine - PIE"))
	{
		HTR_LOG(Error, "Failed to create PIE window. Falling back to the selected viewport.");
		return false;
	}
	// 닫기 버튼은 PIE만 끝내고, ImGui는 메인 창 입력만 받도록 훅을 끊는다.
	NewWindow->SetQuitOnClose(false);
	NewWindow->SetReceivesWndProcHook(false);

	PIEWindow = std::move(NewWindow);
	PIESwapchain = MakeUnique<FSwapchain>(GetEngineLoop().GetRenderDevice(), PIEWindow.get());
	PIETargetWidth = 0;
	PIETargetHeight = 0;
	PIEWindow->Show();
	return true;
}

void UEditorEngine::DestroyPIEWindow()
{
	if (!PIEWindow)
	{
		return;
	}

	PIEWindowViewportClient.SetViewRect({});
	PIEWindowViewportClient.ClearCameraOverride();

	PIERenderingInfo = {};
	PIEColorTarget.reset();
	PIEDepthTarget.reset();
	PIEFxaaTarget.reset();
	PIEHDRTarget.reset();
	PIETargetWidth = 0;
	PIETargetHeight = 0;
	bPIEWindowRendered = false;

	// Swapchain이 창 핸들을 출력 대상으로 쥐고 있으므로 창보다 먼저 놓는다.
	PIESwapchain.reset();
	PIEWindow->Destroy();
	PIEWindow.reset();

	// PIE 창이 포커스를 갖고 있었다면 에디터로 돌려준다.
	::SetForegroundWindow(MainWindow->GetHandle());
}

bool UEditorEngine::UpdatePIEWindowTargets()
{
	const uint32 Width = PIEWindow->GetWidth();
	const uint32 Height = PIEWindow->GetHeight();
	// 최소화 중에는 그릴 대상이 없다.
	if (Width == 0 || Height == 0 || ::IsIconic(PIEWindow->GetHandle()))
	{
		PIEWindowViewportClient.SetViewRect({});
		return false;
	}

	if (PIEWindow->CheckResized())
	{
		PIESwapchain->Resize(Width, Height);
	}

	if (Width != PIETargetWidth || Height != PIETargetHeight)
	{
		// 패널 View 슬롯과 같은 구성이다. Fog가 깊이를 읽으므로 깊이는 TYPELESS로 만든다.
		D3D11_TEXTURE2D_DESC Desc{};
		Desc.Width = Width;
		Desc.Height = Height;
		Desc.MipLevels = 1;
		Desc.ArraySize = 1;
		Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		Desc.SampleDesc.Count = 1;
		Desc.Usage = D3D11_USAGE_DEFAULT;
		Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		PIEColorTarget = RenderCommand::CreateTexture2D(Desc);
		PIEFxaaTarget = RenderCommand::CreateTexture2D(Desc);

		Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		PIEDepthTarget = RenderCommand::CreateTexture2D(Desc);

		Desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		PIEHDRTarget = RenderCommand::CreateTexture2D(Desc);

		PIERenderingInfo.ColorRenderTargets.Reset();
		PIERenderingInfo.ViewportSetting.Width = Width;
		PIERenderingInfo.ViewportSetting.Height = Height;
		FRenderingDesc ColorDesc{};
		ColorDesc.Texture = PIEColorTarget.get();
		PIERenderingInfo.ColorRenderTargets.Add(ColorDesc);
		PIERenderingInfo.DepthStencil.Texture = PIEDepthTarget.get();

		PIETargetWidth = Width;
		PIETargetHeight = Height;
	}

	PIEWindowViewportClient.SetViewRect({ 0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height) });
	return true;
}

void UEditorEngine::CopyPIEWindowToBackbuffer()
{
	const FRenderingInfo& BackbufferInfo = PIESwapchain->GetRenderingInfo();
	if (BackbufferInfo.ColorRenderTargets.Num() == 0)
	{
		return;
	}

	// 백버퍼는 SRV가 없어 FXAA·Fog 입력으로 쓸 수 없으므로 오프스크린 결과를 통째로 복사한다.
	// 창 크기 변경 직후 한 프레임은 크기가 어긋날 수 있어 그때는 건너뛴다.
	FTexture2D* Backbuffer = BackbufferInfo.ColorRenderTargets[0].Texture;
	if (!Backbuffer || !PIEColorTarget ||
		BackbufferInfo.ViewportSetting.Width != PIETargetWidth ||
		BackbufferInfo.ViewportSetting.Height != PIETargetHeight)
	{
		return;
	}

	RenderCommand::CopyTexture(Backbuffer, PIEColorTarget.get());
	bPIEWindowRendered = true;
}

FEditorViewportClient& UEditorEngine::GetViewportClient(const int32 ViewIndex)
{
	return ViewIndex == PIEWindowViewIndex ? PIEWindowViewportClient : ViewportsPanel->GetViewportClient(ViewIndex);
}

const FEditorViewportClient& UEditorEngine::GetViewportClient(const int32 ViewIndex) const
{
	return ViewIndex == PIEWindowViewIndex ? PIEWindowViewportClient : ViewportsPanel->GetViewportClient(ViewIndex);
}

bool UEditorEngine::IsViewActive(const int32 ViewIndex) const
{
	if (ViewIndex == PIEWindowViewIndex)
	{
		return PIEWindowViewportClient.HasCameraOverride() && PIEWindowViewportClient.HasValidViewRect();
	}
	return ViewportsPanel->IsViewActive(ViewIndex);
}

FTexture2D* UEditorEngine::GetFxaaTargetForView(const int32 ViewIndex) const
{
	if (ViewIndex == PIEWindowViewIndex)
	{
		return PIEFxaaTarget.get();
	}
	return ViewportsPanel->GetFxaaTarget(ViewIndex);
}

FTexture2D* UEditorEngine::GetHDRTargetForView(const int32 ViewIndex) const
{
	if (ViewIndex == PIEWindowViewIndex)
	{
		return PIEHDRTarget.get();
	}
	return ViewportsPanel->GetHDRTarget(ViewIndex);
}


// 데칼 페이드 종료처럼 게임 로직이 실행 중에 지우는 경우에도 패널이 해제된 포인터를 잡지 않게 한다.
void UEditorEngine::OnActorContentDestroying(const AActor* Owner)
{
	if (DetailsPanel && Owner == DetailsPanel->GetTargetActor())
	{
		ResetSceneSelection();
	}
}

// 씬 변경으로 무효화된 에디터의 선택 참조를 모두 해제한다.
void UEditorEngine::ResetSceneSelection()
{
	GizmoTarget = nullptr;
	Outline->SetTarget(nullptr);
	DetailsPanel->SetTarget(nullptr);
	OutlinerPanel->SelectActor(nullptr);
}

// 새 씬 생성이 성공하면 에디터 선택 상태를 초기화한다.
void UEditorEngine::CreateNewScene()
{
	if (IsPlaying())
	{
		HTR_LOG(Warning, "Cannot create a new scene during PIE. Stop the play session first.");
		return;
	}
	if (!FEditorFileUtils::NewScene(EditorWorld))
	{
		return;
	}

	ResetSceneSelection();
}

// 씬 불러오기가 성공하면 에디터 선택 상태를 초기화한다.
void UEditorEngine::OpenScene()
{
	if (IsPlaying())
	{
		HTR_LOG(Warning, "Cannot open a scene during PIE. Stop the play session first.");
		return;
	}
	if (!FEditorFileUtils::LoadScene(EditorWorld))
	{
		return;
	}

	ResetSceneSelection();
}

// 공통 파일 유틸리티로 현재 씬을 저장한다.
void UEditorEngine::SaveCurrentScene()
{
	if (IsPlaying())
	{
		HTR_LOG(Warning, "Cannot save during PIE. Stop the play session first.");
		return;
	}
	FEditorFileUtils::SaveScene(EditorWorld);
}

// 공통 파일 유틸리티로 새 경로에 씬을 저장한다.
void UEditorEngine::SaveSceneAs()
{
	if (IsPlaying())
	{
		HTR_LOG(Warning, "Cannot save during PIE. Stop the play session first.");
		return;
	}
	FEditorFileUtils::SaveSceneAs(EditorWorld);
}


// Script UI coordinates use the same render-target dimensions as the UI renderer.
FGameViewportInfo UEditorEngine::GetGameViewportInfo(const UWorld* InWorld) const
{
	FGameViewportInfo Info;
	if (!InWorld || InWorld != PlayWorld || PIEViewIndex == InvalidViewIndex)
	{
		return Info;
	}

	if (PIEWindow)
	{
		Info.Size = FVector2(static_cast<float>(PIEWindow->GetWidth()), static_cast<float>(PIEWindow->GetHeight()));
		POINT Mouse{};
		if (::GetCursorPos(&Mouse) && ::ScreenToClient(PIEWindow->GetHandle(), &Mouse))
		{
			Info.MousePosition = FVector2(static_cast<float>(Mouse.x), static_cast<float>(Mouse.y));
			Info.bAcceptsMouseInput = ::GetForegroundWindow() == PIEWindow->GetHandle() && Mouse.x >= 0 && Mouse.y >= 0 && Mouse.x < Info.Size.X && Mouse.y < Info.Size.Y;
		}
		return Info;
	}

	if (!ViewportsPanel || !MainWindow || !IsViewActive(PIEViewIndex))
	{
		return Info;
	}

	const FRect& Rect = GetViewportClient(PIEViewIndex).GetViewRect();
	if (Rect.Width <= 0.0f || Rect.Height <= 0.0f)
	{
		return Info;
	}

	const auto& Viewport = ViewportsPanel->GetRenderingInfo(PIEViewIndex).ViewportSetting;
	Info.Size = FVector2(static_cast<float>(Viewport.Width), static_cast<float>(Viewport.Height));
	const FVector2 LocalMouse = ViewportsPanel->GetLocalMousePosition();
	const float X = LocalMouse.X - Rect.X;
	const float Y = LocalMouse.Y - Rect.Y;
	Info.MousePosition = FVector2(X * Info.Size.X / Rect.Width, Y * Info.Size.Y / Rect.Height);
	Info.bAcceptsMouseInput = ViewportsPanel->IsHovered() && !ImGui::GetIO().WantTextInput && ::GetForegroundWindow() == MainWindow->GetHandle() && X >= 0.0f && Y >= 0.0f && X < Rect.Width && Y < Rect.Height;
	return Info;
}

bool UEditorEngine::SetGameMouseCapture(const UWorld* InWorld, bool Enabled)
{
	if (!InWorld || InWorld != PlayWorld || PIEViewIndex == InvalidViewIndex)
	{
		return false;
	}

	// 일시정지 중에는 편집할 수 있게 마우스를 놓는다.
	if (!Enabled || bIsPaused)
	{
		FMouseCapture::Release();
		return false;
	}

	HWND Window = nullptr;
	RECT Bounds{};
	if (PIEWindow)
	{
		Window = PIEWindow->GetHandle();
		::GetClientRect(Window, &Bounds);
		::MapWindowPoints(Window, nullptr, reinterpret_cast<POINT*>(&Bounds), 2);
	}
	else
	{
		if (!MainWindow || !ViewportsPanel || !IsViewActive(PIEViewIndex))
		{
			return false;
		}
		// 패널 안의 PIE View 영역에만 가둔다.
		const FRect& Rect = GetViewportClient(PIEViewIndex).GetViewRect();
		const FVector2 Origin = ViewportsPanel->GetContentScreenOrigin();
		Window = MainWindow->GetHandle();
		Bounds.left = static_cast<LONG>(Origin.X + Rect.X);
		Bounds.top = static_cast<LONG>(Origin.Y + Rect.Y);
		Bounds.right = static_cast<LONG>(Origin.X + Rect.X + Rect.Width);
		Bounds.bottom = static_cast<LONG>(Origin.Y + Rect.Y + Rect.Height);
	}
	return FMouseCapture::Update(Window, Bounds, true);
}

void UEditorEngine::RequestQuitGame(const UWorld* InWorld)
{
	// A game menu closes the PIE session, not the editor and its unsaved scene.
	if (InWorld && InWorld == PlayWorld)
	{
		RequestEndPlayMap();
	}
}
