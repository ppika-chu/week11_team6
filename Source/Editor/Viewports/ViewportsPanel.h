#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Editor/Viewports/LevelViewportLayout.h"
#include "Render/RenderingInfo.h"

// 레벨 에디터의 네 View를 담는 컨테이너. 화면 배치(Layout)와 View별 ViewportClient·렌더 타깃을 소유하고
// 레이아웃·View 종류·Splitter UI와 카메라 입력 대상 선택을 처리한다.
class FViewportsPanel : public IEditorPanel
{
public:
	static constexpr int32 ViewCount = FLevelViewportLayout::ViewCount;

	// 네 View의 렌더 타깃을 초기화한다.
	bool Init() override;
	void Tick(float DeltaTime) override;
	// 각 View Texture와 Splitter·Layout·View 종류 조작 UI를 ImGui 패널에 그리고 변경을 바로 반영한다.
	void OnRender() override;
	const char* GetPanelName() const override { return "Viewports"; }

	// 기본 원근 투영으로 네 View의 초기 카메라와 View 종류를 구성한다.
	void InitializeViewports(const FCameraProjection& PerspectiveProjection);

	// 패널 크기로 레이아웃·View 영역·렌더 타깃을 갱신하고 카메라 입력을 입력 대상 View에 전달한다.
	// 프레임마다 렌더 전에 한 번 부른다.
	void UpdateViewports(float DeltaTime, float MoveSpeed, float MouseSensitivity);

	FLevelViewportLayout& GetLayout() { return Layout; }
	const FLevelViewportLayout& GetLayout() const { return Layout; }

	FEditorViewportClient& GetViewportClient(int32 ViewIndex);
	const FEditorViewportClient& GetViewportClient(int32 ViewIndex) const;
	bool IsViewActive(int32 ViewIndex) const { return Layout.IsViewActive(ViewIndex); }

	// 현재 입력을 받는 View. 없으면 InvalidViewIndex다.
	int32 GetActiveViewIndex() const { return ActiveViewIndex; }
	// 우클릭으로 카메라 입력을 Capture 중인 View. 없으면 InvalidViewIndex다.
	int32 GetCapturedViewIndex() const { return CapturedViewIndex; }
	// 입력 영역 밖에서도 마지막 편집 대상을 유지하며 Single에서는 확대 View를 반환한다.
	int32 GetEditorViewIndex() const;
	void SetEditorViewIndex(int32 ViewIndex);

	// 커서가 입력 View 위에 있을 때 그 View의 피킹 Ray를 만든다.
	bool TryGetActiveViewRay(FRay& OutRay) const;

	const FRenderingInfo& GetRenderingInfo(int32 ViewIndex) const;
	// FXAA 결과를 임시로 받는 View별 텍스처. ColorTarget과 크기·포맷이 같다.
	FTexture2D* GetFxaaTarget(int32 ViewIndex) const;
	// HDR 결과를 임시로 받는 View별 텍스처. ColorTarget과 크기는 같고 포맷은 float.
	FTexture2D* GetHDRTarget(int32 ViewIndex) const;

	FVector2 GetContentSize() const { return { ContentSize.x, ContentSize.y }; }
	FVector2 GetLocalMousePosition() const;
	// View 영역 왼쪽 위의 화면 좌표. ImGui 멀티 뷰포트를 켜 두었으므로 OS 화면 좌표와 같다.
	FVector2 GetContentScreenOrigin() const { return { ContentOrigin.x, ContentOrigin.y }; }
	bool IsHovered() const;

private:
	struct FViewSlot
	{
		// View 하나의 Color/Depth 타깃과 렌더 정보를 담는다.
		uint32 Width = 0;
		uint32 Height = 0;
		TUniquePtr<FTexture2D> ColorTarget;
		TUniquePtr<FTexture2D> DepthTarget;
		TUniquePtr<FTexture2D> FxaaTarget;
		TUniquePtr<FTexture2D> HDRTarget;
		FRenderingInfo RenderingInfo{};
	};

	// 레이아웃을 계산해 Client 영역과 렌더 타깃 크기에 반영하고 숨겨진 View의 입력 상태를 정리한다.
	void UpdateLayout();
	// Capture와 입력 대상을 결정하고 키·마우스·휠 입력을 해당 Client에 전달한다.
	void UpdateInput(float DeltaTime, float MoveSpeed, float MouseSensitivity);
	// Rect 크기가 바뀐 Slot의 Color/Depth Texture와 렌더 정보를 다시 만든다.
	void ResizeSlot(FViewSlot& Slot, uint32 Width, uint32 Height);
	// 콘솔 stat 명령으로 켜진 항목을 해당 View의 좌상단에 겹쳐 그린다.
	void DrawStatOverlay(ImDrawList* DrawList, const ImVec2& ViewMin) const;

	FLevelViewportLayout Layout;
	FEditorViewportClient ViewportClients[ViewCount];
	FViewSlot Slots[ViewCount]{};
	int32 ActiveViewIndex = InvalidViewIndex;
	int32 CapturedViewIndex = InvalidViewIndex;
	int32 EditorViewIndex = 0;

	ImVec2 ContentOrigin{};
	ImVec2 ContentSize{ 1.0f, 1.0f };
	bool bHovered = false;
	bool bWheelOverView = false;
	bool bStatResetButtonVisible = false;
	ImVec2 StatResetButtonMin{};
	ImVec2 StatResetButtonMax{};
	// 누른 뒤 커서가 Splitter 영역을 벗어나도 버튼을 놓을 때까지 드래그를 유지한다.
	bool bDraggingVerticalSplitter = false;
	bool bDraggingHorizontalSplitter = false;
};
