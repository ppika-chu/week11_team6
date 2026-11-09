#include "EnginePCH.h"

#include "Editor/AnimationEditor/AnimationEditor.h"
#include "Editor/EditorUI/EditorUI.h"
#include "Editor/EditorUI/EditorUIScale.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Asset/AssetManager.h"
#include "Editor/Persona/DebugSkelMeshComponent.h"
#include "Render/SkeletalMesh.h"

#include <imgui.h>
#include <format>

FAnimationEditor::FAnimationEditor(UAnimSequence* InSequence, USkeletalMesh* InPreviewMesh, int32 InEditorId)
	: FPersonaAssetEditor(InPreviewMesh, InEditorId)
	, Sequence(InSequence)
	, WindowTitle(std::format("{} - Animation Editor###AnimationEditor_{}", GetDisplayName(InSequence->GetPath()), InSequence->GetPath()))
{
	// 시각 진행과 포즈 계산은 프리뷰 월드의 TickComponent가 한다
	PreviewScene->GetPreviewComponent()->PlayAnimation(Sequence, true);
}

URenderAsset* FAnimationEditor::GetEditedAsset() const
{
	return Sequence;
}

TUniquePtr<FPersonaAssetEditor> FAnimationEditor::CreateReplacementEditor(int32 NewEditorId) const
{
	// 키로 찾으면 retired는 건너뛰므로 새 객체가 나온다. 없으면 옛 객체를 계속 보여 준다
	UAnimSequence* NewSequence = UAssetManager::GetAssetByPath<UAnimSequence>(Sequence->GetPath());
	USkeletalMesh* NewMesh = NewSequence && NewSequence->GetSkeleton() ? NewSequence->GetSkeleton()->FindCompatibleMesh() : nullptr;
	if (!NewMesh)
	{
		return nullptr;
	}
	return MakeUnique<FAnimationEditor>(NewSequence, NewMesh, NewEditorId);
}

void FAnimationEditor::OnRender()
{
	if (bFocusRequested)
	{
		ImGui::SetNextWindowFocus();
		bFocusRequested = false;
	}

	ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(800.0f), EditorUIScale::Px(600.0f)), ImGuiCond_FirstUseEver);
	// 스켈레탈 에디터처럼 루트 창이라 Level Editor나 다른 뷰어와 붙을 수 있다
	ImGui::SetNextWindowClass(&FEditorUI::GetRootWindowClass());
	// 휠은 줌에 쓰므로 스크롤을 끈다
	bIsVisible = ImGui::Begin(WindowTitle.c_str(), &bIsOpen,
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	bIsFocused = bIsVisible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (bIsVisible)
	{
		DrawViewport();
	}
	ImGui::End();
}
