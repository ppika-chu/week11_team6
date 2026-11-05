#include "EnginePCH.h"
#include "LevelEditorToolbar.h"

#include "Asset/AssetManager.h"
#include "Editor/EditorUI/EditorUIScale.h"

namespace
{
void BeginToolbarGroup()
{
	ImDrawList* DL = ImGui::GetWindowDrawList();
	DL->ChannelsSplit(2);
	DL->ChannelsSetCurrent(1);
	ImGui::BeginGroup();
}

void EndToolbarGroup()
{
	ImGui::EndGroup();
	const ImVec2 Min = ImGui::GetItemRectMin();
	const ImVec2 Max = ImGui::GetItemRectMax();

	ImDrawList* DL = ImGui::GetWindowDrawList();
	DL->ChannelsSetCurrent(0);
	const float MarginX = EditorUIScale::Px(4.0f);
	const float MarginY = EditorUIScale::Px(2.0f);
	DL->AddRectFilled({ Min.x - MarginX, Min.y - MarginY }, { Max.x + MarginX, Max.y + MarginY }, IM_COL32(45, 45, 45, 255), EditorUIScale::Px(4.0f));
	DL->ChannelsMerge();
}
}

void FLevelEditorToolbar::Draw()
{
	const float PaddingY = EditorUIScale::Px(4.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { EditorUIScale::Px(8.0f), PaddingY });
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { EditorUIScale::Px(4.0f), EditorUIScale::Px(4.0f) });
	const float Height = ImGui::GetFrameHeight() + PaddingY * 2.0f;
	// Level Editor 창 안에 그려서 창과 함께 움직이고, 다른 루트 탭을 보면 함께 가려진다
	const bool bOpen = ImGui::BeginChild("##LevelEditorToolbar", ImVec2(0.0f, Height),
		ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	if (bOpen)
	{
		DrawFileGroup();
		DrawPlayGroup();
	}
	ImGui::EndChild(); // BeginChild 결과와 관계없이 항상 EndChild
	ImGui::PopStyleVar(2);
}

void FLevelEditorToolbar::DrawFileGroup()
{
}

void FLevelEditorToolbar::DrawPlayGroup()
{
	BeginToolbarGroup();

	// 같은 자리에 Play/Pause/Resume을 모두 넘긴다. IsVisible이 참인 하나만 그려진다.
	// SameLine은 "그려졌을 때만" 붙여야 빈 간격이 생기지 않는다.
	bool bFirst = true;
	auto Button = [&](const char* Id, const FEditorCommand& Command)
	{
		if (!Command.IsVisibleNow())
		{
			return;
		}
		if (!bFirst)
		{
			ImGui::SameLine();
		}
		EditorCommandUI::ToolbarButton(Id, Command);
		bFirst = false;
	};

	for (auto Command : Commands)
	{
		Button(Command.Label, Command);
	}
	if (!bFirst)
	{
		ImGui::SameLine();
	}
	DrawPlayModeMenu();

	EndToolbarGroup();
}

void FLevelEditorToolbar::DrawPlayModeMenu()
{
	if (!GetPlayMode || !SetPlayMode)
	{
		return;
	}

	// 실행 중에는 모드를 바꿔도 현재 세션에 반영되지 않으므로 막는다.
	const bool bCanChange = !CanChangePlayMode || CanChangePlayMode();
	ImGui::BeginDisabled(!bCanChange);
	if (ImGui::ArrowButton("##PlayModeMenu", ImGuiDir_Down))
	{
		ImGui::OpenPopup("PlayModePopup");
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		ImGui::SetTooltip("Play Mode: %s",
			GetPlayMode() == EPlayModeType::InViewport ? "Selected Viewport" : "New Editor Window (PIE)");
	}

	if (ImGui::BeginPopup("PlayModePopup"))
	{
		const EPlayModeType Current = GetPlayMode();
		if (ImGui::MenuItem("Selected Viewport", nullptr, Current == EPlayModeType::InViewport))
		{
			SetPlayMode(EPlayModeType::InViewport);
		}
		if (ImGui::MenuItem("New Editor Window (PIE)", nullptr, Current == EPlayModeType::InEditorFloating))
		{
			SetPlayMode(EPlayModeType::InEditorFloating);
		}
		ImGui::EndPopup();
	}
}
