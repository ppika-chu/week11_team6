#pragma once

#include <format>
#include "Editor/EditorUI/EditorPanel.h"
#include <functional>
#include <utility>
#include "Engine/World.h"

struct FTransform;
class FRenderer;
class ULightComponent;

using DetailsSelectionCallback = std::function<void(UActorComponent*)>;
using DetailsDeleteComponentCallback = std::function<void(UActorComponent*)>;
using DetailsMakeRootCallback = std::function<void(USceneComponent*)>;

class FDetailsPanel : public IEditorPanel
{
public:
	FDetailsPanel() = default;
	~FDetailsPanel();

	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;

	void SelectComponent(UActorComponent* Component);
	void DrawCompoenetList(AActor* SelectedActor, UActorComponent*& OutSelectedComponent);

	const char* GetPanelName() const override { return "Details"; }

	void SetTarget(AActor* InTargetActor)
	{
		TargetActor = InTargetActor;
		TargetComponent = InTargetActor ? InTargetActor->GetRootComponent() : nullptr;
	}
	void SetTargetComponent(UActorComponent* InTargetComponent) { TargetComponent = InTargetComponent; }
	const AActor* GetTargetActor() const { return TargetActor; }

	void SetSelectionCallback(DetailsSelectionCallback InCallback) { Callback = InCallback; }
	void SetDeleteComponentCallback(DetailsDeleteComponentCallback InCallback) { DeleteCallback = InCallback; }
	void SetMakeRootCallback(DetailsMakeRootCallback InCallback) { MakeRootCallback = InCallback; }

	void SetWorld(UWorld* InWorld) { World = InWorld; }
	// Renderer를 소유하지 않으며, 미리보기 조회에만 사용한다.
	void SetRenderer(const FRenderer* InRenderer) { Renderer = InRenderer; }
	void SetShadowCascadeDebugCallbacks(std::function<bool(uint32)> InGetter, std::function<void(uint32, bool)> InSetter)
	{
		ShadowCascadeDebugGetter = std::move(InGetter);
		ShadowCascadeDebugSetter = std::move(InSetter);
	}
	ImFont* GetCustomFont() { return CustomFont; }

private:
	void DrawSceneComponentNode(USceneComponent* Component, USceneComponent* Root, UActorComponent*& OutSelectedComponent);
	void TryReparent(USceneComponent* DroppedComponent, USceneComponent* DragComponent);
	// 선택한 라이트에 대응하는 현재 프레임의 Depth Map을 표시한다.
	void DrawShadowMapPreview(const ULightComponent& Light);

	UWorld* World = nullptr;
	// 텍스처와 SRV는 저장하지 않고, 매 프레임 Renderer에서 조회한다.
	const FRenderer* Renderer = nullptr;
	ImFont* CustomFont = nullptr;

	AActor* TargetActor = nullptr;
	UActorComponent* TargetComponent = nullptr;
	DetailsSelectionCallback Callback;
	DetailsDeleteComponentCallback DeleteCallback;
	DetailsMakeRootCallback MakeRootCallback;

	UActorComponent* PendingDeleteComponent = nullptr;
	USceneComponent* PendingMakeRootComponent = nullptr;
	UActorComponent* EditingNameComponent = nullptr;
	char ComponentNameEditBuffer[256] = {};
	bool bFocusNameEdit = false;

	void BeginComponentRename(UActorComponent* Component);
	void DrawComponentRenameField(UActorComponent* Component);
	void DrawComponentRenameMenuItem(UActorComponent* Component);
	void DrawComponentDeleteMenuItem(UActorComponent* Component);
	void HandleComponentRenameShortcut(UActorComponent* Component, const UActorComponent* SelectedComponent);

	USceneComponent* PendingDroppedComponent = nullptr;
	USceneComponent* PendingDragComponent = nullptr;

	// 숫자 선택값만 유지하고 Proxy·텍스처는 매 프레임 다시 조회한다.
	uint32 ShadowPreviewViewId = 0;
	uint32 ShadowPreviewProjectionIndex = 0;

	// 실제 표시 상태는 Client에만 저장한다.
	std::function<bool(uint32)> ShadowCascadeDebugGetter;
	std::function<void(uint32, bool)> ShadowCascadeDebugSetter;
};

// 공통 UObject 편집 UI. 등록된 편집 가능 Property가 바뀌면 true를 반환한다.
bool DrawObjectProperties(UObject* Object, ImFont* CustomFont);
