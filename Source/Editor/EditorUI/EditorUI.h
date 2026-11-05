#pragma once

#include "Editor/EditorUI/EditorPanel.h"

#include "Editor/LevelEditor/LevelEditorToolbar.h"
#include "Asset/FbxImporter/FbxInfo.h"

#include <functional>

struct ImGuiWindowClass;

class FEditorUI
{
public:
	// 바깥 도킹 공간에 붙는 루트 창(Level Editor, FBX 뷰어)의 클래스. 레벨 패널과 섞이지 않는다
	static const ImGuiWindowClass& GetRootWindowClass();

	bool Init(bool bInUseDockSpace = true, bool bInPassthruCentralNode = false);
	void Tick(float DeltaTime);
	void OnRender();

	template<typename T>
	T* AddEditorPanel()
	{
		TUniquePtr<T> newPanel = MakeUnique<T>();
		T* Ret = newPanel.get();
		Ret->Init();
		Panels.Add(std::move(newPanel));

		return Ret;
	}

	FLevelEditorToolbar* GetToolbar() const { return Toolbar.get(); }

	void SetNewSceneCallback(std::function<void()> InCallback) { OnNewScene = InCallback; }
	void SetOpenSceneCallback(std::function<void()> InCallback) { OnOpenScene = InCallback; }
	void SetSaveSceneCallback(std::function<void()> InCallback) { OnSaveScene = InCallback; }
	void SetSaveSceneAsCallback(std::function<void()> InCallback) { OnSaveSceneAs = InCallback; }

private:
	bool bUseDockSpace = true;
	bool bPassthruCentralNode = false;

	void DrawMainMenuBar();
	void DrawFbxImportDialog();
	void BrowseFbxSource();
	void BrowseFbxOutputDirectory();
	void UpdateFbxOutputDirectory();
	bool bRequestFbxImportDialog = false;
	bool bFbxImportDialogOpen = false;
	char FbxSourcePath[2048]{};
	char FbxOutputDirectory[2048]{};
	bool bFbxOutputDirectoryEdited = false;
	FFbxImportOptions FbxOptions;
	FFbxImportResult FbxResult;

	TArray<TUniquePtr<IEditorPanel>> Panels;
	TUniquePtr<FLevelEditorToolbar> Toolbar;


	std::function<void()> OnNewScene;
	std::function<void()> OnOpenScene;
	std::function<void()> OnSaveScene;
	std::function<void()> OnSaveSceneAs;
};
