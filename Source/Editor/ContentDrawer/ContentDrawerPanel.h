#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

namespace fs = std::filesystem;

class URenderAsset;

class FContentDrawerPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Content Drawer"; }
	void RenderFolderTree(const fs::path& Path, fs::path& SelectedPath);
	// 에셋 파일을 더블클릭하면 부른다. 어떤 에디터로 열지는 EditorEngine이 정한다
	void SetOpenAssetCallback(std::function<void(URenderAsset*)> InCallback) { OpenAssetCallback = std::move(InCallback); }

private:
	struct FFolderEntry
	{
		fs::path Path;
		bool bIsDirectory = false;
	};

	struct FFolderListing
	{
		TArray<FFolderEntry> Entries;
		bool bReadFailed = false;
		double LastReadTime = -1.0;
	};

	// 매 프레임 디스크를 읽지 않도록 폴더 목록을 잠시 캐시한다. 바뀐 파일은 다음 갱신 때 보인다.
	const FFolderListing& GetFolderListing(const fs::path& Path);

	std::unordered_map<std::string, FFolderListing> FolderCache;
	std::function<void(URenderAsset*)> OpenAssetCallback;
	// 사용자 콘텐츠(Assets)만 보여준다. 엔진 내장 파일(Resources)은 여기서 다루지 않는다.
	fs::path AssetRootPath = EditorResourcePaths::AssetRoot;
	fs::path CurrentPath = EditorResourcePaths::AssetRoot;
};