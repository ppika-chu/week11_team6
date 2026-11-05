#pragma once

#include "GameFramework/Actor.h"

class APawn;

// Pawn을 조종하는 Actor. UE의 AController에 해당한다.
class AController : public AActor
{
	DECLARE_CLASS(AController, AActor)

public:
	~AController() override;

	// 이전 Pawn은 놓고 새 Pawn을 조종한다. 다른 Controller가 조종 중인 Pawn은 빼앗아 온다.
	virtual void Possess(APawn* InPawn);
	virtual void UnPossess();
	APawn* GetPawn() const { return Pawn; }

	virtual void EndPlay() override;

protected:
	// Possess 직후 호출된다. 시점 연결처럼 하위 Controller가 덧붙일 일을 여기서 한다.
	virtual void OnPossess(APawn* InPawn) {}

private:
	friend class APawn;
	APawn* Pawn = nullptr;
};
