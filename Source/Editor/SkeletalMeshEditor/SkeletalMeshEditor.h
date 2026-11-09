#pragma once
#include "Editor/Persona/PersonaAssetEditor.h"

class UTexture2D;

// Asset 하나에 대응하는 편집창 하나
// 에셋은 읽기만 한다. 포즈는 BoneSpaceTransforms에만 쓴다
class FSkeletalMeshEditor : public FPersonaAssetEditor
{
public:
    // 에셋 키(GetPath)로 창 제목(파일 이름###에셋 키) 생성. 고유 ID는 프리뷰 ViewId에 쓴다
    FSkeletalMeshEditor(USkeletalMesh* InMesh, int32 InEditorId);
    // 루트 창(X 버튼)과 전용 도킹 공간을 열고 트리, 뷰포트, 디테일 패널을 그림
    void OnRender() override;

    void SelectBoneByName(const FString& BoneName) override;
    URenderAsset* GetEditedAsset() const override;
    TUniquePtr<FPersonaAssetEditor> CreateReplacementEditor(int32 NewEditorId) const override;

    // 선택한 본의 기즈모. 그리기(FGizmoRenderer)는 UEditorEngine이 한다
    const FGizmo* GetGizmo() const override { return &BoneGizmo; }

private:
    // 선택한 본의 이름, 부모 + Bone / Reference를 위아래로 (언리얼 BoneProxyDetailsCustomization)
    void DrawDetails();
    // 현재 포즈 편집. 칸 옆 아이콘으로 Local / World, Reference와 다르면 Reset 아이콘
    void DrawBoneTransform();
    // Bind Pose(RefBonePose, 부모 기준) 읽기 전용. 언리얼 GetRefBonePose
    void DrawReferenceTransform();

    // 프리뷰 이미지 위의 마우스 입력으로 오빗 값 갱신
    void HandleViewportInput() override;
    // 화면 좌표(이미지 기준 픽셀)에 그려진 본(구 / 뿔 + 5px)을 찾는다. 겹치면 카메라에 가까운 것. 없으면 -1
    int32 PickBone(float ScreenX, float ScreenY) const;
    // 선택한 본의 위치, Component 회전, Local 스케일과 축 공간을 기즈모에 넣는다. 선택이 없으면 숨긴다
    void SyncBoneGizmoPivot();
    // 기즈모 드래그 결과(Component 공간)를 부모 기준으로 바꿔 프리뷰 컴포넌트에 쓴다 (언리얼 Persona의 ModifyBone)
    void ApplyBoneGizmoDragResult();
    // Component 공간 위치 / 회전을 부모 기준으로 바꿔 프리뷰 컴포넌트에 쓴다. 기즈모와 디테일 패널(World)이 같이 쓴다
    void SetBoneComponentLocation(int32 BoneIndex, const FVector& ComponentLocation);
    void SetBoneComponentRotation(int32 BoneIndex, const FRotator& ComponentRotation);

    // 포즈는 프리뷰 컴포넌트가 들고 있다 (언리얼 UDebugSkelMeshComponent)
    // 부모 기준 자세. 처음엔 RefBonePose
    const TArray<FTransform>& GetBoneSpaceTransforms() const;
    // 본 하나의 부모 기준 자세를 바꾼다. 컴포넌트가 바로 다시 누적하고 스키닝한다
    void SetBoneSpaceTransform(int32 BoneIndex, const FTransform& Transform);
    // 관절 회전 (Component 공간). 스케일을 뺀 축으로 구한다
    FRotator GetBoneRotation(int32 BoneIndex) const;
    // 관절 스케일 (Component 공간). 행렬 축 길이
    FVector GetBoneScale(int32 BoneIndex) const;

    FString WindowTitle;    // 파일 이름###에셋 키. 에셋별로 창 위치가 저장된다
    // 패널 창 제목(이름###에셋 키). 에셋별로 패널 배치가 저장된다
    FString TreePanelTitle;
    FString ViewportPanelTitle;
    FString DetailsPanelTitle;

    FGizmo BoneGizmo;                           // 레벨 에디터와 따로. 결과 적용만 다르다
    bool bGizmoUsedThisClick = false;           // 이번 클릭이 기즈모 드래그였으면 놓을 때 본 선택을 하지 않는다

    UTexture2D* ResetIcon = nullptr;            // 소유는 UAssetManager
    // 디테일 칸 옆 공간 토글. 파일이 없으면 nullptr이고 글자 버튼으로 그린다
    UTexture2D* GizmoWorldIcon = nullptr;
    UTexture2D* GizmoLocalIcon = nullptr;
    // 디테일 칸별 표시 공간. 언리얼 UBoneProxy의 bLocalLocation / bLocalRotation / bLocalScale
    // 기즈모 축 공간도 지금 모드에 해당하는 값을 따른다
    bool bLocalLocation = true;
    bool bLocalRotation = true;
    bool bLocalScale = true;
};
