#pragma once
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Render/RenderingInfo.h"
#include "Render/Texture2D.h"

class UWorld;
class UTexture2D;
class USkeletalMesh;
class USkeletalMeshComponent;
class AActor;
class AStaticMeshActor;
class FLineBatcher;

// 뷰포트에 본을 어디까지 그릴지
enum class EBoneDrawMode : uint8
{
    None,
    Selected,   // 선택한 본과 부모, 자식
    All,
};

// Asset 하나에 대응하는 편집창 하나
// 에셋은 읽기만 한다. 포즈는 BoneSpaceTransforms에만 쓴다
class FSkeletalMeshEditor
{
public:
    // 에셋 키(GetPath)로 창 제목(파일 이름###에셋 키) 생성. 고유 ID는 프리뷰 ViewId에 쓴다
    FSkeletalMeshEditor(USkeletalMesh* InMesh, int32 InEditorId);
    // PreviewWorld 정리
    ~FSkeletalMeshEditor();

    // PreviewWorld를 raw pointer로 소유하므로 복사 금지
    FSkeletalMeshEditor(const FSkeletalMeshEditor&) = delete;
    FSkeletalMeshEditor& operator=(const FSkeletalMeshEditor&) = delete;

    // 안 보이면 skip. 월드, 카메라, 스키닝 갱신이 들어올 곳
    void Tick(float DeltaTime);
    // 루트 창(X 버튼)과 전용 도킹 공간을 열고 트리, 뷰포트, 디테일 패널을 그림
    void OnRender();

    // X로 닫혔는지 확인. Manager가 삭제할 때 씀
    bool IsOpen() const { return bIsOpen; }
    // 지난 프레임에 포커스였는지. 레벨 단축키를 막을 때 씀
    bool IsFocused() const { return bIsFocused; }
    // 다음 프레임에 이 창을 앞으로 가져오도록 예약
    void Focus() { bFocusRequested = true; }
    // 같은 에셋이 이미 열려 있는지 비교할 때 씀. 재임포트되면 IsRetired()가 true
    USkeletalMesh* GetMesh() const { return Mesh; }
    // 재임포트로 에디터를 새로 만들 때 선택을 옮긴다. 본 구성이 바뀌었을 수 있어 이름으로
    FString GetSelectedBoneName() const;
    void SelectBoneByName(const FString& BoneName);

    // 이번 프레임에 프리뷰를 그려야 하는지. 뷰포트 패널이 보이고 렌더 타깃이 있을 때만
    bool ShouldRenderPreview() const { return bIsVisible && ColorTarget != nullptr; }
    // UEditorEngine이 프리뷰 View를 그릴 때 쓰는 값들
    UWorld* GetPreviewWorld() const { return PreviewWorld; }
    const FEditorViewportClient& GetViewportClient() const { return ViewportClient; }
    const FRenderingInfo& GetRenderingInfo() const { return RenderingInfo; }
    FTexture2D* GetHDRTarget() const { return HDRTarget.get(); }
    // 레벨 View(1~5)와 겹치지 않는 그림자 / 라이트 캐시 키
    uint32 GetViewId() const { return 100u + static_cast<uint32>(EditorId); }
    // 현재 포즈의 본을 구 + 사각뿔 선으로 쌓는다. 그리기(OnRender)는 UEditorEngine이 한다
    void DrawBones(FLineBatcher& LineBatcher) const;
    // 선택한 본의 기즈모. 그리기(FGizmoRenderer)는 UEditorEngine이 한다
    const FGizmo& GetBoneGizmo() const { return BoneGizmo; }

private:
    // 본 계층을 트리로 그리고, 클릭하면 SelectedBoneIndex 갱신
    void DrawSkeletonTree();
    // 본 하나와 그 자식들을 재귀로 그린다
    void DrawBoneTreeNode(int32 BoneIndex);
    // 툴바(Show 메뉴) + 프리뷰 이미지(왼쪽 위 통계) + 입력
    void DrawViewport();
    // 선택한 본의 이름, 부모 + Bone / Reference를 위아래로 (언리얼 BoneProxyDetailsCustomization)
    void DrawDetails();
    // 현재 포즈 편집. 칸 옆 아이콘으로 Local / World, Reference와 다르면 Reset 아이콘
    void DrawBoneTransform();
    // Bind Pose(RefBonePose, 부모 기준) 읽기 전용. 언리얼 GetRefBonePose
    void DrawReferenceTransform();

    // 프리뷰 월드 생성 + 라이트, 바닥 배치
    void CreatePreviewScene();
    // 메시에서 가장 낮은 정점의 Z. 바닥 높이
    float ComputeFloorHeight() const;
    // 원점에서 바닥 귀퉁이와 메시 정점 중 가장 먼 거리. 카메라 Far 계산에 쓴다
    float ComputeSceneRadius() const;
    // 뷰포트 칸 크기가 바뀌었으면 렌더 타깃을 다시 만든다
    void UpdateRenderTargets();
    // 오빗 값(Target, Yaw, Pitch, Distance)으로 카메라 위치와 방향 설정
    void UpdatePreviewCamera();
    // 메시 높이 가운데를 대상으로, 키의 2배 거리에서 보도록 초기화 (생성 시, F키)
    void FocusOnMesh();
    // 프리뷰 이미지 위의 마우스 입력으로 오빗 값 갱신
    void HandleViewportInput();
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
    // Local * Parent 누적. 프리뷰 액터가 원점이라 Component 공간 = 프리뷰 월드 공간
    const TArray<FMatrix>& GetComponentSpaceTransforms() const;
    // 본 하나의 부모 기준 자세를 바꾼다. 컴포넌트가 바로 다시 누적하고 스키닝한다
    void SetBoneSpaceTransform(int32 BoneIndex, const FTransform& Transform);
    // Ancestor가 Bone의 부모 / 조상인지 (자기 자신은 false)
    bool IsAncestorOf(int32 Ancestor, int32 Bone) const;
    // 지금 모드에서 이 본을 그리는지. 그리는 본만 뷰포트에서 고를 수 있다 (언리얼 HitProxy와 같음)
    bool ShouldDrawBone(int32 BoneIndex) const;
    // 관절 위치 (Component 공간 = 프리뷰 월드 공간)
    FVector GetBoneLocation(int32 BoneIndex) const;
    // 관절 회전 (Component 공간). 스케일을 뺀 축으로 구한다
    FRotator GetBoneRotation(int32 BoneIndex) const;
    // 관절 스케일 (Component 공간). 행렬 축 길이
    FVector GetBoneScale(int32 BoneIndex) const;

    USkeletalMesh* Mesh = nullptr;  // 소유는 UAssetManager. 재임포트되어도 지워지지 않는다
    int32 EditorId = 0;
    FString WindowTitle;    // 파일 이름###에셋 키. 에셋별로 창 위치가 저장된다
    // 패널 창 제목(이름###에셋 키). 에셋별로 패널 배치가 저장된다
    FString TreePanelTitle;
    FString ViewportPanelTitle;
    FString DetailsPanelTitle;

    UWorld* PreviewWorld = nullptr;         // 이 에디터 전용 월드. 엔진 WorldList에 넣지 않는다
    AStaticMeshActor* FloorActor = nullptr; // 소유는 PreviewWorld
    // 원점에 둔 빈 액터 + 스켈레탈 메시 컴포넌트 (언리얼 AAnimationEditorPreviewActor). 소유는 PreviewWorld
    AActor* PreviewActor = nullptr;
    USkeletalMeshComponent* PreviewComponent = nullptr;
    bool bShowFloor = true;
    bool bShowMesh = true;

    // 레벨 뷰포트 슬롯 / PIE 창과 같은 구성 (EditorEngine::UpdatePIEWindowTargets)
    FEditorViewportClient ViewportClient;
    TUniquePtr<FTexture2D> ColorTarget;     // 톤 매핑 결과. ImGui::Image로 표시
    TUniquePtr<FTexture2D> DepthTarget;
    TUniquePtr<FTexture2D> HDRTarget;       // 장면을 먼저 그리는 곳
    FRenderingInfo RenderingInfo{};
    uint32 TargetWidth = 0;
    uint32 TargetHeight = 0;
    uint32 DesiredWidth = 0;                // 지난 프레임 뷰포트 칸 크기
    uint32 DesiredHeight = 0;

    // 오빗 카메라. 각도는 도 단위, 바라보는 방향. Yaw -90 = +Y 쪽에서 -Y를 바라봄 (언리얼 Persona 기본)
    FVector OrbitTarget{ 0.0f, 0.0f, 0.0f };
    float OrbitYaw = -90.0f;
    float OrbitPitch = -15.0f;
    float OrbitDistance = 3.0f;               // m
    float SceneRadius = 0.0f;                 // m. 생성자에서 한 번 구한다

    TArray<TArray<int32>> BoneChildren;         // 트리 / 자식 연결용. 생성자에서 한 번 만든다
    TArray<int32> RootBones;                    // ParentIndex가 -1인 본들

    EBoneDrawMode BoneDrawMode = EBoneDrawMode::Selected;
    int32 SelectedBoneIndex = -1;
    FGizmo BoneGizmo;                           // 레벨 에디터와 따로. 결과 적용만 다르다
    bool bGizmoUsedThisClick = false;           // 이번 클릭이 기즈모 드래그였으면 놓을 때 본 선택을 하지 않는다

    // 언리얼 스켈레톤 트리처럼 정점 가중치가 있는 본과 없는 본을 다른 아이콘으로. 생성자에서 한 번 만든다
    TArray<uint8> BoneHasWeights;               // bool. TArray가 std::vector라 vector<bool>을 피한다
    UTexture2D* BoneIcon = nullptr;             // 소유는 UAssetManager
    UTexture2D* BoneNonWeightedIcon = nullptr;
    UTexture2D* ResetIcon = nullptr;
    // 디테일 칸 옆 공간 토글. 파일이 없으면 nullptr이고 글자 버튼으로 그린다
    UTexture2D* GizmoWorldIcon = nullptr;
    UTexture2D* GizmoLocalIcon = nullptr;
    // 디테일 칸별 표시 공간. 언리얼 UBoneProxy의 bLocalLocation / bLocalRotation / bLocalScale
    // 기즈모 축 공간도 지금 모드에 해당하는 값을 따른다
    bool bLocalLocation = true;
    bool bLocalRotation = true;
    bool bLocalScale = true;
    bool bRevealSelectedInTree = false;         // 뷰포트에서 고르면 다음 트리 그릴 때 펼치고 스크롤
    bool bIsOpen = true;
    bool bIsVisible = false;    // 지난 프레임 뷰포트 패널의 Begin 결과. 다른 도킹 탭 뒤면 false
    bool bIsFocused = false;    // 지난 프레임에 루트 창이나 패널 중 하나가 포커스였는지
    bool bFocusRequested = true;
};
