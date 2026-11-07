#pragma once
#include "Editor/Persona/PersonaAssetEditor.h"

class UAnimSequence;

// 클립 하나를 재생해 보는 창. 언리얼 FAnimationEditor에 해당한다.
// 타임라인과 노티파이 트랙은 아직 없다.
class FAnimationEditor : public FPersonaAssetEditor
{
public:
	// 클립에는 메시가 없으므로 같은 스켈레톤의 메시를 받아 프리뷰에 쓴다
	FAnimationEditor(UAnimSequence* InSequence, USkeletalMesh* InPreviewMesh, int32 InEditorId);

	void OnRender() override;
	URenderAsset* GetEditedAsset() const override;
	TUniquePtr<FPersonaAssetEditor> CreateReplacementEditor(int32 NewEditorId) const override;

private:
	UAnimSequence* Sequence = nullptr; // 소유는 UAssetManager
	FString WindowTitle;               // 파일 이름###에셋 키. 에셋별로 창 위치가 저장된다
};
