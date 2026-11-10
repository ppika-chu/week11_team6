#pragma once
#include "Editor/Persona/PersonaAssetEditor.h"

class UAnimSequence;
class UAnimSingleNodeInstance;
class UTexture2D;
class UClass;

// 클립 하나를 재생해 보는 창. 언리얼 FAnimationEditor에 해당한다.
// 패널: 스켈레톤 트리 | 뷰포트·타임라인 | Notify 목록·편집, Asset Details. 위에 저장 툴바.
class FAnimationEditor : public FPersonaAssetEditor
{
public:
	// 클립에는 메시가 없으므로 같은 스켈레톤의 메시를 받아 프리뷰에 쓴다
	FAnimationEditor(UAnimSequence* InSequence, USkeletalMesh* InPreviewMesh, int32 InEditorId);

	// 루트 창(X 버튼)과 전용 도킹 공간을 열고 트리, 뷰포트, 타임라인 패널을 그림
	void OnRender() override;
	URenderAsset* GetEditedAsset() const override;
	TUniquePtr<FPersonaAssetEditor> CreateReplacementEditor(int32 NewEditorId) const override;

private:
	// 프리뷰 컴포넌트의 재생 상태. 스켈레톤이 달라 클립을 못 받았으면 nullptr
	UAnimSingleNodeInstance* GetPreviewInstance() const;

	// 언리얼 SAnimTimeline. 레벨 시퀀서(SequencePanel)와 같은 ImNeoSequencer로 그리고, 아래에 재생 버튼
	void DrawTimeline();
	// Notify 선택은 타임라인과 목록이 공유하며, 배치 값은 Sequence에 저장한다.
	void DrawNotifiesTrack(float FrameRate);
	void DrawNotifyEditor();
	void CreateNewNotify(UClass* NotifyClass);
	void PrepareNotifyEdit();
	// 디스크의 편집 데이터(Notify, Root Motion 설정)로 되돌린다
	bool DiscardAssetChanges();
	void RefreshNotifyOffsets();
	// 언리얼 SAnimTimelineTransportControls. 처음 / 이전 프레임 / 역재생 / 재생 / 다음 프레임 / 끝 / 반복, 오른쪽에 속도
	void DrawTransportControls();
	// 속도 메뉴. 언리얼 타임라인 오른쪽의 x1.0 메뉴
	void DrawPlaybackSpeedMenu();
	// 뷰포트 왼쪽 위에 프레임 레이트
	FString GetDisplayInfo() const override;
	// 보기 콤보의 Root Motion 하위 메뉴. 언리얼 Character > Animation > Process Root Motion. 미리보기 전용이라 저장하지 않는다
	void DrawViewModeMenuExtras() override;
	// 패널들 위 툴바. 언리얼 에셋 에디터 툴바처럼 Save 버튼을 둔다
	void DrawToolbar();
	// 언리얼 애니메이션 에디터의 Asset Details. 지금은 Root Motion 항목만
	void DrawAssetDetails();
	// 편집 데이터(Notify, Root Motion 설정)를 이 클립의 .fbxbin에 쓴다. 결과는 SaveMessage에 남긴다 (UE 에셋 저장)
	bool SaveAsset();

	// 언리얼 SAnimTimelineTransportControls의 버튼 동작. 녹화(OnClick_Record)는 넣지 않는다
	void OnClick_Backward_End();
	void OnClick_Backward_Step();
	void OnClick_Backward();
	void OnClick_Forward();
	void OnClick_Forward_Step();
	void OnClick_Forward_End();
	void OnClick_ToggleLoop();
	// 타임라인을 누르거나 끌 때. 언리얼 SAnimTimeline::HandleScrubPositionChanged
	void HandleScrubPositionChanged(float NewTime);

	UAnimSequence* Sequence = nullptr; // 소유는 UAssetManager
	FString WindowTitle;               // 파일 이름###에셋 키. 에셋별로 창 위치가 저장된다
	// Notify나 Root Motion 설정을 바꿨지만 아직 파일에 쓰지 않았다. 메모리의 에셋에는 바로 적용된다 (UE MarkPackageDirty)
	bool bAssetDirty = false;
	// 패널 창 제목(이름###에셋 키). 에셋별로 패널 배치가 저장된다
	FString TreePanelTitle;
	FString ViewportPanelTitle;
	FString TimelinePanelTitle;
	FString DetailsPanelTitle;
	FString NotifyPanelTitle;
	int32 SelectedNotifyIndex = -1;
	// 한 번 누르고 끄는 동안 시작 시각과 마우스 위치를 유지한다.
	int32 DraggedNotifyIndex = -1;
	float NotifyDragStartX = 0.0f;
	float NotifyDragStartTime = 0.0f;
	FString SaveMessage; // 마지막 저장, 되돌리기 결과

	// 재생 버튼 아이콘. 소유는 UAssetManager. 파일이 없으면 글자 버튼
	UTexture2D* ToFrontIcon = nullptr;
	UTexture2D* ToPreviousIcon = nullptr;
	UTexture2D* PlayReverseIcon = nullptr;
	UTexture2D* PlayForwardIcon = nullptr;
	UTexture2D* PauseIcon = nullptr;
	UTexture2D* ToNextIcon = nullptr;
	UTexture2D* ToEndIcon = nullptr;
	UTexture2D* LoopingIcon = nullptr;
	UTexture2D* NoLoopingIcon = nullptr;
};
