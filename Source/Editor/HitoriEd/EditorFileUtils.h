#pragma once

#include "Engine/World.h"

class FEditorFileUtils
{
public:
	static bool NewScene(UWorld* World);
	static bool SaveScene(UWorld* World);
	static bool SaveSceneAs(UWorld* World);
	static bool LoadScene(UWorld* World);
	// 취소하면 빈 문자열을 돌려준다.
	static FString OpenScriptDialog(const FString& InitialFolder);

private:
	static FString OpenSaveSceneDialog();
	static FString OpenLoadSceneDialog();

	static FString CurrentScenePath;
};
