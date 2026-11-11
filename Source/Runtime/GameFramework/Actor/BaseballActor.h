#pragma once

#include "GameFramework/Actor.h"

// 투구 Notify 시연용 공. Lua에서는 생성 위치와 회전만 지정한다.
class ABaseballActor : public AActor
{
	DECLARE_CLASS(ABaseballActor, AActor)

public:
	ABaseballActor();
};
