#pragma once

#include <sol/forward.hpp>

class UActorComponent;

// 스크립트 하나의 환경에 소유 Actor의 World를 기준으로 동작하는 함수(시간, 뷰포트, 비네트, 씬 전환 등)를 등록한다.
// 함수는 호출될 때마다 Component의 현재 Owner와 World를 다시 찾는다.
void BindScriptWorldEnvironment(sol::environment& Environment, UActorComponent* Component);
