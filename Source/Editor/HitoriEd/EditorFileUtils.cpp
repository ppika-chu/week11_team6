#include "EnginePCH.h"
#include "Editor/HitoriEd/EditorFileUtils.h"

#include "Core/EngineLog.h"
#include <commdlg.h>
#include "Serialization/JsonArchive.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"

FString FEditorFileUtils::CurrentScenePath = "";

namespace
{
// 파일 대화상자는 상대 경로를 무시할 수 있어 절대 경로로 넘긴다. 폴더가 없으면 만들어 둔다.
FString GetSceneFolderAbsolute()
{
	std::error_code Ec;
	std::filesystem::create_directories(EditorResourcePaths::SceneFolder, Ec);
	return std::filesystem::absolute(EditorResourcePaths::SceneFolder, Ec).lexically_normal().string();
}
}

bool FEditorFileUtils::NewScene(UWorld* World)
{
	if (!World)
	{
		return false;
	}

	World->ClearWorld();

	CurrentScenePath.clear();
	World->SetScenePath("");

	return true;
}

bool FEditorFileUtils::SaveScene(UWorld* World)
{
	if (!World)
	{
		return false;
	}

	if (CurrentScenePath.empty())
	{
		return SaveSceneAs(World);
	}

	if (!FJsonArchive::SaveWorld(World, CurrentScenePath))
	{
		HTR_LOG(Error, "Failed to save scene: {}", CurrentScenePath);

		MessageBoxW(
			GetActiveWindow(),
			L"씬을 저장하지 못했습니다.\n경로와 쓰기 권한을 확인하세요.",
			L"저장 실패",
			MB_OK | MB_ICONERROR);
		return false;
	}
	HTR_LOG(Info, "Save Scene : {}", CurrentScenePath);

	return true;
}

bool FEditorFileUtils::SaveSceneAs(UWorld* World)
{
	if (!World)
	{
		return false;
	}

	FString FilePath = OpenSaveSceneDialog();

	if (FilePath.empty())
	{
		return false;
	}

	FilePath = std::filesystem::absolute(FilePath).lexically_normal().string();

	CurrentScenePath = FilePath;
	World->SetScenePath(FilePath);

	if (!FJsonArchive::SaveWorld(World, FilePath))
	{
		HTR_LOG(Error, "Failed to save scene: {}", FilePath);

		MessageBoxW(
			GetActiveWindow(),
			L"씬을 저장하지 못했습니다.\n경로와 쓰기 권한을 확인하세요.",
			L"저장 실패",
			MB_OK | MB_ICONERROR);

		return false;
	}

	HTR_LOG(Info, "Save Scene As : {}", CurrentScenePath);

	return true;
}

bool FEditorFileUtils::LoadScene(UWorld* World)
{
	if (!World)
	{
		return false;
	}

	FString FilePath = OpenLoadSceneDialog();

	// 파일 선택 취소
	if (FilePath.empty())
	{
		return false;
	}

	FilePath = std::filesystem::absolute(FilePath).lexically_normal().string();

	// 이제 이 파일이 현재 Scene
	CurrentScenePath = FilePath;
	World->SetScenePath(FilePath);

	if (!FJsonArchive::LoadWorld(World, FilePath))
	{
		return false;
	}

	HTR_LOG(Info, "Load Scene : {}", CurrentScenePath);

	return true;
}

FString FEditorFileUtils::OpenSaveSceneDialog()
{
	char FilePath[MAX_PATH] = {};

	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(OPENFILENAMEA);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = FilePath;
	ofn.nMaxFile = MAX_PATH;

	ofn.lpstrFilter = "Scene Files (*.scene)\0*.scene\0";
	ofn.lpstrDefExt = "scene";
	const FString InitialDir = GetSceneFolderAbsolute();
	ofn.lpstrInitialDir = InitialDir.c_str();

	ofn.Flags =
		OFN_PATHMUSTEXIST |
		OFN_OVERWRITEPROMPT |
		OFN_NOCHANGEDIR;

	if (GetSaveFileNameA(&ofn))
	{
		return FString(FilePath);
	}

	return FString();
}

FString FEditorFileUtils::OpenScriptDialog(const FString& InitialFolder)
{
	char FilePath[MAX_PATH] = {};

	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(OPENFILENAMEA);
	ofn.hwndOwner = nullptr;

	ofn.lpstrFile = FilePath;
	ofn.nMaxFile = MAX_PATH;

	ofn.lpstrFilter = "Lua Scripts (*.lua)\0*.lua\0";
	ofn.lpstrDefExt = "lua";
	ofn.lpstrInitialDir = InitialFolder.c_str();

	ofn.Flags =
		OFN_FILEMUSTEXIST |
		OFN_PATHMUSTEXIST |
		OFN_NOCHANGEDIR;

	if (GetOpenFileNameA(&ofn))
	{
		return FString(FilePath);
	}

	return FString();
}

FString FEditorFileUtils::OpenLoadSceneDialog()
{
	char FilePath[MAX_PATH] = {};

	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(OPENFILENAMEA);
	ofn.hwndOwner = nullptr;

	ofn.lpstrFile = FilePath;
	ofn.nMaxFile = MAX_PATH;

	ofn.lpstrFilter =
		"Scene Files (*.scene)\0*.scene\0"
		"All Files (*.*)\0*.*\0";

	ofn.lpstrDefExt = "scene";
	const FString InitialDir = GetSceneFolderAbsolute();
	ofn.lpstrInitialDir = InitialDir.c_str();

	ofn.Flags =
		OFN_FILEMUSTEXIST |
		OFN_PATHMUSTEXIST |
		OFN_NOCHANGEDIR;

	if (GetOpenFileNameA(&ofn))
	{
		return FString(FilePath);
	}

	return FString();
}
