#pragma once

#include "GameFramework/Controller.h"
#include "Input/MouseCapture.h"
#include "Render/UIRenderer.h"

class APlayerCameraManager;

// 플레이어 한 명을 대표하는 Controller. UE의 APlayerController에 해당한다.
// 카메라(PlayerCameraManager), 화면 UI(HUD), 마우스 커서 모드를 소유한다.
// 게임·PIE World가 하나씩 만들며 씬에는 저장하지 않는다.
class APlayerController : public AController
{
	DECLARE_CLASS(APlayerController, AController)

public:
	~APlayerController() override;
	virtual void EndPlay() override;

	// 없으면 레벨에 배치된 것을 쓰고, 그것도 없으면 새로 스폰한다. 시점이 비어 있으면 조종 중인 Pawn 또는 World의 게임 카메라를 본다.
	APlayerCameraManager* GetPlayerCameraManager();
	// 저장된 씬에서 복원한 PlayerCameraManager를 이 Controller의 것으로 쓴다.
	void SetPlayerCameraManager(APlayerCameraManager* InCameraManager);
	// PlayerCameraManager가 지워질 때 World가 알린다.
	void ClearPlayerCameraManager(const APlayerCameraManager* InCameraManager);

	// Locked면 마우스를 게임 화면에 가두고 커서를 숨긴다. 실제 창 처리는 엔진(게임 창, PIE View)이 맡는다.
	void SetCursorMode(ECursorMode Mode);
	ECursorMode GetCursorMode() const { return CursorMode; }

	// 이번 프레임에 그릴 화면 UI. World Tick 시작마다 비우고 스크립트가 다시 채운다.
	FUIDrawList& GetHUD() { return HUD; }
	const FUIDrawList& GetHUD() const { return HUD; }

protected:
	virtual void OnPossess(APawn* InPawn) override;

private:
	friend class UWorld;
	APlayerCameraManager* PlayerCameraManager = nullptr;
	ECursorMode CursorMode = ECursorMode::Free;
	FUIDrawList HUD;
};
