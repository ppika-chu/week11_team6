#pragma once

#include <sol/forward.hpp>

class AActor;

void BindPlayerCameraManagerAPI(sol::state& LuaState);
void BindPlayerCameraManagerEnvironment(sol::environment& Environment, AActor* Owner);
