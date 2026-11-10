#pragma once

#include "EnginePCH.h"
#include "Editor/EditorUI/EditorPanel.h"
#include "ThirdParty/ImNeoSequencer/imgui_neo_sequencer.h"

#include "Sequence/SequencePlayer.h"
#include <functional>

class ASequenceActor;

class FSequencePanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;

	const char* GetPanelName() const override { return "Sequence"; }
	void SetViewportCameraProvider(std::function<bool(CameraKey&)> InProvider) { ViewportCameraProvider = std::move(InProvider); } // 키 기록용 현재 뷰포트 카메라
	void SetSequenceActorProvider(std::function<ASequenceActor*()> InProvider) { SequenceActorProvider = std::move(InProvider); }
	void SetCreateSequenceActorCallback(std::function<void()> InCallback) { CreateSequenceActorCallback = std::move(InCallback); }

private:
	static bool IsFadeLayoutValid(const FFadeTimelineTrack& Track);
	static bool IsAnimationLayoutValid(const FAnimationTimelineTrack& Track, int FPS);
	void SetPreviewActor(ASequenceActor* Actor);
	// EditorEngine에 요청해 편집할 Actor를 얻거나 새 Actor를 만든다. 패널이 Actor를 소유하지 않는다.
	std::function<ASequenceActor*()> SequenceActorProvider;
	std::function<void()> CreateSequenceActorCallback;
	// Add Key용 카메라 스냅샷을 채워주는 함수. 뷰포트 접근은 EditorEngine이 담당한다.
	std::function<bool(CameraKey&)> ViewportCameraProvider;

	int SelectedTrack = 0;
	// Fade 트랙 옆 입력란의 값. Add Key 시 키에 복사되며, 재생/커서 이동 시 보간값으로 갱신된다.
	float NewFadeKeyAlpha = 0.0f;
	// 같은 프레임에서 직접 입력한 값을 보간 결과로 덮어쓰지 않기 위한 상태.
	float LastFadeValueFrame = 0.0f;
	bool bFadeValueEdited = false;
	bool bResetSequenceSelection = false;
	// Actor 주소를 보관하지 않고 식별자만 비교해 편집 대상 변경을 감지한다.
	uint32 EditedActorUUID = 0;
	uint32 PreviewActorUUID = 0; // 애니메이션 미리보기 적용 중인 Actor
};
