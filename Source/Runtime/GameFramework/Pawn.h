#pragma once

#include "GameFramework/Actor.h"

class AController;
class UCameraComponent;

// Controller가 조종할 수 있는 Actor. UE의 APawn에 해당한다.
class APawn : public AActor
{
	DECLARE_CLASS(APawn, AActor)
	REFLECT_START(APawn)
		PROPERTY(bAutoPossessPlayer)
	REFLECT_END()

public:
	// 조종 중에 지워져도 Controller가 지워진 Pawn을 가리키지 않게 연결을 끊는다.
	~APawn() override;
	virtual void EndPlay() override;

	AController* GetController() const { return Controller; }
	bool IsPlayerControlled() const;

	// 이 Pawn이 가진 첫 카메라. 조종하는 PlayerController가 시점으로 쓴다.
	UCameraComponent* FindCameraComponent() const;

	// true면 게임 시작 시 PlayerController가 이 Pawn을 자동으로 조종한다.
	bool bAutoPossessPlayer = false;

private:
	friend class AController;
	AController* Controller = nullptr;
};
