#include "EnginePCH.h"
#include "Editor/Outliner/OutlinerPanel.h"

#include "Engine/Level.h"
#include "Engine/World.h"

#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Editor/HitoriEd/EditorDragDrop.h"
#include "UObject/UObjectGlobals.h"

bool FOutlinerPanel::Init()
{
	return true;
}

void FOutlinerPanel::Tick(float DeltaTime)
{
}

void FOutlinerPanel::OnRender()
{
	// World 참조 확인
	if (!World)
	{
		return;
	}

	// 현재 Level 가져오기
	ULevel* Level = World->GetCurrentLevel();
	if (!Level)
	{
		return;
	}

	// Level의 Actor 목록 Tree 출력
	ImGui::Begin("Outliner");

	DrawActors(Level);

	ImGui::End();

	if (bPendingAttach)
	{
		if (PendingAttachActor && PendingAttachActor != PendingAttachParent)
		{
			if (!PendingAttachParent)
			{
				PendingAttachActor->DetachFromActor(EAttachmentRule::KeepWorld);
			}
			else if (!PendingAttachActor->AttachToActor(PendingAttachParent, EAttachmentRule::KeepWorld))
			{
				HTR_LOG(Warning, "Cannot attach {} to {}", PendingAttachActor->GetName(), PendingAttachParent->GetName());
			}
		}

		PendingAttachActor = nullptr;
		PendingAttachParent = nullptr;
		bPendingAttach = false;
	}

	// Actors 순회가 완전히 끝난 뒤 삭제
	if (PendingDeleteActor)
	{
		CollapsedActors.Remove(PendingDeleteActor);
		if (RenamingActor == PendingDeleteActor)
		{
			RenamingActor = nullptr;
		}
		if (DeleteCallback)
		{
			DeleteCallback(PendingDeleteActor);
		}

		PendingDeleteActor = nullptr;
	}
}

// Level이 가지고 있는 Actor 목록 순회하며 Draw TreeNode
void FOutlinerPanel::DrawActors(ULevel* Level)
{
	if (!Level)
	{
		return;
	}

	const TArray<AActor*>& Actors = Level->GetActors();

	Rows.Reset();
	for (AActor* Actor : Actors)
	{
		if (!Actor)
		{
			continue;
		}

		AActor* ParentActor = Actor->GetAttachParentActor();
		if (!ParentActor || ParentActor->GetLevel() != Level)
		{
			AddActorRows(Actor, 0);
		}
	}

	ImGuiListClipper Clipper;
	Clipper.Begin(Rows.Num(), ImGui::GetTextLineHeightWithSpacing());

	while (Clipper.Step())
	{
		for (int32 Index = Clipper.DisplayStart; Index < Clipper.DisplayEnd; ++Index)
		{
			DrawActorNode(Rows[Index]);
		}
	}

	const ImVec2 Remaining = ImGui::GetContentRegionAvail();
	ImGui::Dummy(ImVec2(std::max(Remaining.x, 1.0f), std::max(Remaining.y, ImGui::GetTextLineHeight())));
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Actor))
		{
			PendingAttachActor = *static_cast<AActor**>(Payload->Data);
			PendingAttachParent = nullptr;
			bPendingAttach = true;
		}
		ImGui::EndDragDropTarget();
	}
}

void FOutlinerPanel::AddActorRows(AActor* Actor, int32 Depth)
{
	TArray<AActor*> Children;
	Actor->GetAttachedActors(Children);
	Rows.Add({ Actor, Depth, !Children.IsEmpty() });

	if (CollapsedActors.Contains(Actor))
	{
		return;
	}

	for (AActor* Child : Children)
	{
		AddActorRows(Child, Depth + 1);
	}
}

void FOutlinerPanel::BeginActorRename(AActor* Actor)
{
	RenamingActor = Actor;
	strncpy_s(ActorRenameBuffer, sizeof(ActorRenameBuffer), Actor->GetName().c_str(), _TRUNCATE);
	bFocusActorRename = true;
}

void FOutlinerPanel::DrawActorRenameField(AActor* Actor)
{
	ImGui::SameLine();
	if (bFocusActorRename)
	{
		ImGui::SetKeyboardFocusHere();
		bFocusActorRename = false;
	}

	// 목록 클리퍼가 줄 높이를 일정하게 가정하므로 입력칸도 글자 높이에 맞춘다.
	ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, 0.0f);
	ImGui::SetNextItemWidth(-FLT_MIN);
	const bool bConfirmed = ImGui::InputText("##RenameActor", ActorRenameBuffer, sizeof(ActorRenameBuffer),
		ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
	const bool bDeactivated = ImGui::IsItemDeactivated();
	ImGui::PopStyleVar();

	const bool bCancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
	if (!bCancelled && !bConfirmed && !bDeactivated)
	{
		return;
	}

	const FName NewName(ActorRenameBuffer);
	if (!bCancelled && ActorRenameBuffer[0] != '\0' && NewName != NAME_None && NewName != Actor->GetFName())
	{
		// 같은 Outer의 같은 이름은 NewObject가 재활용하고, 뷰 타깃 복원도 이름으로 찾으므로 겹치면 안 된다.
		if (!StaticFindObjectFast(nullptr, Actor->GetOuter(), NewName))
		{
			Actor->SetName(NewName);
		}
		else
		{
			HTR_LOG(Warning, "An actor with the name '{}' already exists in this level.", ActorRenameBuffer);
		}
	}
	RenamingActor = nullptr;
}

// Actor를 ImGui TreeNode로 출력
void FOutlinerPanel::DrawActorNode(const FOutlinerRow& Row)
{
	AActor* Actor = Row.Actor;
	if (!Actor)
	{
		return;
	}

	const FString& ActorName = Actor->GetName();

	ImGuiTreeNodeFlags Flags =
		ImGuiTreeNodeFlags_NoTreePushOnOpen |
		ImGuiTreeNodeFlags_SpanAvailWidth |
		ImGuiTreeNodeFlags_OpenOnArrow;
	if (!Row.bHasChildren)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf;
	}

	// Actor 선택 시 UI 선택 상태
	if (SelectedObject == Actor)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}

	const float IndentWidth = Row.Depth * ImGui::GetStyle().IndentSpacing;
	if (IndentWidth > 0.0f)
	{
		ImGui::Indent(IndentWidth);
	}

	const bool bCollapsed = CollapsedActors.Contains(Actor);
	if (Row.bHasChildren)
	{
		ImGui::SetNextItemOpen(!bCollapsed, ImGuiCond_Always);
	}

	const bool bIsRenaming = RenamingActor == Actor;
	const bool bOpen = ImGui::TreeNodeEx(Actor, Flags, "%s", bIsRenaming ? "" : ActorName.c_str());
	if (Row.bHasChildren && bOpen == bCollapsed)
	{
		if (bOpen)
		{
			CollapsedActors.Remove(Actor);
		}
		else
		{
			CollapsedActors.Add(Actor);
		}
	}

	if (bIsRenaming)
	{
		DrawActorRenameField(Actor);
		if (IndentWidth > 0.0f)
		{
			ImGui::Unindent(IndentWidth);
		}
		return;
	}

	// UI에서 Actor 클릭
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
	{
		SelectActor(Actor);
	}

	// 화살표를 빠르게 두 번 눌러 접고 펴는 동작은 더블 클릭으로 치지 않는다.
	if (DoubleClickCallback && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		DoubleClickCallback(Actor);
	}

	if (SelectedObject == Actor && !ImGui::GetIO().WantTextInput
		&& ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_F2, false))
	{
		BeginActorRename(Actor);
	}

	if (ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload(EditorDragDrop::Actor, &Actor, sizeof(AActor*));
		ImGui::TextUnformatted(ActorName.c_str());
		ImGui::EndDragDropSource();
	}

	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Actor))
		{
			PendingAttachActor = *static_cast<AActor**>(Payload->Data);
			PendingAttachParent = Actor;
			bPendingAttach = true;
		}
		ImGui::EndDragDropTarget();
	}

	if (ImGui::BeginPopupContextItem())
	{
		if (ImGui::MenuItem("Rename", "F2"))
		{
			BeginActorRename(Actor);
		}

		if (ImGui::MenuItem("Detach", nullptr, false, Actor->GetAttachParentActor() != nullptr))
		{
			PendingAttachActor = Actor;
			PendingAttachParent = nullptr;
			bPendingAttach = true;
		}

		if (ImGui::MenuItem("Delete"))
		{
			PendingDeleteActor = Actor; // 여기서는 바로 삭제 X
		}

		ImGui::EndPopup();
	}

	if (IndentWidth > 0.0f)
	{
		ImGui::Unindent(IndentWidth);
	}
}

// 선택 Object 변경
void FOutlinerPanel::SelectActor(AActor* Actor)
{
	if (!Actor)
	{
		SelectedObject = nullptr;

		if (Callback)
		{
			Callback(nullptr);
		}

		return;
	}

	SelectedObject = Actor;

	HTR_LOG(Info, "{} UUID {} is selected", SelectedObject->GetName(), SelectedObject->GetUUID());

	USceneComponent* Scene = Cast<USceneComponent>(Actor->GetRootComponent());

	if (Callback)
	{
		Callback(Scene);
	}
}


AActor* FOutlinerPanel::GetSelectedActor() const
{
	return Cast<AActor>(SelectedObject);
}