#pragma once

#include "Core/EngineString.h"

class UWorld;

// 게임 월드의 Scene 파일 교체를 프레임 경계까지 지연한다.
// TargetWorld는 소유하지 않으므로 월드 파괴 전 CancelForWorld를 호출해야 한다.
class FSceneTransitionManager
{
public:
	void RequestLoadScene(UWorld* TargetWorld, const FString& ScenePath);
	void RequestRestartScene(UWorld* TargetWorld);

	// World Tick이 끝나고 렌더링용 포인터를 얻기 전에 한 번 호출한다.
	bool ProcessPendingTransition();
	void CancelForWorld(const UWorld* World);

private:
	struct FPendingTransition
	{
		UWorld* TargetWorld = nullptr;
		FString ScenePath;

		bool IsPending() const
		{
			return TargetWorld != nullptr && !ScenePath.empty();
		}

		void Reset()
		{
			TargetWorld = nullptr;
			ScenePath.clear();
		}
	};

	FPendingTransition PendingTransition;
};
