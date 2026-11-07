#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"

// 열린 Skeletal Mesh Editor 창들을 관리
// FEditorUI에는 Manager만 등록
class FSkeletalMeshEditorManager : public IEditorPanel
{
public:
	// 등록할 때 1번 호출. 지금은 비어 있음
	bool Init() override;
	// 지난 프레임에 닫힌 Editor를 삭제하고 재임포트된 에셋의 Editor를 새로 만든 뒤
	// 패널이 꺼져 있으면 skip, 켜져 있으면 모든 Editor의 Tick 호출
	void Tick(float DeltaTime) override;
	// 모든 Editor를 그린다. 닫힌 Editor는 ImGui가 SRV를 다 그린 다음 Tick에서 삭제
	void OnRender() override;
	// Window 메뉴에 쓰이는 이름
	const char* GetPanelName() const override { return "Skeletal Mesh Editors"; }

	// 같은 에셋이 열려 있으면 그 창에 Focus, 없으면 새로 생성
	void OpenEditor(USkeletalMesh* Mesh);

	// 열린 에디터를 모두 닫는다. 엔진 종료 시 UObject 일괄 삭제 전에 불러야 한다
	void CloseAllEditors() { Editors.Reset(); }

	// 이번 프레임에 프리뷰를 그릴 에디터들. 패널이 꺼져 있으면 비어 있다
	void GetPreviewsToRender(TArray<const FPersonaAssetEditor*>& OutEditors) const;

	// 열린 에디터 중 하나가 키보드 포커스를 가졌는지. 레벨 단축키(Delete, Space)를 막을 때 쓴다
	bool HasFocusedEditor() const;

private:
	// 재임포트로 retired 된 에셋을 같은 키의 새 객체로 다시 연다. 창 ID가 키라 위치 / 도킹은 그대로
	void ReopenRetiredEditors();

	TArray<TUniquePtr<FPersonaAssetEditor>> Editors;
	int32 NextEditorId = 0;     // 창마다 고유한 프리뷰 ViewId 번호. 닫아도 재사용하지 않는다
};
