#pragma once

#include "Editor/EditorUI/EditorPanel.h"

#include <functional>

class UWorld;
class ULevel;
class AActor;
class UActorComponent;
class UPrimitiveComponent;
class USceneComponent;

using SelectionCallback = std::function<void(USceneComponent*)>;
using DeleteActorCallback = std::function<void(AActor*)>;
using ActorDoubleClickCallback = std::function<void(AActor*)>;

class FOutlinerPanel : public IEditorPanel
{
public:
	FOutlinerPanel() = default;
	~FOutlinerPanel() = default;

	void SetSelectionCallback(SelectionCallback InCallback) { Callback = InCallback; }
	void SetDeleteActorCallback(DeleteActorCallback InCallback) { DeleteCallback = InCallback; }
	void SetActorDoubleClickCallback(ActorDoubleClickCallback InCallback) { DoubleClickCallback = InCallback; }

public:
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Outliner"; }

	void SetWorld(UWorld* InWorld) { World = InWorld; };

	struct FOutlinerRow
	{
		AActor* Actor;
		int32 Depth;
		bool bHasChildren;
	};

	void DrawActors(ULevel* Level);
	void DrawActorNode(const FOutlinerRow& Row);
	void SelectActor(AActor* Actor);

	AActor* GetSelectedActor() const;


private:
	void AddActorRows(AActor* Actor, int32 Depth);
	void BeginActorRename(AActor* Actor);
	void DrawActorRenameField(AActor* Actor);

	UWorld* World = nullptr;
	UObject* SelectedObject = nullptr;

	SelectionCallback Callback;
	DeleteActorCallback DeleteCallback;
	ActorDoubleClickCallback DoubleClickCallback;
	AActor* PendingDeleteActor = nullptr;

	TArray<FOutlinerRow> Rows;
	TSet<AActor*> CollapsedActors;
	AActor* PendingAttachActor = nullptr;
	AActor* PendingAttachParent = nullptr;
	bool bPendingAttach = false;

	AActor* RenamingActor = nullptr;
	char ActorRenameBuffer[256] = {};
	bool bFocusActorRename = false;
};