#include "EnginePCH.h"

#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Editor/EditorUI/EditorUI.h"
#include "Editor/EditorUI/EditorUIScale.h"

#include "Asset/AssetManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "Editor/Persona/DebugSkelMeshComponent.h"
#include "Render/Mesh.h"
#include "Render/SkeletalMesh.h"

#include <imgui.h>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <format>

namespace
{
	constexpr float BonePickMargin = 5.0f;   // 그려진 구 / 뿔 둘레 클릭 여유 (픽셀). 언리얼 FViewport::HitProxySize

	bool IsNearlyEqual(const FVector& A, const FVector& B, float Tolerance = 1.0e-3f)
	{
		return std::abs(A.X - B.X) <= Tolerance && std::abs(A.Y - B.Y) <= Tolerance && std::abs(A.Z - B.Z) <= Tolerance;
	}

	// 오일러 값은 같은 회전도 다르게 나올 수 있어서(180 / -180 등) 행렬로 비교한다
	bool IsSameRotation(const FRotator& A, const FRotator& B, float Tolerance = 1.0e-4f)
	{
		const FMatrix MA = A.Quaternion().ToFMatrix();
		const FMatrix MB = B.Quaternion().ToFMatrix();
		for (int32 Row = 0; Row < 3; ++Row)
		{
			for (int32 Col = 0; Col < 3; ++Col)
			{
				if (std::abs(MA.M[Row][Col] - MB.M[Row][Col]) > Tolerance)
				{
					return false;
				}
			}
		}
		return true;
	}

	enum class ETransformRowResult : uint8
	{
		None,
		Changed,
		Reset,
	};

	// 불러오지 못한 아이콘은 글자나 기본 화살표로 대신한다
	bool IsIconReady(const UTexture2D* Icon)
	{
		return Icon && Icon->GetResource();
	}

	// 표 안의 작은 아이콘 버튼. 배경은 투명
	bool DrawIconButton(const char* Id, UTexture2D* Icon)
	{
		const float IconSize = ImGui::GetFrameHeight() - 4.0f;
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		const bool bClicked = ImGui::ImageButton(Id, (ImTextureID)Icon->GetResource()->GetSRV(), ImVec2(IconSize, IconSize));
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
		return bClicked;
	}

	// 칸마다 붙는 공간 토글에 쓰는 아이콘들
	struct FTransformRowIcons
	{
		UTexture2D* Local = nullptr;
		UTexture2D* World = nullptr;
		UTexture2D* Reset = nullptr;
	};

	// 라벨 | 공간 | 값 | Reset 4칸 표 (언리얼 디테일 패널 배치)
	bool BeginTransformTable(const char* Id)
	{
		if (!ImGui::BeginTable(Id, 4, ImGuiTableFlags_SizingFixedFit))
		{
			return false;
		}
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Location").x);
		ImGui::TableSetupColumn("Space", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Reset", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
		return true;
	}

	// 한 줄. bLocalSpace가 nullptr이면 공간 토글을 안 그린다 (Reference)
	// 공간 아이콘은 언리얼 UBoneProxy의 bLocalLocation 등처럼 칸마다 따로. Reset 아이콘은 bCanReset일 때만
	ETransformRowResult DrawTransformRow(const char* Label, bool* bLocalSpace, float* Values, float Speed, bool bCanReset, const FTransformRowIcons& Icons)
	{
		ETransformRowResult Result = ETransformRowResult::None;
		ImGui::PushID(Label);
		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Label);

		ImGui::TableSetColumnIndex(1);
		if (bLocalSpace)
		{
			// 아이콘을 못 불러오면 글자
			UTexture2D* SpaceIcon = *bLocalSpace ? Icons.Local : Icons.World;
			const bool bClicked = IsIconReady(SpaceIcon) ? DrawIconButton("##Space", SpaceIcon) : ImGui::Button(*bLocalSpace ? "L##Space" : "W##Space", ImVec2(ImGui::GetFrameHeight(), 0.0f));
			if (bClicked)
			{
				*bLocalSpace = !*bLocalSpace;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip(*bLocalSpace ? "Local (parent space)" : "World (component space)");
			}
		}

		ImGui::TableSetColumnIndex(2);
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::DragFloat3("##Value", Values, Speed))
		{
			Result = ETransformRowResult::Changed;
		}

		ImGui::TableSetColumnIndex(3);
		if (bCanReset)
		{
			// 아이콘을 못 불러오면 ImGui 기본 화살표
			const bool bClicked = IsIconReady(Icons.Reset) ? DrawIconButton("##Reset", Icons.Reset) : ImGui::ArrowButton("##Reset", ImGuiDir_Left);
			if (bClicked)
			{
				Result = ETransformRowResult::Reset;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Reset to Reference");
			}
		}

		ImGui::PopID();
		return Result;
	}

}

namespace
{
	// 저장된 배치가 없을 때의 기본 배치: 왼쪽 트리 | 가운데 뷰포트 | 오른쪽 디테일
	void BuildDefaultPanelLayout(ImGuiID DockId, const ImVec2& Size, const char* TreePanel, const char* ViewportPanel, const char* DetailsPanel)
	{
		// 디테일 칸 초기 폭: 라벨 + 아이콘 2개 + 값 3칸("-179.999")에 표 / 창 여백을 더한다
		const ImGuiStyle& Style = ImGui::GetStyle();
		const float TreeWidth = EditorUIScale::Px(250.0f);
		const float DetailsWidth = ImGui::CalcTextSize("Location").x + ImGui::GetFrameHeight() * 2.0f
			+ (ImGui::CalcTextSize("-179.999").x + Style.FramePadding.x * 2.0f + Style.ItemInnerSpacing.x) * 3.0f
			+ Style.CellPadding.x * 8.0f + Style.WindowPadding.x * 2.0f;
		const float TreeRatio = std::clamp(TreeWidth / Size.x, 0.1f, 0.4f);
		const float DetailsRatio = std::clamp(DetailsWidth / (Size.x - TreeWidth), 0.1f, 0.6f);

		ImGui::DockBuilderAddNode(DockId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(DockId, Size);

		ImGuiID CenterId = DockId;
		const ImGuiID TreeId = ImGui::DockBuilderSplitNode(CenterId, ImGuiDir_Left, TreeRatio, nullptr, &CenterId);
		const ImGuiID DetailsId = ImGui::DockBuilderSplitNode(CenterId, ImGuiDir_Right, DetailsRatio, nullptr, &CenterId);

		ImGui::DockBuilderDockWindow(TreePanel, TreeId);
		ImGui::DockBuilderDockWindow(ViewportPanel, CenterId);
		ImGui::DockBuilderDockWindow(DetailsPanel, DetailsId);
		ImGui::DockBuilderFinish(DockId);
	}
}

FSkeletalMeshEditor::FSkeletalMeshEditor(USkeletalMesh* InMesh, int32 InEditorId)
	: FPersonaAssetEditor(InMesh, InEditorId)
	// ### 뒤가 ImGui 창 ID. 에셋 키로 두어 imgui.ini에 에셋별 마지막 위치 / 도킹이 저장되게 한다
	// 같은 에셋은 Manager가 한 창만 열게 막고, 재임포트한 새 객체도 키가 같아 같은 자리에 열린다
	, WindowTitle(std::format("{} - Skeletal Mesh Editor###SkeletalMeshEditor_{}", GetDisplayName(InMesh->GetPath()), InMesh->GetPath()))
	, TreePanelTitle(std::format("Skeleton Tree###SkeletonTree_{}", InMesh->GetPath()))
	, ViewportPanelTitle(std::format("Viewport###SkeletalMeshViewport_{}", InMesh->GetPath()))
	, DetailsPanelTitle(std::format("Details###SkeletalMeshDetails_{}", InMesh->GetPath()))
{
	// 가중치가 0보다 큰 정점이 하나라도 있으면 그 본은 메시를 움직인다
	const int32 NumBones = Mesh->RefSkeleton.GetNum();
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
	ResetIcon = UAssetManager::Get().LoadTexture(EditorResourcePaths::ResetToDefaultIcon);
	GizmoWorldIcon = UAssetManager::Get().LoadTexture(EditorResourcePaths::GizmoWorldIcon);
	GizmoLocalIcon = UAssetManager::Get().LoadTexture(EditorResourcePaths::GizmoLocalIcon);
}

URenderAsset* FSkeletalMeshEditor::GetEditedAsset() const
{
	return Mesh;
}

TUniquePtr<FPersonaAssetEditor> FSkeletalMeshEditor::CreateReplacementEditor(int32 NewEditorId) const
{
	// 키로 찾으면 retired는 건너뛰므로 새 객체가 나온다. 없으면(지워졌거나 다른 타입) 옛 객체를 계속 보여 준다
	USkeletalMesh* NewMesh = UAssetManager::GetAssetByPath<USkeletalMesh>(Mesh->GetPath());
	if (!NewMesh)
	{
		return nullptr;
	}

	// 포즈는 새 Bind Pose로 시작하고, 선택한 본은 이름이 남아 있으면 유지한다
	auto NewEditor = MakeUnique<FSkeletalMeshEditor>(NewMesh, NewEditorId);
	NewEditor->SelectBoneByName(GetSelectedBoneName());
	return NewEditor;
}

void FSkeletalMeshEditor::SelectBoneByName(const FString& BoneName)
{
	FPersonaAssetEditor::SelectBoneByName(BoneName);
	bRevealSelectedInTree = SelectedBoneIndex >= 0;
	SyncBoneGizmoPivot();
}

const TArray<FTransform>& FSkeletalMeshEditor::GetBoneSpaceTransforms() const
{
	return PreviewScene->GetPreviewComponent()->GetBoneSpaceTransforms();
}

void FSkeletalMeshEditor::SetBoneSpaceTransform(int32 BoneIndex, const FTransform& Transform)
{
	// 컴포넌트가 바로 누적 + 스키닝하므로 같은 프레임에 그리는 본, 기즈모, 메시가 새 포즈를 쓴다
	PreviewScene->GetPreviewComponent()->SetBoneLocalTransform(BoneIndex, Transform);
}

FRotator FSkeletalMeshEditor::GetBoneRotation(int32 BoneIndex) const
{
	// USceneComponent::SetWorldTransform과 같은 방법. 축 길이(스케일)를 빼고 회전만 남긴다
	const FMatrix& M = GetComponentSpaceTransforms()[BoneIndex];
	FMatrix Rotation = FMatrix::Identity;
	Rotation.SetAxis(0, FVector(M.M[0][0], M.M[0][1], M.M[0][2]).Normalized());
	Rotation.SetAxis(1, FVector(M.M[1][0], M.M[1][1], M.M[1][2]).Normalized());
	Rotation.SetAxis(2, FVector(M.M[2][0], M.M[2][1], M.M[2][2]).Normalized());
	return MatrixToRotator(Rotation);
}

void FSkeletalMeshEditor::SyncBoneGizmoPivot()
{
	if (SelectedBoneIndex < 0)
	{
		BoneGizmo.ClearPivot();
		return;
	}

	// 기즈모 축 공간은 지금 모드에 해당하는 디테일 칸의 Local / World를 따른다
	const EGizmoMode Mode = BoneGizmo.GetMode();
	const bool bLocal = Mode == EGizmoMode::Location ? bLocalLocation : (Mode == EGizmoMode::Rotation ? bLocalRotation : bLocalScale);
	BoneGizmo.SetSpace(bLocal ? EGizmoSpace::Local : EGizmoSpace::World);

	// 축은 본의 Component 회전, 스케일은 Local 값 (언리얼처럼 스케일은 부모 기준으로 조작)
	BoneGizmo.SetPivot(GetBoneLocation(SelectedBoneIndex), GetBoneRotation(SelectedBoneIndex), GetBoneSpaceTransforms()[SelectedBoneIndex].Scale);
}

void FSkeletalMeshEditor::ApplyBoneGizmoDragResult()
{
	if (SelectedBoneIndex < 0 || !BoneGizmo.HasDragResult())
	{
		return;
	}

	const FGizmoDragResult& Result = BoneGizmo.GetDragResult();
	switch (BoneGizmo.GetMode())
	{
	case EGizmoMode::Location:
		SetBoneComponentLocation(SelectedBoneIndex, Result.Location);
		break;
	case EGizmoMode::Rotation:
		SetBoneComponentRotation(SelectedBoneIndex, Result.Rotation);
		break;
	case EGizmoMode::Scale:
	{
		FTransform Local = GetBoneSpaceTransforms()[SelectedBoneIndex];
		Local.Scale = Result.Scale;
		SetBoneSpaceTransform(SelectedBoneIndex, Local);
		break;
	}
	}
}

void FSkeletalMeshEditor::SetBoneComponentLocation(int32 BoneIndex, const FVector& ComponentLocation)
{
	// Component = Local * Parent 이므로 Local = Component * Parent^-1. 루트는 Component가 곧 Local
	const int32 Parent = Mesh->RefSkeleton.RefBoneInfo[BoneIndex].ParentIndex;
	FTransform Local = GetBoneSpaceTransforms()[BoneIndex];
	Local.Location = Parent < 0 ? ComponentLocation : GetComponentSpaceTransforms()[Parent].Inverse().TransformPosition(ComponentLocation);
	SetBoneSpaceTransform(BoneIndex, Local);
}

void FSkeletalMeshEditor::SetBoneComponentRotation(int32 BoneIndex, const FRotator& ComponentRotation)
{
	const int32 Parent = Mesh->RefSkeleton.RefBoneInfo[BoneIndex].ParentIndex;
	FTransform Local = GetBoneSpaceTransforms()[BoneIndex];
	if (Parent < 0)
	{
		Local.Rotation = ComponentRotation;
	}
	else
	{
		// 회전 행렬은 직교라 역행렬 = 전치
		const FMatrix NewRotation = ComponentRotation.Quaternion().ToFMatrix();
		const FMatrix ParentRotation = GetBoneRotation(Parent).Quaternion().ToFMatrix();
		Local.Rotation = MatrixToRotator(NewRotation * ParentRotation.GetTransposed());
	}
	SetBoneSpaceTransform(BoneIndex, Local);
}

FVector FSkeletalMeshEditor::GetBoneScale(int32 BoneIndex) const
{
	// 축 길이가 곧 누적 스케일 (비균등 스케일 + 회전이면 근사)
	const FMatrix& M = GetComponentSpaceTransforms()[BoneIndex];
	return FVector(
		FVector(M.M[0][0], M.M[0][1], M.M[0][2]).Size(),
		FVector(M.M[1][0], M.M[1][1], M.M[1][2]).Size(),
		FVector(M.M[2][0], M.M[2][1], M.M[2][2]).Size());
}

int32 FSkeletalMeshEditor::PickBone(float ScreenX, float ScreenY) const
{
	const uint32 TargetWidth = PreviewScene->GetTargetWidth();
	const uint32 TargetHeight = PreviewScene->GetTargetHeight();
	if (TargetWidth == 0 || TargetHeight == 0)
	{
		return -1;
	}
	const FEditorViewportClient& ViewportClient = GetViewportClient();

	// 언리얼 HitProxy 흉내: DrawBones가 그린 구와 뿔을 화면에 투영해, 그 모양 + 5px 안을 누르면 그 본이다
	//  - 구: 관절 중심에서 (투영된 구 반지름 + 5px) 안
	//  - 뿔: 뼈 선분에서 (투영된 뿔 두께 + 5px) 안. 뿔은 그 뿔을 그린 본(부모 쪽) 소속 (DrawWireBoneAdvanced와 같음)
	//  - 겹치면: 모양 안을 직접 누른 것 중 카메라에 가까운 것 (HitProxy 버퍼의 깊이 테스트)
	//            직접 누른 게 없으면 여유 범위 안에서 화면상 가장 가까운 것
	FMatrix View;
	FMatrix Projection;
	ViewportClient.CalcViewMatrices(View, Projection);
	const FMatrix ViewProjection = View * Projection;
	const float Width = static_cast<float>(TargetWidth);
	const float Height = static_cast<float>(TargetHeight);
	// 깊이 W에서 월드 1m가 화면 몇 px인지 = ScaleY / W * (높이 / 2)
	const float PixelsPerUnitAtW1 = Projection.M[2][1] * 0.5f * Height;

	struct FScreenPoint
	{
		float X = 0.0f;
		float Y = 0.0f;
		float W = 0.0f;  // 카메라와의 거리. 0 이하면 카메라 뒤
	};
	const auto ProjectToScreen = [&](const FVector& Location)
	{
		const FVector4 Clip = ViewProjection.TransformFVector4(FVector4(Location, 1.0f));
		FScreenPoint Result;
		Result.W = Clip.W;
		if (Clip.W > 0.0f)
		{
			// NDC(-1~1, Y 위) → 이미지 픽셀(0~크기, Y 아래)
			Result.X = (Clip.X / Clip.W * 0.5f + 0.5f) * Width;
			Result.Y = (0.5f - Clip.Y / Clip.W * 0.5f) * Height;
		}
		return Result;
	};

	int32 BestDirectBone = -1;           // 모양 안을 직접 누른 후보 중 카메라에 가장 가까운 것
	float BestDirectDepth = FLT_MAX;
	int32 BestMarginBone = -1;           // 여유 범위에만 걸친 후보 중 화면상 가장 가까운 것
	float BestMarginGap = FLT_MAX;

	// Distance: 마우스 ~ 모양 중심선 거리, ShapeRadius: 그 지점에서 모양의 화면 반지름
	const auto Consider = [&](int32 BoneIndex, float Distance, float ShapeRadius, float Depth)
	{
		if (Distance <= ShapeRadius)
		{
			if (Depth < BestDirectDepth)
			{
				BestDirectDepth = Depth;
				BestDirectBone = BoneIndex;
			}
		}
		else if (Distance <= ShapeRadius + BonePickMargin)
		{
			const float Gap = Distance - ShapeRadius;
			if (Gap < BestMarginGap)
			{
				BestMarginGap = Gap;
				BestMarginBone = BoneIndex;
			}
		}
	};

	// 뿔: Base(반지름 BoneDrawSize)에서 Apex(0)로 가늘어지는 선분
	const auto ConsiderCone = [&](int32 OwnerBone, const FVector& Base, const FVector& Apex)
	{
		const FScreenPoint A = ProjectToScreen(Base);
		const FScreenPoint B = ProjectToScreen(Apex);
		if (A.W <= 0.0f || B.W <= 0.0f)
		{
			return;
		}
		// 마우스에서 선분 AB로 내린 수선의 위치 T(0~1)
		const float ABX = B.X - A.X;
		const float ABY = B.Y - A.Y;
		const float LengthSq = ABX * ABX + ABY * ABY;
		float T = 0.0f;
		if (LengthSq > 1.0e-6f)
		{
			T = ((ScreenX - A.X) * ABX + (ScreenY - A.Y) * ABY) / LengthSq;
			T = T < 0.0f ? 0.0f : (T > 1.0f ? 1.0f : T);
		}
		const float DX = ScreenX - (A.X + ABX * T);
		const float DY = ScreenY - (A.Y + ABY * T);
		const float Depth = A.W + (B.W - A.W) * T;
		const float ConeRadius = BoneDrawSize * (1.0f - T) * PixelsPerUnitAtW1 / Depth;
		Consider(OwnerBone, std::sqrt(DX * DX + DY * DY), ConeRadius, Depth);
	};

	const FVector ComponentOrigin(0.0f, 0.0f, 0.0f);
	for (int32 BoneIndex = 0; BoneIndex < Mesh->RefSkeleton.GetNum(); ++BoneIndex)
	{
		if (!ShouldDrawBone(BoneIndex))
		{
			continue;
		}

		// 구
		const FVector BoneLocation = GetBoneLocation(BoneIndex);
		const FScreenPoint Joint = ProjectToScreen(BoneLocation);
		if (Joint.W > 0.0f)
		{
			const float DX = ScreenX - Joint.X;
			const float DY = ScreenY - Joint.Y;
			Consider(BoneIndex, std::sqrt(DX * DX + DY * DY), BoneDrawSize * PixelsPerUnitAtW1 / Joint.W, Joint.W);
		}

		// 자식으로 가는 뿔 (DrawBones와 같은 위치: 양 끝을 구 반지름만큼 줄임)
		for (const int32 ChildIndex : BoneChildren[BoneIndex])
		{
			const FVector ChildLocation = GetBoneLocation(ChildIndex);
			const FVector Delta = ChildLocation - BoneLocation;
			if (Delta.Size() <= BoneDrawSize * 2.0f)
			{
				continue;
			}
			const FVector Offset = Delta.Normalized() * BoneDrawSize;
			ConsiderCone(BoneIndex, BoneLocation + Offset, ChildLocation - Offset);
		}

		// 루트 → 컴포넌트 원점 뿔
		if (Mesh->RefSkeleton.RefBoneInfo[BoneIndex].ParentIndex < 0)
		{
			const FVector Delta = ComponentOrigin - BoneLocation;
			if (Delta.Size() > BoneDrawSize)
			{
				ConsiderCone(BoneIndex, BoneLocation + Delta.Normalized() * BoneDrawSize, ComponentOrigin);
			}
		}
	}

	// Selected 모드에서 따로 그린 부모 → 선택 본 뿔. 선택 본을 위해 그린 것이라 선택 본 소속
	if (BoneDrawMode == EBoneDrawMode::Selected && SelectedBoneIndex >= 0)
	{
		const int32 ParentIndex = Mesh->RefSkeleton.RefBoneInfo[SelectedBoneIndex].ParentIndex;
		if (ParentIndex >= 0)
		{
			const FVector ParentLocation = GetBoneLocation(ParentIndex);
			const FVector Delta = GetBoneLocation(SelectedBoneIndex) - ParentLocation;
			if (Delta.Size() > BoneDrawSize)
			{
				ConsiderCone(SelectedBoneIndex, ParentLocation + Delta.Normalized() * BoneDrawSize, GetBoneLocation(SelectedBoneIndex));
			}
		}
	}

	return BestDirectBone >= 0 ? BestDirectBone : BestMarginBone;
}

void FSkeletalMeshEditor::HandleViewportInput()
{
	const ImGuiIO& IO = ImGui::GetIO();
	const ImVec2 ImageMin = ImGui::GetItemRectMin();
	const FVector2 MousePos(IO.MousePos.x - ImageMin.x, IO.MousePos.y - ImageMin.y);

	// 기즈모. 위치를 넣어 주고 결과를 본에 적용한다 (레벨 에디터의 UpdateGizmoAndPicking과 같은 흐름)
	SyncBoneGizmoPivot();
	if (ImGui::IsItemHovered() || BoneGizmo.IsUsing())
	{
		FEditorViewportClient& ViewportClient = PreviewScene->GetViewportClient();
		const uint32 TargetWidth = PreviewScene->GetTargetWidth();
		const uint32 TargetHeight = PreviewScene->GetTargetHeight();
		// 누르는 순간에만 드래그를 시작한다. Alt + 좌클릭은 오빗
		const bool bGizmoMouseDown = BoneGizmo.IsUsing()
			? ImGui::IsMouseDown(ImGuiMouseButton_Left)
			: ImGui::IsItemClicked(ImGuiMouseButton_Left) && !IO.KeyAlt;
		BoneGizmo.Update(
			ViewportClient.DeprojectScreen(MousePos),
			MousePos,
			ViewportClient.CalcViewProjection(),
			static_cast<int>(TargetWidth),
			static_cast<int>(TargetHeight),
			bGizmoMouseDown,
			ViewportClient.GetViewLocation(),
			ViewportClient.IsOrthographic());
		ApplyBoneGizmoDragResult();
		SyncBoneGizmoPivot();
		bGizmoUsedThisClick = bGizmoUsedThisClick || BoneGizmo.IsUsing();
	}

	// 기즈모 드래그 중에는 오빗하지 않는다
	PreviewScene->HandleCameraInput(!BoneGizmo.IsUsing());

	// 선택. 빈 곳이면 선택 해제. 기즈모를 누른 클릭은 제외
	const float DragThresholdSq = IO.MouseDragThreshold * IO.MouseDragThreshold;
	if (ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !IO.KeyAlt
		&& !bGizmoUsedThisClick && IO.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < DragThresholdSq)
	{
		SelectedBoneIndex = PickBone(MousePos.X, MousePos.Y);
		bRevealSelectedInTree = SelectedBoneIndex >= 0;
		SyncBoneGizmoPivot();
	}
	if (ImGui::IsItemDeactivated())
	{
		bGizmoUsedThisClick = false;
	}
}

void FSkeletalMeshEditor::OnRender()
{
	if (bFocusRequested)
	{
		ImGui::SetNextWindowFocus();
		bFocusRequested = false;
	}

	ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(1100.0f), EditorUIScale::Px(650.0f)), ImGuiCond_FirstUseEver);
	// 루트 창이라 Level Editor나 다른 FBX 뷰어와 붙을 수 있고, 레벨 패널과는 섞이지 않는다
	ImGui::SetNextWindowClass(&FEditorUI::GetRootWindowClass());
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool bHostVisible = ImGui::Begin(WindowTitle.c_str(), &bIsOpen,
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();
	bool bAnyPanelFocused = bHostVisible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	// 이 에셋의 패널끼리만 붙는 도킹 공간. ID가 창 ID에서 나오므로 재임포트해도 배치가 유지된다
	const ImGuiID PanelDockId = ImGui::GetID("PanelDockSpace");
	ImGuiWindowClass PanelClass;
	PanelClass.ClassId = PanelDockId;
	PanelClass.DockingAllowUnclassed = false;
	if (!ImGui::DockBuilderGetNode(PanelDockId))
	{
		BuildDefaultPanelLayout(PanelDockId, ImGui::GetWindowSize(), TreePanelTitle.c_str(), ViewportPanelTitle.c_str(), DetailsPanelTitle.c_str());
	}
	// 탭이 가려져 Begin이 false여도 호출해야 패널의 도킹이 풀리지 않는다
	ImGui::DockSpace(PanelDockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None, &PanelClass);
	ImGui::End();

	const auto BeginPanel = [&](const FString& Title, ImGuiWindowFlags Flags)
	{
		ImGui::SetNextWindowClass(&PanelClass);
		const bool bPanelVisible = ImGui::Begin(Title.c_str(), nullptr, Flags | ImGuiWindowFlags_NoCollapse);
		bAnyPanelFocused |= bPanelVisible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		return bPanelVisible;
	};

	// 깊은 본 이름이 칸 끝에서 잘리지 않도록 가로로 스크롤할 수 있게 한다
	if (BeginPanel(TreePanelTitle, ImGuiWindowFlags_HorizontalScrollbar))
	{
		DrawSkeletonTree();
	}
	ImGui::End();

	// 휠은 줌에 쓰므로 스크롤을 끈다
	bIsVisible = BeginPanel(ViewportPanelTitle, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	if (bIsVisible)
	{
		DrawViewport();
	}
	ImGui::End();

	if (BeginPanel(DetailsPanelTitle, ImGuiWindowFlags_None))
	{
		DrawDetails();
	}
	ImGui::End();

	bIsFocused = bAnyPanelFocused;

	// 레벨 에디터(FEditorControlsPanel)와 같이 Space로 이동 → 회전 → 크기 순환
	// 트리에서 본을 고른 직후에도 되도록 뷰포트 hover가 아니라 패널 중 하나가 포커스일 때 받는다
	if (bIsFocused && !ImGui::GetIO().WantTextInput
		&& ImGui::IsKeyPressed(ImGuiKey_Space, false) && !BoneGizmo.IsUsing())
	{
		const int NextMode = (static_cast<int>(BoneGizmo.GetMode()) + 1) % 3;
		BoneGizmo.SetMode(static_cast<EGizmoMode>(NextMode));
	}
}

void FSkeletalMeshEditor::DrawSkeletonTree()
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

void FSkeletalMeshEditor::DrawBoneTreeNode(int32 BoneIndex)
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
	UTexture2D* Icon = BoneHasWeights[BoneIndex] != 0 ? BoneIcon : BoneNonWeightedIcon;
	if (IsIconReady(Icon))
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

// B8에서 Transform 편집으로 바꾼다.
void FSkeletalMeshEditor::DrawDetails()
{
	ImGui::TextUnformatted("Details");
	ImGui::Separator();

	if (SelectedBoneIndex < 0)
	{
		ImGui::TextDisabled("No bone selected");
		return;
	}

	const FReferenceSkeleton& RefSkeleton = Mesh->RefSkeleton;
	const FMeshBoneInfo& Bone = RefSkeleton.RefBoneInfo[SelectedBoneIndex];
	const FString ParentName = Bone.ParentIndex >= 0 ? RefSkeleton.RefBoneInfo[Bone.ParentIndex].Name : FString("None");
	ImGui::Text("Name   : %s", Bone.Name.c_str());
	ImGui::Text("Parent : %s", ParentName.c_str());

	ImGui::SeparatorText("Bone");
	DrawBoneTransform();
	ImGui::SeparatorText("Reference");
	DrawReferenceTransform();
	ImGui::Spacing();

	// 언리얼 Persona의 "본 트랜스폼 리셋"처럼 Bind Pose로 되돌린다
	if (ImGui::Button("Reset Bone"))
	{
		SetBoneSpaceTransform(SelectedBoneIndex, RefSkeleton.RefBonePose[SelectedBoneIndex]);
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset All"))
	{
		PreviewScene->GetPreviewComponent()->ResetToRefPose();
	}
}

void FSkeletalMeshEditor::DrawBoneTransform()
{
	const int32 BoneIndex = SelectedBoneIndex;
	const FTransform& Reference = Mesh->RefSkeleton.RefBonePose[BoneIndex];
	// 컴포넌트 값의 사본. 칸을 고치면 바로 컴포넌트에 쓰고, World로 고쳤으면 다시 읽어 다음 칸이 새 값을 쓰게 한다
	FTransform Local = GetBoneSpaceTransforms()[BoneIndex];

	if (!BeginTransformTable("##BoneTransform"))
	{
		return;
	}

	// 칸 옆 아이콘으로 고른 공간. World는 Component 공간 값
	const FTransformRowIcons Icons{ GizmoLocalIcon, GizmoWorldIcon, ResetIcon };

	// 칸마다 바뀌면 바로 다시 누적한다. 다음 칸의 World 값이 새 포즈를 쓰도록
	// 화살표는 언리얼 DiffersFromDefault처럼 Local 값을 Reference와 비교한다
	FVector Location = bLocalLocation ? Local.Location : GetBoneLocation(BoneIndex);
	switch (DrawTransformRow("Location", &bLocalLocation, Location.V, 0.001f, !IsNearlyEqual(Local.Location, Reference.Location), Icons))
	{
	case ETransformRowResult::Changed:
		if (bLocalLocation)
		{
			Local.Location = Location;
			SetBoneSpaceTransform(BoneIndex, Local);
		}
		else
		{
			SetBoneComponentLocation(BoneIndex, Location);
			Local = GetBoneSpaceTransforms()[BoneIndex];
		}
		break;
	case ETransformRowResult::Reset:
		Local.Location = Reference.Location;
		SetBoneSpaceTransform(BoneIndex, Local);
		break;
	default:
		break;
	}

	// 레벨 Details와 같은 X / Y / Z(Roll / Pitch / Yaw) 순서
	const FRotator Rotation = bLocalRotation ? Local.Rotation.ToFRotator() : GetBoneRotation(BoneIndex);
	float RotationValues[3] = { Rotation.Roll, Rotation.Pitch, Rotation.Yaw };
	switch (DrawTransformRow("Rotation", &bLocalRotation, RotationValues, 0.1f, !IsSameRotation(Local.Rotation.ToFRotator(), Reference.Rotation.ToFRotator()), Icons))
	{
	case ETransformRowResult::Changed:
	{
		const FRotator NewRotation(RotationValues[1], RotationValues[2], RotationValues[0]);
		if (bLocalRotation)
		{
			Local.Rotation = NewRotation;
			SetBoneSpaceTransform(BoneIndex, Local);
		}
		else
		{
			SetBoneComponentRotation(BoneIndex, NewRotation);
			Local = GetBoneSpaceTransforms()[BoneIndex];
		}
		break;
	}
	case ETransformRowResult::Reset:
		Local.Rotation = Reference.Rotation;
		SetBoneSpaceTransform(BoneIndex, Local);
		break;
	default:
		break;
	}

	// World 스케일은 바뀐 양만 Local에 더한다 (언리얼 UBoneProxy와 같음)
	const FVector DisplayedScale = bLocalScale ? Local.Scale : GetBoneScale(BoneIndex);
	FVector Scale = DisplayedScale;
	switch (DrawTransformRow("Scale", &bLocalScale, Scale.V, 0.01f, !IsNearlyEqual(Local.Scale, Reference.Scale), Icons))
	{
	case ETransformRowResult::Changed:
		if (bLocalScale)
		{
			Local.Scale = Scale;
		}
		else
		{
			Local.Scale = Local.Scale + (Scale - DisplayedScale);
		}
		SetBoneSpaceTransform(BoneIndex, Local);
		break;
	case ETransformRowResult::Reset:
		Local.Scale = Reference.Scale;
		SetBoneSpaceTransform(BoneIndex, Local);
		break;
	default:
		break;
	}

	ImGui::EndTable();
}

void FSkeletalMeshEditor::DrawReferenceTransform()
{
	const FTransform& Reference = Mesh->RefSkeleton.RefBonePose[SelectedBoneIndex];
	if (!BeginTransformTable("##ReferenceTransform"))
	{
		return;
	}

	// 에셋 값이라 고칠 수 없다. 공간 토글 / Reset 아이콘 없음
	ImGui::BeginDisabled();
	FVector Location = Reference.Location;
	DrawTransformRow("Location", nullptr, Location.V, 0.0f, false, {});
	const FRotator ReferenceRotation = Reference.Rotation.ToFRotator();
	float RotationValues[3] = { ReferenceRotation.Roll, ReferenceRotation.Pitch, ReferenceRotation.Yaw };
	DrawTransformRow("Rotation", nullptr, RotationValues, 0.0f, false, {});
	FVector Scale = Reference.Scale;
	DrawTransformRow("Scale", nullptr, Scale.V, 0.0f, false, {});
	ImGui::EndDisabled();

	ImGui::EndTable();
}