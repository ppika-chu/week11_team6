#include "EnginePCH.h"

#include "Editor/Persona/PersonaAssetEditor.h"
#include "Editor/EditorUI/EditorUIScale.h"

#include "Asset/AssetManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "DebugSkelMeshComponent.h"
#include "Render/LineBatcher.h"
#include "Render/Mesh.h"
#include "Render/SkeletalMesh.h"
#include "Render/Texture2D.h"

#include <imgui.h>
#include <cmath>
#include <cstdint>
#include <format>

namespace
{
// 언리얼 SkeletalDebugRendering과 같은 값
constexpr int32 NumSphereSides = 10; // 구를 이루는 원 하나의 선분 수
constexpr int32 NumConeSides = 4;    // 사각뿔


// 언리얼 UPersonaOptions 기본 색은 Linear 값이다. 프리뷰 타깃은 톤 매핑 뒤 화면 값이라 감마를 씌워 같은 색으로 보이게 한다
FVector4 LinearToDisplay(float R, float G, float B)
{
	return FVector4(std::pow(R, 1.0f / 2.2f), std::pow(G, 1.0f / 2.2f), std::pow(B, 1.0f / 2.2f), 1.0f);
}
const FVector4 DefaultBoneColor = LinearToDisplay(0.0f, 0.0f, 0.025f);           // 남색 (기본)
const FVector4 SelectedBoneColor = LinearToDisplay(0.2f, 1.0f, 0.2f);            // 초록 (선택)
const FVector4 AffectedBoneColor = LinearToDisplay(1.0f, 1.0f, 1.0f);            // 흰색 (자손)
const FVector4 ParentOfSelectedBoneColor = LinearToDisplay(0.85f, 0.45f, 0.12f); // 주황 (부모)
const FVector4 RootConnectionColor = LinearToDisplay(1.0f, 0.0f, 0.0f);          // 빨강 (루트)

// Center를 지나고 AxisA, AxisB가 만드는 평면 위의 원
void AddWireCircle(FLineBatcher& LineBatcher, const FVector& Center, const FVector& AxisA, const FVector& AxisB, float Radius, const FVector4& Color)
{
	const float Step = 2.0f * 3.14159265f / static_cast<float>(NumSphereSides);
	FVector Prev = Center + AxisA * Radius;
	for (int32 i = 1; i <= NumSphereSides; ++i)
	{
		const float Angle = Step * static_cast<float>(i);
		const FVector Next = Center + (AxisA * std::cos(Angle) + AxisB * std::sin(Angle)) * Radius;
		LineBatcher.AddLine(Prev, Next, Color);
		Prev = Next;
	}
}

// 본 축 기준 XY, XZ, YZ 원 3개
void AddWireSphere(FLineBatcher& LineBatcher, const FVector& Center, const FVector& X, const FVector& Y, const FVector& Z, float Radius, const FVector4& Color)
{
	AddWireCircle(LineBatcher, Center, X, Y, Radius, Color);
	AddWireCircle(LineBatcher, Center, X, Z, Radius, Color);
	AddWireCircle(LineBatcher, Center, Y, Z, Radius, Color);
}

//  Base 쪽 사각형에서 Apex로 모이는 사각뿔
void AddWireCone(FLineBatcher& LineBatcher, const FVector& Base, const FVector& Apex, float Radius, const FVector4& Color)
{
	const FVector ToApex = Apex - Base;
	if (ToApex.Size() < 1.0e-4f)
	{
		return;
	}
	const FVector Dir = ToApex.Normalized();
	const FVector Helper = std::abs(Dir.Z) < 0.99f ? FVector(0.0f, 0.0f, 1.0f) : FVector(1.0f, 0.0f, 0.0f);
	const FVector U = FVector::Cross(Dir, Helper).Normalized();
	const FVector V = FVector::Cross(Dir, U);

	FVector Corners[NumConeSides];
	for (int32 i = 0; i < NumConeSides; ++i)
	{
		const float Angle = 2.0f * 3.14159265f * static_cast<float>(i) / static_cast<float>(NumConeSides);
		Corners[i] = Base + (U * std::cos(Angle) + V * std::sin(Angle)) * Radius;
	}
	for (int32 i = 0; i < NumConeSides; ++i)
	{
		LineBatcher.AddLine(Corners[i], Apex, Color);
		LineBatcher.AddLine(Corners[i], Corners[(i + 1) % NumConeSides], Color);
	}
}

// Start를 구 반지름만큼 밀고, End(꼭짓점)까지 뿔
void AddConeConnection(FLineBatcher& LineBatcher, const FVector& Start, const FVector& End, float Radius, const FVector4& Color)
{
	const FVector Delta = End - Start;
	if (Delta.Size() <= Radius)
	{
		return;
	}
	AddWireCone(LineBatcher, Start + Delta.Normalized() * Radius, End, Radius, Color);
}

FString GetSkinWeightViewModeLabel(ESkinWeightViewMode ViewMode)
{
	switch (ViewMode)
	{
	case ESkinWeightViewMode::Material:
		return "Lit";
	case ESkinWeightViewMode::Greyscale:
		return "Greyscale";
	case ESkinWeightViewMode::Turbo:
		return "Turbo";
	case ESkinWeightViewMode::Jet:
		return "Jet";
	case ESkinWeightViewMode::Viridis:
		return "Viridis";
	case ESkinWeightViewMode::Inferno:
		return "Inferno";
	default:
		return "Unknown";
	}
}
}

FPersonaAssetEditor::FPersonaAssetEditor(USkeletalMesh* InMesh, int32 InEditorId)
	: Mesh(InMesh)
	, EditorId(InEditorId)
	, PreviewScene(MakeUnique<FAnimationEditorPreviewScene>(InMesh))
{
	// 자식 목록은 ParentIndex로 한 번만 만든다. 포즈는 프리뷰 컴포넌트가 들고 있다
	const FReferenceSkeleton& RefSkeleton = Mesh->RefSkeleton;
	const int32 NumBones = RefSkeleton.GetNum();
	BoneChildren.SetNum(NumBones);
	for (int32 i = 0; i < NumBones; ++i)
	{
		const int32 Parent = RefSkeleton.RefBoneInfo[i].ParentIndex;
		if (Parent < 0)
		{
			RootBones.Add(i);
		}
		else
		{
			BoneChildren[Parent].Add(i);
		}
	}

	// 가중치가 0보다 큰 정점이 하나라도 있으면 그 본은 메시를 움직인다
	BoneHasWeights.Init(0, NumBones);
	const FSkinWeightData& SkinWeights = Mesh->GetSkinWeights();
	for (int32 VertexIndex = 0; VertexIndex < SkinWeights.InfluenceBones.Num(); ++VertexIndex)
	{
		for (int32 i = 0; i < 4; ++i)
		{
			const int32 BoneIndex = SkinWeights.InfluenceBones[VertexIndex][i];
			if (SkinWeights.InfluenceWeights[VertexIndex][i] > 0.0f && BoneIndex < NumBones)
			{
				BoneHasWeights[BoneIndex] = 1;
			}
		}
	}

	// Resources는 스캔하지 않으므로 경로로 불러온다. AssetMap에 캐시된다
	BoneIcon = UAssetManager::Get().LoadTexture(EditorResourcePaths::BoneIcon);
	BoneNonWeightedIcon = UAssetManager::Get().LoadTexture(EditorResourcePaths::BoneNonWeightedIcon);
}

FPersonaAssetEditor::~FPersonaAssetEditor() = default;

void FPersonaAssetEditor::Tick(float DeltaTime)
{
	// Tick이 OnRender보다 먼저 돌기 때문에 지난 프레임의 가시성으로 판단한다 (1프레임 지연은 무시 가능)
	if (!bIsVisible)
	{
		return;
	}

	SyncPreviewSkinWeightView();

	PreviewScene->Tick(DeltaTime);
}

FString FPersonaAssetEditor::GetSelectedBoneName() const
{
	return SelectedBoneIndex >= 0 ? Mesh->RefSkeleton.RefBoneInfo[SelectedBoneIndex].Name : FString();
}

void FPersonaAssetEditor::SelectBoneByName(const FString& BoneName)
{
	SelectedBoneIndex = BoneName.empty() ? -1 : Mesh->RefSkeleton.FindBoneIndex(BoneName);
	bRevealSelectedInTree = SelectedBoneIndex >= 0;
}

void FPersonaAssetEditor::DrawSkeletonTree()
{
	// 키보드 내비게이션을 끈다. 켜 두면 클릭한 노드가 포커스를 받아 Space가 노드 접기 / 펼치기로 먹힌다
	ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
	for (const int32 RootIndex : RootBones)
	{
		DrawBoneTreeNode(RootIndex);
	}
	ImGui::PopItemFlag();

	// 펼치기 / 스크롤은 한 번만
	bRevealSelectedInTree = false;
}

void FPersonaAssetEditor::DrawBoneTreeNode(int32 BoneIndex)
{
	const TArray<int32>& Children = BoneChildren[BoneIndex];
	const bool bIsLeaf = Children.Num() == 0;

	// 화살표로 펼치고 이름 클릭으로 선택. 처음엔 모두 펼친 상태
	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
		| ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
	if (bIsLeaf)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}
	if (BoneIndex == SelectedBoneIndex)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}

	// 뷰포트에서 고른 본이 접힌 곳에 있으면 조상들을 펼친다
	if (bRevealSelectedInTree && !bIsLeaf && SelectedBoneIndex >= 0 && IsAncestorOf(BoneIndex, SelectedBoneIndex))
	{
		ImGui::SetNextItemOpen(true);
	}

	// ID는 본 인덱스. 이름이 같은 본이 있어도 겹치지 않는다
	// 라벨은 비우고 아이콘 + 이름을 뒤에 그린다. Image / Text는 클릭을 가로채지 않아 노드 선택은 그대로
	const bool bIsOpen = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(BoneIndex)), Flags, "");
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
	{
		SelectedBoneIndex = BoneIndex;
	}
	// 불러오지 못한 아이콘은 그리지 않는다
	UTexture2D* Icon = BoneHasWeights[BoneIndex] != 0 ? BoneIcon : BoneNonWeightedIcon;
	if (Icon && Icon->GetResource())
	{
		const float IconSize = ImGui::GetTextLineHeight();
		ImGui::SameLine();
		ImGui::Image((ImTextureID)Icon->GetResource()->GetSRV(), ImVec2(IconSize, IconSize));
	}
	ImGui::SameLine();
	ImGui::TextUnformatted(Mesh->RefSkeleton.RefBoneInfo[BoneIndex].Name.c_str());
	if (bRevealSelectedInTree && BoneIndex == SelectedBoneIndex)
	{
		ImGui::SetScrollHereY(0.5f);
	}

	if (bIsOpen && !bIsLeaf)
	{
		for (const int32 ChildIndex : Children)
		{
			DrawBoneTreeNode(ChildIndex);
		}
		ImGui::TreePop();
	}
}

void FPersonaAssetEditor::HandleViewportInput()
{
	PreviewScene->HandleCameraInput(true);
}

void FPersonaAssetEditor::SyncPreviewSkinWeightView()
{
	UDebugSkelMeshComponent* PreviewComponent = PreviewScene->GetPreviewComponent();
	PreviewComponent->SetShowBoneWeight(SkinWeightViewMode != ESkinWeightViewMode::Material);
	PreviewComponent->SetBoneOfInterest(SelectedBoneIndex);

	switch (SkinWeightViewMode)
	{
	case ESkinWeightViewMode::Greyscale:
		PreviewComponent->SetBoneWeightColorRamp(FSkinWeightColorRamp::Greyscale());
		break;
	case ESkinWeightViewMode::Turbo:
		PreviewComponent->SetBoneWeightColorRamp(FSkinWeightColorRamp::Turbo());
		break;
	case ESkinWeightViewMode::Jet:
		PreviewComponent->SetBoneWeightColorRamp(FSkinWeightColorRamp::Jet());
		break;
	case ESkinWeightViewMode::Viridis:
		PreviewComponent->SetBoneWeightColorRamp(FSkinWeightColorRamp::Viridis());
		break;
	case ESkinWeightViewMode::Inferno:
		PreviewComponent->SetBoneWeightColorRamp(FSkinWeightColorRamp::Inferno());
		break;
	default:
		break;
	}
}

FString FPersonaAssetEditor::GetDisplayName(const FString& AssetKey)
{
	const size_t Slash = AssetKey.find_last_of("/\\");
	FString Name = Slash == FString::npos ? AssetKey : AssetKey.substr(Slash + 1);
	const size_t Dot = Name.find_last_of('.');
	if (Dot != FString::npos && Dot > 0)
	{
		Name.resize(Dot);
	}
	return Name;
}

const TArray<FMatrix>& FPersonaAssetEditor::GetComponentSpaceTransforms() const
{
	return PreviewScene->GetPreviewComponent()->GetComponentSpaceTransforms();
}

bool FPersonaAssetEditor::IsAncestorOf(int32 Ancestor, int32 Bone) const
{
	const TArray<FMeshBoneInfo>& BoneInfo = Mesh->RefSkeleton.RefBoneInfo;
	for (int32 Parent = BoneInfo[Bone].ParentIndex; Parent >= 0; Parent = BoneInfo[Parent].ParentIndex)
	{
		if (Parent == Ancestor)
		{
			return true;
		}
	}
	return false;
}

bool FPersonaAssetEditor::ShouldDrawBone(int32 BoneIndex) const
{
	switch (BoneDrawMode)
	{
	case EBoneDrawMode::All:
		return true;
	case EBoneDrawMode::Selected:
		return BoneIndex == SelectedBoneIndex;
	default:
		return false;
	}
}

FVector FPersonaAssetEditor::GetBoneLocation(int32 BoneIndex) const
{
	// Row-vector 행렬이라 이동은 4번째 행
	const FMatrix& M = GetComponentSpaceTransforms()[BoneIndex];
	return FVector(M.M[3][0], M.M[3][1], M.M[3][2]);
}

void FPersonaAssetEditor::DrawBones(FLineBatcher& LineBatcher) const
{
	if (BoneDrawMode == EBoneDrawMode::None)
	{
		return;
	}

	// Root Motion 미리보기로 프리뷰 메시가 원점을 떠날 수 있어 컴포넌트 공간을 월드로 바꿔 그린다
	const FMatrix ComponentToWorld = PreviewScene->GetPreviewComponent()->GetWorldMatrix();
	const auto GetBoneWorldLocation = [&](int32 BoneIndex)
	{
		return ComponentToWorld.TransformPosition(GetBoneLocation(BoneIndex));
	};
	const FVector ComponentOrigin = ComponentToWorld.TransformPosition(FVector(0.0f, 0.0f, 0.0f));

	for (int32 BoneIndex = 0; BoneIndex < Mesh->RefSkeleton.GetNum(); ++BoneIndex)
	{
		if (!ShouldDrawBone(BoneIndex))
		{
			continue;
		}

		const bool bIsSelected = BoneIndex == SelectedBoneIndex;
		const bool bIsAffected = SelectedBoneIndex >= 0 && IsAncestorOf(SelectedBoneIndex, BoneIndex);
		const FVector4& BoneColor = bIsSelected ? SelectedBoneColor : (bIsAffected ? AffectedBoneColor : DefaultBoneColor);

		// 구는 본 축 방향으로 (스케일은 뺀다)
		const FMatrix M = GetComponentSpaceTransforms()[BoneIndex] * ComponentToWorld;
		const FVector AxisX = FVector(M.M[0][0], M.M[0][1], M.M[0][2]).Normalized();
		const FVector AxisY = FVector(M.M[1][0], M.M[1][1], M.M[1][2]).Normalized();
		const FVector AxisZ = FVector(M.M[2][0], M.M[2][1], M.M[2][2]).Normalized();
		const FVector BoneLocation = GetBoneWorldLocation(BoneIndex);
		AddWireSphere(LineBatcher, BoneLocation, AxisX, AxisY, AxisZ, BoneDrawSize, BoneColor);

		// 자식으로 가는 뿔. 양 끝을 구 반지름만큼 줄여 구와 겹치지 않게 한다
		for (const int32 ChildIndex : BoneChildren[BoneIndex])
		{
			const FVector ChildLocation = GetBoneWorldLocation(ChildIndex);
			const FVector Delta = ChildLocation - BoneLocation;
			if (Delta.Size() <= BoneDrawSize * 2.0f)
			{
				continue;
			}
			const FVector Offset = Delta.Normalized() * BoneDrawSize;
			const FVector4& ChildColor = (!bIsSelected && ChildIndex == SelectedBoneIndex) ? ParentOfSelectedBoneColor : BoneColor;
			AddWireCone(LineBatcher, BoneLocation + Offset, ChildLocation - Offset, BoneDrawSize, ChildColor);
		}

		if (Mesh->RefSkeleton.RefBoneInfo[BoneIndex].ParentIndex < 0)
		{
			AddConeConnection(LineBatcher, BoneLocation, ComponentOrigin, BoneDrawSize, RootConnectionColor);
		}
	}

	// Selected 모드에서는 부모 본을 그리지 않으므로, 부모 → 선택 본 뿔을 따로 그린다
	if (BoneDrawMode == EBoneDrawMode::Selected && SelectedBoneIndex >= 0)
	{
		const int32 ParentIndex = Mesh->RefSkeleton.RefBoneInfo[SelectedBoneIndex].ParentIndex;
		if (ParentIndex >= 0)
		{
			AddConeConnection(LineBatcher, GetBoneWorldLocation(ParentIndex), GetBoneWorldLocation(SelectedBoneIndex), BoneDrawSize, ParentOfSelectedBoneColor);
		}
	}
}

void FPersonaAssetEditor::DrawViewport()
{
	// 툴바: 표시 옵션을 Show 메뉴 하나로 (언리얼 Show / Character 메뉴). 기즈모 모드는 Space로 순환
	if (ImGui::Button("Show"))
	{
		ImGui::OpenPopup("##ViewportShowMenu");
	}
	if (ImGui::BeginPopup("##ViewportShowMenu"))
	{
		if (ImGui::MenuItem("Mesh", nullptr, PreviewScene->IsMeshVisible()))
		{
			PreviewScene->SetMeshVisible(!PreviewScene->IsMeshVisible());
		}
		if (ImGui::MenuItem("Floor", nullptr, PreviewScene->IsFloorVisible()))
		{
			PreviewScene->SetFloorVisible(!PreviewScene->IsFloorVisible());
		}
		ImGui::SeparatorText("Bones");
		if (ImGui::RadioButton("None", BoneDrawMode == EBoneDrawMode::None))
		{
			BoneDrawMode = EBoneDrawMode::None;
		}
		if (ImGui::RadioButton("Selected", BoneDrawMode == EBoneDrawMode::Selected))
		{
			BoneDrawMode = EBoneDrawMode::Selected;
		}
		if (ImGui::RadioButton("All", BoneDrawMode == EBoneDrawMode::All))
		{
			BoneDrawMode = EBoneDrawMode::All;
		}
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	ImGui::SetNextItemWidth(250.0f);
	if (ImGui::BeginCombo("##PersonaViewMode", GetSkinWeightViewModeLabel(SkinWeightViewMode).c_str()))
	{
		if (ImGui::MenuItem("Lit", nullptr, SkinWeightViewMode == ESkinWeightViewMode::Material))
		{
			SkinWeightViewMode = ESkinWeightViewMode::Material;
		}
		if (ImGui::BeginMenu("Skin Weights"))
		{
			if (ImGui::MenuItem("Greyscale", nullptr, SkinWeightViewMode == ESkinWeightViewMode::Greyscale))
			{
				SkinWeightViewMode = ESkinWeightViewMode::Greyscale;
			}
			if (ImGui::MenuItem("Turbo", nullptr, SkinWeightViewMode == ESkinWeightViewMode::Turbo))
			{
				SkinWeightViewMode = ESkinWeightViewMode::Turbo;
			}
			if (ImGui::MenuItem("Jet", nullptr, SkinWeightViewMode == ESkinWeightViewMode::Jet))
			{
				SkinWeightViewMode = ESkinWeightViewMode::Jet;
			}
			if (ImGui::MenuItem("Viridis", nullptr, SkinWeightViewMode == ESkinWeightViewMode::Viridis))
			{
				SkinWeightViewMode = ESkinWeightViewMode::Viridis;
			}
			if (ImGui::MenuItem("Inferno", nullptr, SkinWeightViewMode == ESkinWeightViewMode::Inferno))
			{
				SkinWeightViewMode = ESkinWeightViewMode::Inferno;
			}
			ImGui::EndMenu();
		}
		// 파생 에디터 전용 하위 메뉴 (애니메이션 에디터의 Root Motion)
		DrawViewModeMenuExtras();
		ImGui::EndCombo();
	}

	// 남은 영역 크기를 기록해 두면 다음 Tick에서 타깃을 맞춘다 (1프레임 지연)
	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	PreviewScene->SetDesiredSize(Avail.x > 1.0f ? static_cast<uint32>(Avail.x) : 0, Avail.y > 1.0f ? static_cast<uint32>(Avail.y) : 0);

	if (FTexture2D* ColorTarget = PreviewScene->GetColorTarget())
	{
		const ImVec2 ImageSize(static_cast<float>(PreviewScene->GetTargetWidth()), static_cast<float>(PreviewScene->GetTargetHeight()));
		const ImVec2 ImagePos = ImGui::GetCursorScreenPos();
		ImGui::Image((ImTextureID)ColorTarget->GetSRV(), ImageSize);

		// 왼쪽 위 통계 (레벨 뷰포트 stat 표시처럼 이미지 위에 겹쳐 그린다)
		FString Stats = std::format("Bones: {}\nVertices: {}\nTriangles: {}",
			Mesh->RefSkeleton.GetNum(), Mesh->GetMeshData().Vertices.Num(), Mesh->GetMeshData().Indices.Num() / 3);
		const FString DisplayInfo = GetDisplayInfo();
		if (!DisplayInfo.empty())
		{
			Stats += "\n" + DisplayInfo;
		}
		const ImVec2 StatsPos(ImagePos.x + EditorUIScale::Px(8.0f), ImagePos.y + EditorUIScale::Px(8.0f));
		const ImVec2 StatsSize = ImGui::CalcTextSize(Stats.c_str());
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		DrawList->AddRectFilled(ImVec2(StatsPos.x - EditorUIScale::Px(4.0f), StatsPos.y - EditorUIScale::Px(4.0f)),
			ImVec2(StatsPos.x + StatsSize.x + EditorUIScale::Px(4.0f), StatsPos.y + StatsSize.y + EditorUIScale::Px(4.0f)), IM_COL32(0, 0, 0, 110), EditorUIScale::Px(3.0f));
		DrawList->AddText(StatsPos, IM_COL32(255, 255, 255, 230), Stats.c_str());

		// Image는 입력을 받지 않으므로 같은 자리에 같은 크기의 투명 버튼을 올려 마우스 입력을 받는다
		ImGui::SetCursorScreenPos(ImagePos);
		ImGui::InvisibleButton("##PreviewViewportInput", ImageSize,
			ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		HandleViewportInput();
	}
}
