#pragma once

#include <memory>
#include <sol/forward.hpp>

struct FLuaAnimationGraphBindings;

// Lua 핸들이 그래프 객체를 소유하며 UAnimInstance와 노드의 연결은 실제 비소유 포인터다.
std::shared_ptr<FLuaAnimationGraphBindings> BindAnimationGraphAPI(sol::state& LuaState);
void CollectAnimationGraphReferences(const std::shared_ptr<FLuaAnimationGraphBindings>& Bindings);
// AnimInstance가 VM보다 오래 살아도 Lua 함수 참조를 lua_close 전에 해제한다.
void ReleaseAnimationGraphCallbacks(const std::shared_ptr<FLuaAnimationGraphBindings>& Bindings);
