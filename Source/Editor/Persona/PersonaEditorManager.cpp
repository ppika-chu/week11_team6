#include "EnginePCH.h"

#include "Editor/Persona/PersonaEditorManager.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Editor/AnimationEditor/AnimationEditor.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Render/SkeletalMesh.h"

bool FPersonaEditorManager::Init()
{
	return true;
}

void FPersonaEditorManager::Tick(float DeltaTime)
{
	// 지난 프레임에 닫힌 창 정리. ImGui가 그 창의 SRV를 다 그린 뒤라 안전하다
	for (int32 i = Editors.Num() - 1; i >= 0; --i)
	{
		if (!Editors[i]->IsOpen())
		{
			Editors.RemoveAt(i);
		}
	}

	// 지우는 시점이 위와 같아서 여기서 교체한다
	ReopenRetiredEditors();

	// FEditorUI는 Window 메뉴로 꺼진 패널도 Tick을 부르므로 여기서 막는다
	if (!IsOpen())
	{
		return;
	}

	for (auto& Editor : Editors)
	{
		Editor->Tick(DeltaTime);
	}
}

void FPersonaEditorManager::OnRender()
{
	for (auto& Editor : Editors)
	{
		Editor->OnRender();
	}
}

void FPersonaEditorManager::OpenEditor(USkeletalMesh* Mesh)
{
	if (!Mesh)
	{
		return;
	}

	// Window 메뉴로 꺼 두었으면 에디터들이 Tick / 렌더되지 않으므로 다시 켠다
	SetOpen(true);

	if (FocusOpenEditor(Mesh))
	{
		return;
	}

	Editors.Add(MakeUnique<FSkeletalMeshEditor>(Mesh, NextEditorId++));
}

void FPersonaEditorManager::OpenEditor(UAnimSequence* Sequence)
{
	if (!Sequence)
	{
		return;
	}

	SetOpen(true);

	if (FocusOpenEditor(Sequence))
	{
		return;
	}

	// 클립에는 메시가 없으므로 같은 스켈레톤의 메시로 보여 준다 (UE USkeleton::FindCompatibleMesh)
	USkeletalMesh* PreviewMesh = Sequence->GetSkeleton() ? Sequence->GetSkeleton()->FindCompatibleMesh() : nullptr;
	if (!PreviewMesh)
	{
		HTR_LOG(Warning, "[Animation] No skeletal mesh uses the skeleton of {}", Sequence->GetPath());
		return;
	}

	Editors.Add(MakeUnique<FAnimationEditor>(Sequence, PreviewMesh, NextEditorId++));
}

bool FPersonaEditorManager::FocusOpenEditor(const URenderAsset* Asset)
{
	for (auto& Editor : Editors)
	{
		if (Editor->GetEditedAsset() == Asset)
		{
			Editor->Focus();
			return true;
		}
	}
	return false;
}

void FPersonaEditorManager::ReopenRetiredEditors()
{
	for (auto& Editor : Editors)
	{
		if (!Editor->GetEditedAsset()->IsRetired() && !Editor->GetMesh()->IsRetired())
		{
			continue;
		}

		if (TUniquePtr<FPersonaAssetEditor> NewEditor = Editor->CreateReplacementEditor(NextEditorId))
		{
			++NextEditorId;
			Editor = std::move(NewEditor);
		}
	}
}

bool FPersonaEditorManager::HasFocusedEditor() const
{
	// 패널이 꺼지면 OnRender가 안 불려 에디터의 포커스 값이 갱신되지 않으므로 여기서 막는다
	if (!IsOpen())
	{
		return false;
	}

	for (const auto& Editor : Editors)
	{
		if (Editor->IsFocused())
		{
			return true;
		}
	}
	return false;
}

void FPersonaEditorManager::GetPreviewsToRender(TArray<const FPersonaAssetEditor*>& OutEditors) const
{
	OutEditors.Reset();

	// 패널이 꺼지면 OnRender가 안 불려 에디터의 가시성이 갱신되지 않으므로 여기서 막는다
	if (!IsOpen())
	{
		return;
	}

	for (const auto& Editor : Editors)
	{
		if (Editor->ShouldRenderPreview())
		{
			OutEditors.Add(Editor.get());
		}
	}
}
