#include "EnginePCH.h"
#include "Engine/SceneTransitionManager.h"

#include "Engine/World.h"
#include "Serialization/JsonArchive.h"

void FSceneTransitionManager::RequestLoadScene(UWorld* TargetWorld, const FString& ScenePath)
{
	if (!TargetWorld || TargetWorld->GetWorldType() == EWorldType::Editor || TargetWorld->GetWorldType() == EWorldType::EditorPreview || ScenePath.empty())
	{
		HTR_LOG(Warning, "[Scene] Ignored invalid scene transition request");
		return;
	}

	PendingTransition.TargetWorld = TargetWorld;
	PendingTransition.ScenePath = std::filesystem::path(ScenePath).lexically_normal().string();
}

void FSceneTransitionManager::RequestRestartScene(UWorld* TargetWorld)
{
	if (!TargetWorld)
	{
		HTR_LOG(Warning, "[Scene] Cannot restart a null world");
		return;
	}

	RequestLoadScene(TargetWorld, TargetWorld->GetScenePath());
}

bool FSceneTransitionManager::ProcessPendingTransition()
{
	if (!PendingTransition.IsPending())
	{
		return false;
	}

	// EndPlay에서 발생할 수 있는 요청이 현재 요청에 덮이지 않게 먼저 비운다.
	FPendingTransition Request = PendingTransition;
	PendingTransition.Reset();

	UWorld* World = Request.TargetWorld;
	if (!World || World->GetWorldType() == EWorldType::Editor || World->GetWorldType() == EWorldType::EditorPreview)
	{
		return false;
	}

	std::error_code ErrorCode;
	if (!std::filesystem::is_regular_file(Request.ScenePath, ErrorCode) || ErrorCode)
	{
		HTR_LOG(Error, "[Scene] Scene file does not exist: {}", Request.ScenePath);
		return false;
	}

	// 실행 중인 Lua 함수와 충돌 처리가 모두 끝난 프레임 경계에서만 호출된다.
	World->EndPlay();
	if (!FJsonArchive::LoadWorld(World, Request.ScenePath))
	{
		HTR_LOG(Error, "[Scene] Failed to load scene: {}", Request.ScenePath);
		return false;
	}

	World->SetScenePath(Request.ScenePath);
	World->BeginPlay();
	HTR_LOG(Info, "[Scene] Loaded scene: {}", Request.ScenePath);
	return true;
}

void FSceneTransitionManager::CancelForWorld(const UWorld* World)
{
	if (PendingTransition.TargetWorld == World)
	{
		PendingTransition.Reset();
	}
}
