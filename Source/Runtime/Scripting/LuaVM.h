#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"
#include "Core/CoroutineScheduler.h"

#include <sol/sol.hpp>

class FLuaVM final
{
public:
	FLuaVM() = default;
	~FLuaVM();

	FLuaVM(const FLuaVM&) = delete;
	FLuaVM& operator=(const FLuaVM&) = delete;

	bool Initialize();
	void Shutdown();

	bool ExecuteString(
		const FString& Source,
		const FString& ChunkName = "@StringChunk");

	bool ExecuteFile(const FString& ScriptPath);

	bool IsInitialized() const
	{
		return State != nullptr;
	}

	sol::state* GetState()
	{
		return State.get();
	}

	const sol::state* GetState() const
	{
		return State.get();
	}

	void UpdateCoroutine(float DeltaTime);
	void ResetCoroutines();

private:
	void BindEngineAPI();
	CoroutineScheduler FCoroutineScheduler;
	TUniquePtr<sol::state> State;
};
