#include "EnginePCH.h"
#include "Editor/Viewports/ViewportsPanel.h"

#include "Core/StatOverlay.h"
#include "Core/Stats/LightweightStats.h"
#include "Input/InputSystem.h"
#include "Render/RenderCommand.h"
#include "Editor/EditorUI/ShowFlagUI.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include <algorithm>
#include <cassert>
#include <format>

#include "Core/Stats/EditorStats.h"
#include "Core/Stats/ShadowStats.h"
#include "Core/Stats/DecalStats.h"
#include "Core/Stats/SkinningStats.h"
#include "Render/ShadowSettings.h"

namespace
{
constexpr ImU32 SplitterColor = IM_COL32(55, 55, 55, 230);
constexpr ImU32 SplitterHoverColor = IM_COL32(255, 192, 0, 255);
constexpr const char* ViewportTypeLabels[] = {
	"Perspective", "Ortho (Current)", "Top", "Bottom", "Front", "Back", "Left", "Right"
};

// Stat Overlay
constexpr float StatOverlayMargin = 8.0f;
constexpr float StatOverlayPadding = 6.0f;
constexpr ImU32 StatOverlayBackgroundColor = IM_COL32(0, 0, 0, 140);
constexpr ImU32 TitleColor = IM_COL32(255, 210, 60, 255);
constexpr ImU32 ValueColor = IM_COL32(235, 235, 235, 255);
}

// 네 View의 렌더 타깃을 최소 크기로 초기화한다.
bool FViewportsPanel::Init()
{
	for (FViewSlot& Slot : Slots)
	{
		ResizeSlot(Slot, 1, 1);
	}
	return true;
}

// 패널의 프레임 갱신 인터페이스이며 별도 계산은 하지 않는다.
void FViewportsPanel::Tick(float DeltaTime)
{
	(void)DeltaTime;
}

// 첫 View는 원근, 나머지는 Top·Front·Left 직교로 시작한다.
void FViewportsPanel::InitializeViewports(const FCameraProjection& PerspectiveProjection)
{
	Layout.SetLayoutMode(ELayoutMode::QuadSplit);
	// 화면 정면은 +X, 화면 오른쪽은 +Y가 되는 구도로 시작한다.
	const FViewCamera InitialCamera{ { { -11.665390f, 6.117728f, 9.921079f },
										 { 0.081251f, 0.256328f, -0.291035f, 0.918146f } },
		PerspectiveProjection };
	for (FEditorViewportClient& Client : ViewportClients)
	{
		Client.SetCamera(InitialCamera);
	}
	ViewportClients[1].SetViewportType(ELevelViewportType::Top);
	ViewportClients[2].SetViewportType(ELevelViewportType::Front);
	ViewportClients[3].SetViewportType(ELevelViewportType::Left);
}

void FViewportsPanel::UpdateViewports(const float DeltaTime, const float MoveSpeed, const float MouseSensitivity)
{
	UpdateLayout();
	UpdateInput(DeltaTime, MoveSpeed, MouseSensitivity);
	// 이동 도중 입력 대상이 다른 View로 바뀌어도 끝까지 진행되도록 모든 View를 갱신한다.
	for (FEditorViewportClient& Client : ViewportClients)
	{
		Client.TickFrameTransition(DeltaTime);
	}
	// 겹친 창은 Hover 선택에서 제외하고 우클릭 Capture를 우선한다.
	if (IsHovered() || CapturedViewIndex != InvalidViewIndex)
	{
		SetEditorViewIndex(ActiveViewIndex);
	}
}

// 창 크기와 Single/Quad 전환으로 Aspect가 바뀌어도 카메라 투영과 렌더 타깃을 함께 맞춘다.
void FViewportsPanel::UpdateLayout()
{
	Layout.Update(GetContentSize(), EditorUIScale::Px(SplitterThickness));
	for (int32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex)
	{
		const FRect& Rect = Layout.GetViewRect(ViewIndex);
		ViewportClients[ViewIndex].SetViewRect(Rect);
		if (!Layout.IsViewActive(ViewIndex))
		{
			continue;
		}

		FViewSlot& Slot = Slots[ViewIndex];
		const uint32 Width = static_cast<uint32>(std::max(1.0f, Rect.Width));
		const uint32 Height = static_cast<uint32>(std::max(1.0f, Rect.Height));
		if (Width != Slot.Width || Height != Slot.Height)
		{
			ResizeSlot(Slot, Width, Height);
		}
	}

	if (CapturedViewIndex != InvalidViewIndex && !IsViewActive(CapturedViewIndex))
	{
		CapturedViewIndex = InvalidViewIndex;
	}
	// Capture가 없으면 마지막 선택을 유지하고, 숨겨진 View만 레이아웃에 맞게 보정한다.
	if (CapturedViewIndex != InvalidViewIndex)
	{
		ActiveViewIndex = CapturedViewIndex;
	}
	if (!IsViewActive(ActiveViewIndex))
	{
		const int32 SingleViewIndex = Layout.GetSingleViewIndex();
		ActiveViewIndex = Layout.GetLayoutMode() == ELayoutMode::Single && IsViewActive(SingleViewIndex)
			? SingleViewIndex
			: InvalidViewIndex;
	}
}

void FViewportsPanel::UpdateInput(const float DeltaTime, const float MoveSpeed, const float MouseSensitivity)
{
	const int WheelDelta = FInputSystem::GetWheelDelta(); // 이 Hook은 프레임마다 정확히 한 번만 호출한다.
	if (ImGui::IsKeyReleased(ImGuiKey_MouseRight))
	{
		CapturedViewIndex = InvalidViewIndex;
	}
	const int32 PointerViewIndex = Layout.FindViewAt(GetLocalMousePosition());
	// 우클릭 시작 위치를 Capture하고, Capture 밖에서는 좌클릭으로만 선택을 바꾼다.
	// 다른 창이 View 위를 덮고 있으면 그 창의 입력이므로 Capture하지 않는다.
	if (CapturedViewIndex == InvalidViewIndex && IsHovered() && IsViewActive(PointerViewIndex))
	{
		if (FInputSystem::IsMousePressed(EMouseButton::Right))
		{
			CapturedViewIndex = PointerViewIndex;
		}
		else if (FInputSystem::IsMousePressed(EMouseButton::Left))
		{
			ActiveViewIndex = PointerViewIndex;
		}
	}
	if (CapturedViewIndex != InvalidViewIndex)
	{
		ActiveViewIndex = CapturedViewIndex;
	}
	if (!IsViewActive(ActiveViewIndex))
	{
		return;
	}

	// 키와 마우스 드래그는 우클릭으로 캡처한 View에만 전달한다.
	const bool bCameraCaptured = FInputSystem::IsMouseDown(EMouseButton::Right) && CapturedViewIndex == ActiveViewIndex;
	FCameraMoveInput RawInput{};
	if (bCameraCaptured)
	{
		const auto AxisValue = [](EKeyCode Positive, EKeyCode Negative)
		{
			return (FInputSystem::IsKeyDown(Positive) ? 1.0f : 0.0f) - (FInputSystem::IsKeyDown(Negative) ? 1.0f : 0.0f);
		};
		RawInput.MoveAxis = {
			AxisValue(EKeyCode::W, EKeyCode::S),
			AxisValue(EKeyCode::D, EKeyCode::A),
			AxisValue(EKeyCode::E, EKeyCode::Q)
		};
		RawInput.MouseDelta = {
			static_cast<float>(FInputSystem::GetMouseDeltaX()),
			static_cast<float>(FInputSystem::GetMouseDeltaY())
		};
	}

	// 이미 한 번 읽은 휠 값을 대상 View에만 전달한다.
	RawInput.ZoomDelta = bCameraCaptured || (bWheelOverView && PointerViewIndex == ActiveViewIndex)
		? static_cast<float>(WheelDelta)
		: 0.0f;
	ViewportClients[ActiveViewIndex].UpdateCamera(DeltaTime, RawInput, MoveSpeed, MouseSensitivity, bCameraCaptured);
}

FEditorViewportClient& FViewportsPanel::GetViewportClient(const int32 ViewIndex)
{
	assert(ViewIndex >= 0 && ViewIndex < ViewCount);
	return ViewportClients[ViewIndex];
}

const FEditorViewportClient& FViewportsPanel::GetViewportClient(const int32 ViewIndex) const
{
	assert(ViewIndex >= 0 && ViewIndex < ViewCount);
	return ViewportClients[ViewIndex];
}

int32 FViewportsPanel::GetEditorViewIndex() const
{
	return Layout.GetLayoutMode() == ELayoutMode::Single ? Layout.GetSingleViewIndex() : EditorViewIndex;
}

void FViewportsPanel::SetEditorViewIndex(const int32 ViewIndex)
{
	if (ViewIndex >= 0 && ViewIndex < ViewCount)
	{
		EditorViewIndex = ViewIndex;
	}
}

bool FViewportsPanel::TryGetActiveViewRay(FRay& OutRay) const
{
	if (!IsViewActive(ActiveViewIndex))
	{
		return false;
	}
	// Splitter gutter나 다른 View 위의 좌표를 이전 Active View의 Ray로 잘못 해석하지 않는다.
	const FVector2 LocalMousePosition = GetLocalMousePosition();
	if (Layout.FindViewAt(LocalMousePosition) != ActiveViewIndex)
	{
		return false;
	}
	const FEditorViewportClient& Client = ViewportClients[ActiveViewIndex];
	const FRect& Rect = Client.GetViewRect();
	OutRay = Client.DeprojectScreen({ LocalMousePosition.X - Rect.X, LocalMousePosition.Y - Rect.Y });
	return true;
}

// 인덱스를 검사해 해당 View의 렌더 정보를 반환한다.
const FRenderingInfo& FViewportsPanel::GetRenderingInfo(const int32 ViewIndex) const
{
	assert(ViewIndex >= 0 && ViewIndex < ViewCount);
	return Slots[ViewIndex].RenderingInfo;
}

FTexture2D* FViewportsPanel::GetFxaaTarget(const int32 ViewIndex) const
{
	assert(ViewIndex >= 0 && ViewIndex < ViewCount);
	return Slots[ViewIndex].FxaaTarget.get();
}

FTexture2D* FViewportsPanel::GetHDRTarget(const int32 ViewIndex) const
{
	assert(ViewIndex >= 0 && ViewIndex < ViewCount);
	return Slots[ViewIndex].HDRTarget.get();
}

// 마우스 위치에서 패널 원점을 빼 로컬 좌표로 바꾼다.
FVector2 FViewportsPanel::GetLocalMousePosition() const
{
	const ImVec2 Mouse = ImGui::GetMousePos();
	return { Mouse.x - ContentOrigin.x, Mouse.y - ContentOrigin.y };
}

// 엔진 입력 처리는 ImGui NewFrame보다 먼저 실행되므로 현재 OS 커서로 버튼 영역을 검사한다.
bool FViewportsPanel::IsHovered() const
{
	if (bStatResetButtonVisible && FStatOverlay::IsAnyEnabled())
	{
		POINT Cursor{};
		if (::GetCursorPos(&Cursor) &&
			Cursor.x >= StatResetButtonMin.x && Cursor.x < StatResetButtonMax.x &&
			Cursor.y >= StatResetButtonMin.y && Cursor.y < StatResetButtonMax.y)
		{
			return false;
		}
	}
	return bHovered;
}

// View Texture와 Splitter·Layout·View 종류 UI를 그리고 변경을 바로 반영한다.
void FViewportsPanel::OnRender()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
	ImGui::Begin("Viewports", nullptr,
		ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoScrollWithMouse |
			ImGuiWindowFlags_NoTitleBar);
	// 창 자체의 여백만 없앤다. 계속 유지하면 안에서 여는 콤보·서브메뉴까지 여백이 0이 된다.
	ImGui::PopStyleVar();

	ContentOrigin = ImGui::GetCursorScreenPos();
	ContentSize = ImGui::GetContentRegionAvail();
	ContentSize.x = std::max(1.0f, ContentSize.x);
	ContentSize.y = std::max(1.0f, ContentSize.y);
	bHovered = ImGui::IsWindowHovered();
	bStatResetButtonVisible = false;

	// 전체 캔버스를 한 번 확보한 뒤 각 렌더 타깃을 창 DrawList에 직접 그린다.
	// Image 항목 네 개를 따로 배치하면 ImGui 레이아웃과 클리핑 상태가 삽입 순서에
	// 영향을 받아, Layout이 올바른 사각형을 줘도 아래쪽 행이 잘릴 수 있다.
	ImGui::Dummy(ContentSize);
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->PushClipRect(ContentOrigin,
		{ ContentOrigin.x + ContentSize.x, ContentOrigin.y + ContentSize.y }, true);
	for (int32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex)
	{
		const FViewSlot& Slot = Slots[ViewIndex];
		if (!IsViewActive(ViewIndex) || !Slot.ColorTarget)
		{
			continue;
		}

		const FRect& Rect = Layout.GetViewRect(ViewIndex);
		const ImVec2 ViewMin{ ContentOrigin.x + Rect.X, ContentOrigin.y + Rect.Y };
		const ImVec2 ViewMax{ ViewMin.x + Rect.Width, ViewMin.y + Rect.Height };
		DrawList->AddImage(Slot.ColorTarget->GetSRV(), ViewMin, ViewMax);
	}
	DrawList->PopClipRect();

	if (Layout.GetLayoutMode() == ELayoutMode::QuadSplit)
	{
		// View Rect 사이에 비워 둔 gutter의 중앙에 Splitter 버튼을 배치한다.
		const FRect& TopLeft = Layout.GetViewRect(0);
		const float SplitX = (TopLeft.X + TopLeft.Width + Layout.GetViewRect(1).X) * 0.5f;
		const float SplitY = (TopLeft.Y + TopLeft.Height + Layout.GetViewRect(2).Y) * 0.5f;

		const float Thickness = EditorUIScale::Px(SplitterThickness);
		const ImVec2 VerticalMin{ ContentOrigin.x + SplitX - Thickness * 0.5f, ContentOrigin.y };
		const ImVec2 VerticalMax{ VerticalMin.x + Thickness, ContentOrigin.y + ContentSize.y };
		const ImVec2 HorizontalMin{ ContentOrigin.x, ContentOrigin.y + SplitY - Thickness * 0.5f };
		const ImVec2 HorizontalMax{ ContentOrigin.x + ContentSize.x, HorizontalMin.y + Thickness };

		ImGui::SetCursorScreenPos(VerticalMin);
		ImGui::InvisibleButton("##MultipleViewportsHorizontalSplitter", { Thickness, ContentSize.y });
		ImGui::SetCursorScreenPos(HorizontalMin);
		ImGui::InvisibleButton("##MultipleViewportsVerticalSplitter", { ContentSize.x, Thickness });

		const bool bVerticalHovered = bHovered && ImGui::IsMouseHoveringRect(VerticalMin, VerticalMax);
		const bool bHorizontalHovered = bHovered && ImGui::IsMouseHoveringRect(HorizontalMin, HorizontalMax);
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			bDraggingVerticalSplitter = bVerticalHovered;
			bDraggingHorizontalSplitter = bHorizontalHovered;
		}
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			bDraggingVerticalSplitter = false;
			bDraggingHorizontalSplitter = false;
		}

		const bool bVerticalHighlighted = bDraggingVerticalSplitter || bVerticalHovered;
		const bool bHorizontalHighlighted = bDraggingHorizontalSplitter || bHorizontalHovered;
		DrawList->AddRectFilled(VerticalMin, VerticalMax,
			bVerticalHighlighted ? SplitterHoverColor : SplitterColor);
		DrawList->AddRectFilled(HorizontalMin, HorizontalMax,
			bHorizontalHighlighted ? SplitterHoverColor : SplitterColor);

		// 비율만 바꾸고 화면 배치는 다음 프레임의 UpdateViewports에서 계산한다.
		if (bDraggingVerticalSplitter && ImGui::GetIO().MouseDelta.x != 0.0f)
		{
			Layout.ApplySplitterDrag(EDragAxis::Horizontal, ImGui::GetIO().MouseDelta.x, GetContentSize());
		}
		if (bDraggingHorizontalSplitter && ImGui::GetIO().MouseDelta.y != 0.0f)
		{
			Layout.ApplySplitterDrag(EDragAxis::Vertical, ImGui::GetIO().MouseDelta.y, GetContentSize());
		}

		if (bVerticalHighlighted && bHorizontalHighlighted)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
		}
		else if (bVerticalHighlighted)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		}
		else if (bHorizontalHighlighted)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
		}
	}
	else
	{
		bDraggingVerticalSplitter = false;
		bDraggingHorizontalSplitter = false;
	}

	// 뷰포트별 반응형 툴바: 현재 줄에 공간이 없으면 다음 줄로 내려 배치한다.
	float ToolbarExtraHeight[ViewCount]{};

	for (int32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex)
	{
		if (!IsViewActive(ViewIndex))
		{
			continue;
		}

		FEditorViewportClient& Client = ViewportClients[ViewIndex];
		const FRect& Rect = Layout.GetViewRect(ViewIndex);

		const float Left = ContentOrigin.x + Rect.X + EditorUIScale::Px(8.0f);
		const float Top = ContentOrigin.y + Rect.Y + EditorUIScale::Px(8.0f);
		const float Width = std::max(1.0f, Rect.Width - EditorUIScale::Px(16.0f));

		ImVec2 Cursor{ Left, Top };

		auto PlaceToolbarItem = [&](float PreferredWidth)
		{
			const float ItemWidth = std::min(PreferredWidth, Width);

			if (Cursor.x > Left && Cursor.x + ItemWidth > Left + Width)
			{
				Cursor.x = Left;
				Cursor.y += ImGui::GetFrameHeightWithSpacing();
			}

			ImGui::SetCursorScreenPos(Cursor);
			Cursor.x += ItemWidth + ImGui::GetStyle().ItemSpacing.x;

			return ItemWidth;
		};

		auto ComboWidth = [](const char* LongestLabel)
		{
			return ImGui::CalcTextSize(LongestLabel).x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f;
		};

		auto ButtonWidth = [](const char* Label)
		{
			return ImGui::CalcTextSize(Label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
		};

		ImGui::PushID(100 + ViewIndex);

		// View 종류
		int SelectedType = static_cast<int>(Client.GetViewportType());
		ImGui::SetNextItemWidth(PlaceToolbarItem(ComboWidth("Ortho (Current)")));
		if (ImGui::Combo("##ViewportType", &SelectedType, ViewportTypeLabels, IM_ARRAYSIZE(ViewportTypeLabels)))
		{
			Client.SetViewportType(static_cast<ELevelViewportType>(SelectedType));
		}

		{
			FEngineShowFlags Flags = Client.GetShowFlags();
			ImGui::SetNextItemWidth(PlaceToolbarItem(ComboWidth("Show Flags")));

			if (ImGui::BeginCombo("##ViewShowFlags", "Show Flags", ImGuiComboFlags_HeightLargest))
			{
				if (ShowFlagUI::DrawFlags(Flags, EShowFlagScope::View, EShowFlagGroup::Viewport))
				{
					Client.SetShowFlags(Flags);
				}
				ImGui::EndCombo();
			}

			// 뷰모드. 고른 값은 렌더 직전에 Renderer로 넘어가 UberLit 변형을 고른다.
			const EViewMode CurrentViewMode = Client.GetViewMode();

			// 가장 긴 뷰모드 이름 기준으로 Combo 폭을 잡는다.
			const char* LongestViewModeName = GetViewModeName(CurrentViewMode);
			for (int32 ModeIndex = 0; ModeIndex < static_cast<int32>(EViewMode::Count); ++ModeIndex)
			{
				const char* Name = GetViewModeName(static_cast<EViewMode>(ModeIndex));
				if (ImGui::CalcTextSize(Name).x > ImGui::CalcTextSize(LongestViewModeName).x)
				{
					LongestViewModeName = Name;
				}
			}

			ImGui::SetNextItemWidth(PlaceToolbarItem(ComboWidth(LongestViewModeName)));
			if (ImGui::BeginCombo("##ViewMode", GetViewModeName(CurrentViewMode)))
			{
				// UE처럼 묶음은 오른쪽으로 펼쳐지는 서브메뉴로, 묶음이 없는 모드는 최상위 항목으로 그린다.
				// MenuItem은 서브메뉴 안에서 눌러도 콤보까지 함께 닫는다.
				for (int32 GroupIndex = 0; GroupIndex < static_cast<int32>(EViewModeGroup::Count); ++GroupIndex)
				{
					const EViewModeGroup Group = static_cast<EViewModeGroup>(GroupIndex);
					const bool bSubMenu = Group != EViewModeGroup::None;
					if (bSubMenu && !ImGui::BeginMenu(ViewModeGroupNames[GroupIndex]))
					{
						continue;
					}

					for (int32 ModeIndex = 0; ModeIndex < static_cast<int32>(EViewMode::Count); ++ModeIndex)
					{
						const EViewMode Candidate = static_cast<EViewMode>(ModeIndex);
						if (GetViewModeGroup(Candidate) != Group)
						{
							continue;
						}
						const char* Name = bSubMenu ? GetViewModeMenuName(Candidate) : GetViewModeName(Candidate);
						if (ImGui::MenuItem(Name, nullptr, Candidate == CurrentViewMode))
						{
							Client.SetViewMode(Candidate);
						}
					}

					if (bSubMenu)
					{
						ImGui::EndMenu();
					}
				}
				ImGui::EndCombo();
			}
		}

		// Single / Quad
		// 화면 배치는 다음 프레임의 UpdateViewports에서 다시 계산한다.
		if (Layout.GetLayoutMode() == ELayoutMode::QuadSplit)
		{
			PlaceToolbarItem(ButtonWidth("Single"));
			if (ImGui::Button("Single"))
			{
				Layout.SetSingleViewIndex(ViewIndex);
				Layout.SetLayoutMode(ELayoutMode::Single);
			}
		}
		else if (ViewIndex == Layout.GetSingleViewIndex())
		{
			PlaceToolbarItem(ButtonWidth("Quad"));
			if (ImGui::Button("Quad"))
			{
				Layout.SetLayoutMode(ELayoutMode::QuadSplit);
			}
		}

		// Reset Stats
		if (ViewIndex == GetEditorViewIndex() &&
			FStatOverlay::IsAnyEnabled() &&
			(FStatOverlay::IsEnabled(EStatFlags::Profile) ||
				FStatRegistry::Find(EditorStats::STAT_PickingTime)))
		{
			PlaceToolbarItem(ButtonWidth("Reset Stats"));
			if (ImGui::Button("Reset Stats"))
			{
				FStatRegistry::Reset();
			}
			StatResetButtonMin = ImGui::GetItemRectMin();
			StatResetButtonMax = ImGui::GetItemRectMax();
			bStatResetButtonVisible = true;
		}

		// 줄바꿈으로 늘어난 툴바 높이만큼 통계 오버레이를 아래로 민다.
		ToolbarExtraHeight[ViewIndex] = Cursor.y - Top;

		ImGui::PopID();
	}

	// 툴바 메뉴나 위에 뜬 창·팝업 위에서 굴린 휠은 카메라로 보내지 않는다. 입력 처리가 먼저 돌므로 다음 프레임에 쓴다.
	bWheelOverView = bHovered && !ImGui::IsAnyItemHovered();

	// 마지막으로 선택된 뷰포트만 오버레이
	for (int32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex)
	{
		if (ViewIndex != GetEditorViewIndex() || !IsViewActive(ViewIndex))
		{
			continue;
		}
		const FRect& Rect = Layout.GetViewRect(ViewIndex);
		DrawStatOverlay(DrawList, { ContentOrigin.x + Rect.X, ContentOrigin.y + Rect.Y + ToolbarExtraHeight[ViewIndex] });
	}

	ImGui::End();
}

// 항목별 줄을 모아 한 번에 배경과 텍스트를 그린다.
void FViewportsPanel::DrawStatOverlay(ImDrawList* DrawList, const ImVec2& ViewMin) const
{
	if (!DrawList || !FStatOverlay::IsAnyEnabled())
	{
		return;
	}

	constexpr float BytesPerMegabyte = 1024.0f * 1024.0f;

	// 제목은 UE처럼 노란색, 값은 흰색으로 구분한다.
	struct FStatLine
	{
		FString Text;
		ImU32 Color;
	};
	TArray<FStatLine> Lines;

	if (FStatOverlay::IsEnabled(EStatFlags::FPS))
	{
		Lines.Add({ "FPS", TitleColor });
		Lines.Add({ std::format("  {:.1f} fps", FStatOverlay::GetFPS()), ValueColor });
		Lines.Add({ std::format("  {:.2f} ms", FStatOverlay::GetFrameTimeMs()), ValueColor });
	}

	if (FStatOverlay::IsEnabled(EStatFlags::Memory))
	{
		Lines.Add({ "Memory", TitleColor });
		Lines.Add({ std::format("  Object  {:.2f} MB ({} allocs)",
						static_cast<double>(FStatOverlay::GetObjectAllocationBytes()) / BytesPerMegabyte,
						FStatOverlay::GetObjectAllocationCount()),
			ValueColor });
		Lines.Add({ std::format("  Process {:.2f} MB",
						static_cast<double>(FStatOverlay::GetProcessWorkingSetBytes()) / BytesPerMegabyte),
			ValueColor });
	}

	// 데칼 패스는 View마다 돌므로 값은 이번 프레임 전체 View의 합이다.
	if (FStatOverlay::IsEnabled(EStatFlags::Decal))
	{
		Lines.Add({ "Decal", TitleColor });
		Lines.Add({ std::format("  CPU Pass: {:.3f} ms  Receivers: {:.3f} ms  Triangles: {:.3f} ms",
						FStatRegistry::GetCurrentFrameMs(DecalStats::STAT_PassCPU),
						FStatRegistry::GetCurrentFrameMs(DecalStats::STAT_ReceiverCPU),
						FStatRegistry::GetCurrentFrameMs(DecalStats::STAT_TriangleCPU)),
			ValueColor });
		Lines.Add({ std::format("  Decals: {} / {}  Receivers: {}",
						FStatRegistry::GetCounter(DecalStats::STAT_RenderedDecals),
						FStatRegistry::GetCounter(DecalStats::STAT_SceneDecals),
						FStatRegistry::GetCounter(DecalStats::STAT_Receivers)),
			ValueColor });
		Lines.Add({ std::format("  Draw Calls: {}  Triangles: {}",
						FStatRegistry::GetCounter(DecalStats::STAT_DrawCalls),
						FStatRegistry::GetCounter(DecalStats::STAT_Triangles)),
			ValueColor });
	}

	// 현재 프레임 전체 World의 그림자 통계를 기존 줄 목록에 추가한다.
	if (FStatOverlay::IsEnabled(EStatFlags::Shadow))
	{
		Lines.Add({ "Shadow / Total", TitleColor });
		// 전체 CPU 시간에는 그룹별 작업 외의 Atlas 준비와 정보 업로드 비용도 포함된다.
		Lines.Add({ std::format("  CPU Setup: {:.3f} ms  Renderer: {:.3f} ms",
						FStatRegistry::GetCurrentFrameMs(ShadowStats::STAT_SetupCPU),
						FStatRegistry::GetCurrentFrameMs(ShadowStats::STAT_RenderCPU)),
			ValueColor });
		Lines.Add({ std::format("  Prepared lights D/P/S: {}/{}/{}  Bound SRVs: {}",
						FStatRegistry::GetCounter(ShadowStats::STAT_DirectionalLights),
						FStatRegistry::GetCounter(ShadowStats::STAT_PointLights),
						FStatRegistry::GetCounter(ShadowStats::STAT_SpotLights),
						FStatRegistry::GetCounter(ShadowStats::STAT_BoundTextures)),
			ValueColor });
		Lines.Add({ std::format("  Local lights outside views: {}",
						FStatRegistry::GetCounter(ShadowStats::STAT_LocalLightsOutsideViews)),
			ValueColor });
		Lines.Add({ std::format("  Local lights without receivers: {}",
						FStatRegistry::GetCounter(ShadowStats::STAT_LocalLightsWithoutReceivers)),
			ValueColor });
		Lines.Add({ std::format("  Empty caster depth skips: {}",
						FStatRegistry::GetCounter(ShadowStats::STAT_EmptyCasterDepthSkips)),
			ValueColor });
		Lines.Add({ std::format("  Small caster submission skips: {}",
						FStatRegistry::GetCounter(ShadowStats::STAT_SmallCasterSkips)),
			ValueColor });
		Lines.Add({ std::format("  Caster sphere skips: {}",
						FStatRegistry::GetCounter(ShadowStats::STAT_CasterSphereSkips)),
			ValueColor });
		Lines.Add({ std::format("  Receiver projection skips: {}",
						FStatRegistry::GetCounter(ShadowStats::STAT_ReceiverProjectionSkips)),
			ValueColor });
		Lines.Add({ std::format("  Maps: {}  Draws: {}  Updates: {}",
						FStatRegistry::GetCounter(ShadowStats::STAT_Maps),
						FStatRegistry::GetCounter(ShadowStats::STAT_DrawCalls),
						FStatRegistry::GetCounter(ShadowStats::STAT_MapUpdates)),
			ValueColor });
		// CSM OFF와 직교 View의 단일 맵도 실제 Directional 투영에 포함한다.
		Lines.Add({ std::format("  Directional views: {}  Projections: {}",
						FStatRegistry::GetCounter(ShadowStats::STAT_DirectionalViews),
						FStatRegistry::GetCounter(ShadowStats::STAT_DirectionalProjections)),
			ValueColor });

		// Directional은 View별로 구분하고, Point와 Spot은 World 공유 그룹으로 표시한다.
		for (uint32 Group = 0; Group < ShadowStats::GroupCount; ++Group)
		{
			if (Group == ShadowStats::Local)
			{
				Lines.Add({ "Local / World shared", TitleColor });
			}
			else
			{
				const TStatId ViewStat = Group == ShadowStats::Single
					? ShadowStats::STAT_SingleViews
					: ShadowStats::STAT_CSMViews;
				Lines.Add({ std::format("{} / Views: {}", ShadowStats::GroupNames[Group],
								FStatRegistry::GetCounter(ViewStat)),
					TitleColor });
			}

			Lines.Add({ std::format("  CPU Setup: {:.3f} ms  Depth submit: {:.3f} ms",
							FStatRegistry::GetCurrentFrameMs(ShadowStats::SetupCPUStats[Group]),
							FStatRegistry::GetCurrentFrameMs(ShadowStats::DepthCPUStats[Group])),
				ValueColor });
			// 활성 타일 내용량은 아래의 전체 Atlas 페이지 할당량과 별개다.
			const double TileMiB = static_cast<double>(
									   FStatRegistry::GetMemory(ShadowStats::TileMemoryStats[Group])) /
				BytesPerMegabyte;
			Lines.Add({ std::format("  Maps: {}  Draws: {}  Updates: {}  Tiles: {:.2f} MiB",
							FStatRegistry::GetCounter(ShadowStats::MapStats[Group]),
							FStatRegistry::GetCounter(ShadowStats::DrawStats[Group]),
							FStatRegistry::GetCounter(ShadowStats::UpdateStats[Group]),
							TileMiB),
				ValueColor });
		}

		Lines.Add({ "Shadow / Cache", TitleColor });
		Lines.Add({ std::format("  r.shadow.CacheWholeSceneShadows: {}  Generation: {}",
						FShadowSettings::IsWholeSceneCacheEnabled() ? "ON" : "OFF",
						FShadowSettings::GetCacheGeneration()),
			ValueColor });

		const uint64 CacheHits = FStatRegistry::GetCounter(ShadowStats::STAT_CacheHits);
		const uint64 MapUpdates = FStatRegistry::GetCounter(ShadowStats::STAT_MapUpdates);
		const uint64 CacheTotal = CacheHits + MapUpdates;
		const double HitRate = CacheTotal > 0 ? 100.0 * static_cast<double>(CacheHits) / static_cast<double>(CacheTotal) : 0.0;
		Lines.Add({ std::format("  Cached: {}  Updated: {}  Hit rate: {:.1f}%",
						CacheHits, MapUpdates, HitRate),
			ValueColor });
		// Point는 라이트 수가 아니라 캐시를 재사용한 면 수로 집계한다.
		Lines.Add({ std::format("  Cached D/P/S: {} / {} / {}",
						FStatRegistry::GetCounter(ShadowStats::STAT_CacheHitsDirectional),
						FStatRegistry::GetCounter(ShadowStats::STAT_CacheHitsPoint),
						FStatRegistry::GetCounter(ShadowStats::STAT_CacheHitsSpot)),
			ValueColor });
		Lines.Add({ std::format("  Miss Off / New / Gen / VP / Caster: {} / {} / {} / {} / {}",
						FStatRegistry::GetCounter(ShadowStats::STAT_CacheMissDisabled),
						FStatRegistry::GetCounter(ShadowStats::STAT_CacheMissNewTile),
						FStatRegistry::GetCounter(ShadowStats::STAT_CacheMissGeneration),
						FStatRegistry::GetCounter(ShadowStats::STAT_CacheMissViewProjection),
						FStatRegistry::GetCounter(ShadowStats::STAT_CacheMissCasters)),
			ValueColor });
		// 실제 확보한 페이지 용량 대비 POT 예약 블록의 점유율을 표시한다.
		const uint64 AtlasDepthBytes = FStatRegistry::GetMemory(ShadowStats::STAT_DepthMemory);
		const uint64 ReservedBytes = FStatRegistry::GetMemory(ShadowStats::STAT_AtlasReservedMemory);
		const double Occupancy = AtlasDepthBytes > 0
			? 100.0 * static_cast<double>(ReservedBytes) / static_cast<double>(AtlasDepthBytes)
			: 0.0;
		Lines.Add({ std::format("  Tile clears: {}  Atlas pages: {}  Allocations: {}",
						FStatRegistry::GetCounter(ShadowStats::STAT_TileClears),
						FStatRegistry::GetCounter(ShadowStats::STAT_AtlasPages),
						FStatRegistry::GetCounter(ShadowStats::STAT_AtlasAllocations)),
			ValueColor });
		Lines.Add({ std::format("  Reserved: {:.2f} MiB  Block occupancy: {:.1f}%",
						static_cast<double>(ReservedBytes) / BytesPerMegabyte, Occupancy),
			ValueColor });

		const uint64 DepthBytes = FStatRegistry::GetMemory(ShadowStats::STAT_DepthMemory);
		const uint64 ParameterBytes = FStatRegistry::GetMemory(ShadowStats::STAT_ParameterMemory);
		// 빈 타일과 재사용 대기 페이지를 포함해 실제 확보한 자원 크기를 표시한다.
		Lines.Add({ "Shadow / Allocated resources", TitleColor });
		Lines.Add({ std::format("  Depth: {:.2f} MiB  Info: {} B  Total: {:.2f} MiB",
						static_cast<double>(DepthBytes) / BytesPerMegabyte, ParameterBytes,
						static_cast<double>(DepthBytes + ParameterBytes) / BytesPerMegabyte),
			ValueColor });
	}

	if (FStatOverlay::IsEnabled(EStatFlags::Skin))
	{
		Lines.Add({ "Skinning", TitleColor });

		Lines.Add({ std::format("  Skinning CPU: {:.3f} ms", FStatRegistry::GetCurrentFrameMs(SkinningStats::STAT_CPU)), ValueColor });
		Lines.Add({ std::format("  Skinned Vertices: {}", FStatRegistry::GetCounter(SkinningStats::STAT_SkinnedVertices)), ValueColor });
		Lines.Add({ std::format("  Skin Upload Bytes: {}", FStatRegistry::GetCounter(SkinningStats::STAT_SkinUploadBytes)), ValueColor });
	}

	struct FProfileStatLine
	{
		FString Text;
		ImU32 Color;
		FString Values[3];
	};
	TArray<FProfileStatLine> ProfileLines;
	if (FStatOverlay::IsEnabled(EStatFlags::Profile))
	{
		ProfileLines.Add({ "CPU Profile (ms)", TitleColor, { "Last", "Avg", "Max" } });

		for (const auto& [Name, Data] : FStatRegistry::GetAll())
		{
			if (TStatId{ Name } == EditorStats::STAT_PickingTime)
			{
				continue;
			}

			FProfileStatLine Line{ "  " + FString(Name), ValueColor };
			Line.Values[0] = std::format("{:.2f}", Data.GetLastMs());
			Line.Values[1] = std::format("{:.2f}", Data.GetRecentAverageMs());
			Line.Values[2] = std::format("{:.2f}", Data.GetMaxMs());
			ProfileLines.Add(Line);
		}
	}

	if (const FCycleStatData* PickingData = FStatRegistry::Find(EditorStats::STAT_PickingTime))
	{
		ProfileLines.Add({ "Picking", TitleColor });
		FProfileStatLine Line{ "  Time (ms)", ValueColor };
		Line.Values[0] = std::format("Last: {:.2f}", PickingData->GetLastMs());
		Line.Values[1] = std::format("Attempts: {}", PickingData->CallCount);
		Line.Values[2] = std::format("Acc.: {:.2f}", PickingData->GetTotalMs());
		ProfileLines.Add(Line);
	}

	if (Lines.Num() == 0 && ProfileLines.Num() == 0)
	{
		return;
	}

	// 제어 위젯 한 줄 아래에서 시작해 Combo와 겹치지 않게 한다.
	const float LineHeight = ImGui::GetTextLineHeight();
	const float Margin = EditorUIScale::Px(StatOverlayMargin);
	const float Padding = EditorUIScale::Px(StatOverlayPadding);
	const ImVec2 Origin{
		ViewMin.x + Margin,
		ViewMin.y + Margin + ImGui::GetFrameHeight() + Margin
	};

	// 최대 너비 계산
	float MaxWidth = 0.0f;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		MaxWidth = std::max(MaxWidth, ImGui::CalcTextSize(Lines[Index].Text.c_str()).x);
	}

	const float MinValueWidth = ImGui::CalcTextSize("00000.00").x;
	float NameWidth = 0.0f;
	float ValueWidths[3] = { MinValueWidth, MinValueWidth, MinValueWidth };
	bool bHasColumns = false;

	for (const FProfileStatLine& Line : ProfileLines)
	{
		const float TextWidth = ImGui::CalcTextSize(Line.Text.c_str()).x;
		bHasColumns = true;
		NameWidth = std::max(NameWidth, TextWidth);
		for (int32 Column = 0; Column < 3; ++Column)
		{
			ValueWidths[Column] = std::max(ValueWidths[Column], ImGui::CalcTextSize(Line.Values[Column].c_str()).x);
		}
	}

	float ColumnRight[3]{};
	if (bHasColumns)
	{
		float Width = NameWidth;
		for (int32 Column = 0; Column < 3; ++Column)
		{
			Width += ImGui::GetFontSize() + ValueWidths[Column];
			ColumnRight[Column] = Origin.x + Width;
		}
		MaxWidth = std::max(MaxWidth, Width);
	}

	const ImVec2 BackgroundMin{ Origin.x - Padding, Origin.y - Padding };
	const ImVec2 BackgroundMax{
		Origin.x + MaxWidth + Padding,
		Origin.y + LineHeight * static_cast<float>(Lines.Num() + ProfileLines.Num()) + Padding
	};
	DrawList->AddRectFilled(BackgroundMin, BackgroundMax, StatOverlayBackgroundColor, EditorUIScale::Px(4.0f));

	// Lines 그리기
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		DrawList->AddText(
			{ Origin.x, Origin.y + LineHeight * static_cast<float>(Index) },
			Lines[Index].Color,
			Lines[Index].Text.c_str());
	}

	// ProfileLines 그리기
	for (int32 Index = 0; Index < ProfileLines.Num(); ++Index)
	{
		const FProfileStatLine& Line = ProfileLines[Index];
		const float Y = Origin.y + LineHeight * static_cast<float>(Lines.Num() + Index);
		DrawList->AddText({ Origin.x, Y }, Line.Color, Line.Text.c_str());

		for (int32 Column = 0; Column < 3; ++Column)
		{
			const FString& Text = Line.Values[Column];
			const float TextWidth = ImGui::CalcTextSize(Text.c_str()).x;
			DrawList->AddText({ ColumnRight[Column] - TextWidth, Y }, Line.Color, Text.c_str());
		}
	}
}

// View 크기에 맞춰 Color·Depth Texture와 렌더 정보를 재생성한다.
void FViewportsPanel::ResizeSlot(FViewSlot& Slot, const uint32 Width, const uint32 Height)
{
	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = Width;
	Desc.Height = Height;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	Slot.ColorTarget = RenderCommand::CreateTexture2D(Desc);
	// CopyResource로 되돌려 쓰므로 ColorTarget과 같은 Desc로 만든다
	Slot.FxaaTarget = RenderCommand::CreateTexture2D(Desc);

	// Fog 패스가 깊이를 셰이더에서 읽으므로 DSV와 SRV를 둘 다 만들 수 있는 TYPELESS로 만든다
	Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	Slot.DepthTarget = RenderCommand::CreateTexture2D(Desc);

	// HDR Target
	Desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	Slot.HDRTarget = RenderCommand::CreateTexture2D(Desc);

	Slot.Width = Width;
	Slot.Height = Height;
	Slot.RenderingInfo.ColorRenderTargets.Reset();
	Slot.RenderingInfo.ViewportSetting.Width = Width;
	Slot.RenderingInfo.ViewportSetting.Height = Height;
	FRenderingDesc ColorDesc{};
	ColorDesc.Texture = Slot.ColorTarget.get();
	Slot.RenderingInfo.ColorRenderTargets.Add(ColorDesc);
	Slot.RenderingInfo.DepthStencil.Texture = Slot.DepthTarget.get();
}
