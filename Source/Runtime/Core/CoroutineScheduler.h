#pragma once

#include "EnginePCH.h"
#include <sol/sol.hpp>

struct CoroutineInfo
{
	float remainTime{};                      // 남은 시간
	std::function<void()> CoroutineFunction; // Couroutine 대기 완료 후 재개할 함수
	std::function<bool()> CheckFunction;     // 조건 Coroutine사 체크할 함수
};

class CoroutineScheduler
{
public:
	using CoroutineFunc = std::function<void()>;
	using CheckFunc = std::function<bool()>;

	void Reset()
	{
		CoroutineInfoArray.Reset();
		LuaCouroutineArray.Reset();
	}

	void Wait(CoroutineFunc Function, float Waittime) // Function은 Coroutine 대기 완료 후 재개할 함수 람다 형태로 추가
	{
		CoroutineInfo Coroutine;
		Coroutine.CoroutineFunction = Function;
		Coroutine.remainTime = Waittime;
		CoroutineInfoArray.Add(Coroutine); // CouroutineInfoArray에 해당 정보 추가
	}

	void Update(float deltaTime)
	{
		for (int i = CoroutineInfoArray.Num() - 1; i >= 0; i--)
		{
			if (CoroutineInfoArray[i].CheckFunction) // 조건 Coroutine일 경우와 시간 Coroutine일 경우 분리
			{
				if (CoroutineInfoArray[i].CheckFunction())
				{
					std::function<void()> callback = CoroutineInfoArray[i].CoroutineFunction; // Callback으로 실행
					callback();
					CoroutineInfoArray.RemoveAt(i); // Callback 실행했으므로 Coroutine 목록에서 제거
				}
			}
			else
			{
				CoroutineInfoArray[i].remainTime -= deltaTime; // 시간 DeltaTime만큼 감소
				if (CoroutineInfoArray[i].remainTime <= 0)
				{

					std::function<void()> callback = CoroutineInfoArray[i].CoroutineFunction; // Callback으로 실행
					callback();
					CoroutineInfoArray.RemoveAt(i); // Callback 실행했으므로 Coroutine 목록에서 제거
				}
			}
		}
	}

	void WaitUntil(CoroutineFunc Function, CheckFunc CheckFunc)
	{
		CoroutineInfo Coroutine;
		Coroutine.CoroutineFunction = Function;
		Coroutine.CheckFunction = CheckFunc;
		CoroutineInfoArray.Add(Coroutine); // CouroutineInfoArray에 해당 정보 추가
	}

	void RegisterLuaCoroutine(sol::thread LuaCour)
	{
		LuaCouroutineArray.Add(LuaCour);
	}

	void RunCoroutine(sol::state& lua, sol::protected_function resume, sol::thread LuaCoroutine) // Coroutine 반복 실행함수, Yield 만나고 값에 따라서 Wait Array에 값을 넣는 역할 담당
	{
		auto YieldResult = resume(LuaCoroutine); // Yield시 같이 넘기는 값을 Resume하면서 가져옴
		bool Success = YieldResult.get<bool>(0);
		if (Success) // Yield가 Success일 경우만 실행
		{
			sol::protected_function status = lua["coroutine"]["status"]; // Coroutine.status 실행해서 현재 상태 체크
			FString state = status(LuaCoroutine);

			if (state == "dead") // 함수가 끝났으므로 대기 정보를 읽지 않음
			{
				return;
			}

			if (state == "suspended") // 대기 상태이므로 Wait 체크
			{
				FString waittype = YieldResult.get<FString>(1);
				if (waittype == "wait")
				{
					float WaitTime = YieldResult.get<float>(2);
					Wait([this, resume, LuaCoroutine, &lua]()
						{ RunCoroutine(lua, resume, LuaCoroutine); }, WaitTime); // Wait가 있으면 대기열에 현재 람다를 넣음, 현재 람다가 끝나고 다음 Wait에도 실행되도록 RunCoroutine을 넣음
				}
				if (waittype == "wait_until")
				{
					sol::protected_function condition = YieldResult.get<sol::protected_function>(2);
					std::function<bool()> CheckFunction = [condition]()
					{if (condition().get<bool>()){return true;
} else{ return false;
} };
					WaitUntil([this, resume, LuaCoroutine, &lua]()
						{ RunCoroutine(lua, resume, LuaCoroutine); }, CheckFunction); // WaitUntil이 있으면 대기열에 현재 람다를 넣음, 현재 람다가 끝나고 다음 Wait에도 실행되도록 RunCoroutine을 넣음
				}
			}
		}
	}

	TArray<sol::thread> LuaCouroutineArray;

private:
	TArray<CoroutineInfo> CoroutineInfoArray;
};
