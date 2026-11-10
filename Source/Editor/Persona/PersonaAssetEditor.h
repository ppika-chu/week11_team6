#pragma once
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/Persona/AnimationEditorPreviewScene.h"

class URenderAsset;
class USkeletalMesh;
class UTexture2D;
class USkeletalMeshComponent;
class FLineBatcher;

// 뷰포트에 본을 어디까지 그릴지
enum class EBoneDrawMode : uint8
{
	None,
	Selected,   // 선택한 본과 부모, 자식
	All,
};

enum class ESkinWeightViewMode : uint8
{
	Material, // 기본 스킨 색상
	Greyscale,
	Turbo,
	Jet,
	Viridis,
	Inferno,
};

// 프리뷰 씬을 쓰는 에셋 에디터의 공용 부모. 언리얼에 같은 클래스는 없고,
// 렌더링과 매니저가 에디터 종류를 몰라도 되게 하려고 둔다.
class FPersonaAssetEditor
{
public:
	FPersonaAssetEditor(USkeletalMesh* InMesh, int32 InEditorId);
	virtual ~FPersonaAssetEditor();

	FPersonaAssetEditor(const FPersonaAssetEditor&) = delete;
	FPersonaAssetEditor& operator=(const FPersonaAssetEditor&) = delete;

	// 안 보이면 skip
	virtual void Tick(float DeltaTime);
	// 루트 창과 패널을 그린다
	virtual void OnRender() = 0;

	// X로 닫혔는지 확인. Manager가 삭제할 때 씀
	bool IsOpen() const { return bIsOpen; }
	// 지난 프레임에 포커스였는지. 레벨 단축키를 막을 때 씀
	bool IsFocused() const { return bIsFocused; }
	// 다음 프레임에 이 창을 앞으로 가져오도록 예약
	void Focus() { bFocusRequested = true; }
	// 프리뷰에 쓰는 메시. 재임포트되면 IsRetired()가 true
	USkeletalMesh* GetMesh() const { return Mesh; }
	// 이 창이 여는 에셋. 같은 에셋이 이미 열려 있는지 비교할 때 씀
	virtual URenderAsset* GetEditedAsset() const = 0;
	// 재임포트로 에셋이 교체되었을 때 새 객체로 다시 연다. 새 객체가 없으면 nullptr
	virtual TUniquePtr<FPersonaAssetEditor> CreateReplacementEditor(int32 NewEditorId) const = 0;
	// 재임포트로 에디터를 새로 만들 때 선택을 옮긴다. 본 구성이 바뀌었을 수 있어 이름으로
	FString GetSelectedBoneName() const;
	virtual void SelectBoneByName(const FString& BoneName);

	// 이번 프레임에 프리뷰를 그려야 하는지. 뷰포트 패널이 보이고 렌더 타깃이 있을 때만
	bool ShouldRenderPreview() const { return bIsVisible && PreviewScene->GetColorTarget() != nullptr; }
	// UEditorEngine이 프리뷰 View를 그릴 때 쓰는 값들
	UWorld* GetPreviewWorld() const { return PreviewScene->GetWorld(); }
	const FEditorViewportClient& GetViewportClient() const { return PreviewScene->GetViewportClient(); }
	const FRenderingInfo& GetRenderingInfo() const { return PreviewScene->GetRenderingInfo(); }
	FTexture2D* GetHDRTarget() const { return PreviewScene->GetHDRTarget(); }
	// 레벨 View(1~5)와 겹치지 않는 그림자 / 라이트 캐시 키
	uint32 GetViewId() const { return 100u + static_cast<uint32>(EditorId); }
	// 현재 포즈의 본을 구 + 사각뿔 선으로 쌓는다. 그리기(OnRender)는 UEditorEngine이 한다
	void DrawBones(FLineBatcher& LineBatcher) const;
	// 그릴 기즈모. 없으면 nullptr. 그리기(FGizmoRenderer)는 UEditorEngine이 한다
	virtual const FGizmo* GetGizmo() const { return nullptr; }

protected:
	// 툴바(Show 메뉴) + 프리뷰 이미지(왼쪽 위 통계) + 입력
	void DrawViewport();
	// 프리뷰 이미지 위의 입력. 기본은 카메라 조작만
	virtual void HandleViewportInput();
	// 뷰포트 왼쪽 위 통계 아래에 붙일 줄. 언리얼 FAnimationViewportClient::GetDisplayInfo
	virtual FString GetDisplayInfo() const { return FString(); }
	// 뷰포트 보기 콤보(Lit / Skin Weights) 안에 파생 에디터가 하위 메뉴를 더 붙인다. 기본은 없음
	virtual void DrawViewModeMenuExtras() {}
	// 본 계층을 트리로 그리고, 클릭하면 SelectedBoneIndex 갱신. 언리얼도 두 에디터가 스켈레톤 트리를 같이 쓴다
	void DrawSkeletonTree();
	// 본 하나와 그 자식들을 재귀로 그린다
	void DrawBoneTreeNode(int32 BoneIndex);
	// Skin Weight 컬러 램프 동기화
	void SyncPreviewSkinWeightView();

	// 에셋 키(폴더/이름.fbxbin)에서 폴더와 확장자를 뺀 이름. 창 제목에 보인다
	static FString GetDisplayName(const FString& AssetKey);

	// Local * Parent 누적. 프리뷰 액터가 원점이라 Component 공간 = 프리뷰 월드 공간
	const TArray<FMatrix>& GetComponentSpaceTransforms() const;
	// Ancestor가 Bone의 부모 / 조상인지 (자기 자신은 false)
	bool IsAncestorOf(int32 Ancestor, int32 Bone) const;
	// 지금 모드에서 이 본을 그리는지. 그리는 본만 뷰포트에서 고를 수 있다
	bool ShouldDrawBone(int32 BoneIndex) const;
	// 관절 위치 (Component 공간 = 프리뷰 월드 공간)
	FVector GetBoneLocation(int32 BoneIndex) const;

	static constexpr float BoneDrawSize = 0.01f;    // 관절 구 반지름 (m). UPersonaOptions 기본값 1cm

	USkeletalMesh* Mesh = nullptr;  // 소유는 UAssetManager. 재임포트되어도 지워지지 않는다
	int32 EditorId = 0;
	TUniquePtr<FAnimationEditorPreviewScene> PreviewScene;

	TArray<TArray<int32>> BoneChildren;         // 트리 / 자식 연결용. 생성자에서 한 번 만든다
	TArray<int32> RootBones;                    // ParentIndex가 -1인 본들

	EBoneDrawMode BoneDrawMode = EBoneDrawMode::All;
	int32 SelectedBoneIndex = -1;
	bool bRevealSelectedInTree = false;         // 뷰포트에서 고르면 다음 트리 그릴 때 펼치고 스크롤

	// 언리얼 스켈레톤 트리처럼 정점 가중치가 있는 본과 없는 본을 다른 아이콘으로. 생성자에서 한 번 만든다
	TArray<uint8> BoneHasWeights;               // bool. TArray가 std::vector라 vector<bool>을 피한다
	UTexture2D* BoneIcon = nullptr;             // 소유는 UAssetManager
	UTexture2D* BoneNonWeightedIcon = nullptr;

	ESkinWeightViewMode SkinWeightViewMode = ESkinWeightViewMode::Material;

	bool bIsOpen = true;
	bool bIsVisible = false;    // 지난 프레임 뷰포트 패널의 Begin 결과
	bool bIsFocused = false;    // 지난 프레임에 루트 창이나 패널 중 하나가 포커스였는지
	bool bFocusRequested = true;
};
