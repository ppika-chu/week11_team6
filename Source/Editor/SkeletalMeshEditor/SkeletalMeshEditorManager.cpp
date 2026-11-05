#include "EnginePCH.h"

#include "Editor/SkeletalMeshEditor/SkeletalMeshEditorManager.h"

#include "Asset/AssetManager.h"
#include "Render/SkeletalMesh.h"

bool FSkeletalMeshEditorManager::Init()
{
	return true;
}

void FSkeletalMeshEditorManager::Tick(float DeltaTime)
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

void FSkeletalMeshEditorManager::OnRender()
{
	for (auto& Editor : Editors)
	{
		Editor->OnRender();
	}
}

void FSkeletalMeshEditorManager::OpenEditor(USkeletalMesh* Mesh)
{
	if (!Mesh)
	{
		return;
	}

	// Window 메뉴로 꺼 두었으면 에디터들이 Tick / 렌더되지 않으므로 다시 켠다
	SetOpen(true);

	for (auto& Editor : Editors)
	{
		if (Editor->GetMesh() == Mesh)
		{
			Editor->Focus();
			return;
		}
	}

	Editors.Add(MakeUnique<FSkeletalMeshEditor>(Mesh, NextEditorId++));
}

void FSkeletalMeshEditorManager::ReopenRetiredEditors()
{
	for (auto& Editor : Editors)
	{
		USkeletalMesh* OldMesh = Editor->GetMesh();
		if (!OldMesh->IsRetired())
		{
			continue;
		}

		// 키로 찾으면 retired는 건너뛰므로 새 객체가 나온다. 없으면(지워졌거나 다른 타입) 옛 객체를 계속 보여 준다
		USkeletalMesh* NewMesh = UAssetManager::GetAssetByPath<USkeletalMesh>(OldMesh->GetPath());
		if (!NewMesh)
		{
			continue;
		}

		// 포즈는 새 Bind Pose로 시작하고, 선택한 본은 이름이 남아 있으면 유지한다
		const FString SelectedBoneName = Editor->GetSelectedBoneName();
		Editor = MakeUnique<FSkeletalMeshEditor>(NewMesh, NextEditorId++);
		Editor->SelectBoneByName(SelectedBoneName);
	}
}

bool FSkeletalMeshEditorManager::HasFocusedEditor() const
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

void FSkeletalMeshEditorManager::GetPreviewsToRender(TArray<const FSkeletalMeshEditor*>& OutEditors) const
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
