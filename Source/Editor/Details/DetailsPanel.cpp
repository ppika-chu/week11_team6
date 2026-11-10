#include "EnginePCH.h"
#include "Editor/Details/DetailsPanel.h"

#include "imgui_internal.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Editor/HitoriEd/EditorDragDrop.h"
#include "Editor/HitoriEd/EditorFileUtils.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/ParticleSubUVComponent.h"
#include "Components/DecalComponent.h"
#include "Components/LightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ScriptComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraModifier_CameraShake.h"
#include "Camera/PlayerCameraManager.h"

#include "Components/RotatingMovementComponent.h"
#include "Components/ProjectileMovementComponent.h"
#include "Asset/AssetManager.h"
#include "Render/Material.h"
#include "Render/Texture2D.h"
#include "Render/Renderer.h"
#include "Render/ShadowSettings.h"
#include "Text/Font.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/Actor.h"
#include "Core/EngineLog.h"
#include "Render/SkeletalMesh.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/LuaAnimInstance.h"
#include "Animation/Skeleton.h"
#include "Math/Color.h"
#include <algorithm>


namespace
{
bool DrawLinearColorEdit3(const char* Label, float* LinearRGB)
{
	float Display[3] = { FMath::LinearToSRGB(LinearRGB[0]), FMath::LinearToSRGB(LinearRGB[1]), FMath::LinearToSRGB(LinearRGB[2]) };
	const float Previous[3] = { Display[0], Display[1], Display[2] };
	if (!ImGui::ColorEdit3(Label, Display, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float)) return false;
	for (int32 I = 0; I < 3; ++I) if (Display[I] != Previous[I]) LinearRGB[I] = FMath::SRGBToLinear(Display[I]);
	return true;
}

// ImGui의 표시/입력 RGB는 sRGB, 엔진의 색상 파라미터는 선형이다.
bool DrawLinearColorEdit4(const char* Label, float* LinearRGBA, ImGuiColorEditFlags Flags = ImGuiColorEditFlags_None)
{
	float DisplayColor[4] = { FMath::LinearToSRGB(LinearRGBA[0]), FMath::LinearToSRGB(LinearRGBA[1]),
		FMath::LinearToSRGB(LinearRGBA[2]), LinearRGBA[3] };
	const float PreviousRGB[3] = { DisplayColor[0], DisplayColor[1], DisplayColor[2] };
	if (!ImGui::ColorEdit4(Label, DisplayColor, Flags))
	{
		return false;
	}
	for (int32 Channel = 0; Channel < 3; ++Channel)
	{
		if (DisplayColor[Channel] != PreviousRGB[Channel])
		{
			LinearRGBA[Channel] = FMath::SRGBToLinear(DisplayColor[Channel]);
		}
	}
	LinearRGBA[3] = DisplayColor[3];
	return true;
}

bool DrawAxisControl(const FString& _label, float& _value, float _speed, float _minValue, float _maxValue, float _resetValue, FVector4 _color)
{
	bool isValueChanged = false;

	float lineHeight = ImGui::GetFrameHeight();
	ImVec4 color(_color.X, _color.Y, _color.Z, _color.W);
	ImVec2 buttonSize = { lineHeight + 2.0f, lineHeight };

	// 각 버튼의 색상 설정
	ImGui::PushStyleColor(ImGuiCol_Button, color);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ color.x + 0.1f, color.y + 0.1f, color.z + 0.1f, 1.0f });
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ color.x - 0.1f, color.y - 0.1f, color.z - 0.1f, 1.0f });

	// 굵은 폰트 적용
	if (ImGui::Button(_label.c_str(), buttonSize))
	{
		_value = _resetValue;
		isValueChanged = true;
	}
	ImGui::PopStyleColor(3);

	// 드래그 슬라이더
	ImGui::SameLine();
	std::string dragID = "##" + _label;
	isValueChanged |= ImGui::DragFloat(dragID.c_str(), &_value, _speed, _minValue, _maxValue, "%.2f");

	return isValueChanged;
}

bool DrawVector3Controller(const FString& _label, float* _values, float _resetValue)
{
	ImGuiIO& io = ImGui::GetIO();
	auto boldFont = io.Fonts->Fonts[0];

	// 저장된 열 폭이 있으면 초기 폭을 무시하므로 저장하지 않는다.
	ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings;

	bool isVectorChanged = false;

	ImGui::PushID(_label.c_str());
	if (ImGui::BeginTable(_label.c_str(), 2, flags)) // 고유 ID, 열 2개, 플래그
	{
		// ImGuiTableColumnFlags_WidthFixed: 초기 너비 고정
		// ImGuiTableColumnFlags_WidthStretch: 창 크기에 따라 너비 조절 (기본값)
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Location").x);
		ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(_label.c_str()); // 왼쪽 열: 레이블

		ImGui::TableSetColumnIndex(1);
		// [컨트롤러 UI 코드] // 오른쪽 열: 컨트롤러
		// 축 버튼 3개와 그 사이 간격(2px 5칸)을 뺀 폭을 입력칸 3개가 나눠 쓴다.
		const float AxisButtonWidth = ImGui::GetFrameHeight() + 2.0f;
		ImGui::PushMultiItemsWidths(3, ImGui::GetContentRegionAvail().x - AxisButtonWidth * 3.0f - 2.0f * 5.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 2, 0 });
		ImGui::PushFont(boldFont);

		isVectorChanged |= DrawAxisControl("X", _values[0], 0.1f, 0.0f, 0.0f, _resetValue, { 0.8f, 0.1f, 0.1f, 1.0f });

		ImGui::PopItemWidth();

		ImGui::SameLine();
		isVectorChanged |= DrawAxisControl("Y", _values[1], 0.1f, 0.0f, 0.0f, _resetValue, { 0.1f, 0.8f, 0.1f, 1.0f });

		ImGui::PopItemWidth();

		ImGui::SameLine();
		isVectorChanged |= DrawAxisControl("Z", _values[2], 0.1f, 0.0f, 0.0f, _resetValue, { 0.1f, 0.1f, 0.8f, 1.0f });

		ImGui::PopItemWidth();

		ImGui::PopFont();
		ImGui::PopStyleVar();
		ImGui::EndTable();
	}

	ImGui::PopID();

	return isVectorChanged;
}

// FRotator의 V는 (Pitch, Yaw, Roll) 순서지만 엔진은 Roll=X, Pitch=Y, Yaw=Z축이므로
// X/Y/Z 칸이 실제 회전축과 맞도록 재배열해 편집한다.
bool DrawRotatorAsXYZ(const FString& _label, FRotator& _rotator)
{
	float Euler[3] = { _rotator.Roll, _rotator.Pitch, _rotator.Yaw };
	const bool isChanged = DrawVector3Controller(_label, Euler, 0.0f);
	if (isChanged)
	{
		_rotator.Roll = Euler[0];
		_rotator.Pitch = Euler[1];
		_rotator.Yaw = Euler[2];
	}
	return isChanged;
}

FString GetAssetDisplayName(const FString& Path)
{
	fs::path AssetPath(Path);
	if (AssetPath.stem() == "Atlas")
	{
		return AssetPath.parent_path().filename().string();
	}
	return AssetPath.stem().string();
}

// 머티리얼 참조(UMaterial*)의 텍스처 슬롯 하나를 바꾼다. 드래그 앤 드롭과 드롭다운이 같이 쓴다.
void SetTextureOnMaterialRef(UMaterial** MaterialPtr, int32 SlotIndex, UTexture2D* Texture)
{
	UMaterial* Material = *MaterialPtr;

	// 머티리얼이 없으면 기본 머티리얼에서 시작한다
	if (!Material)
	{
		Material = UMaterial::CreateInstance(UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial"));
		*MaterialPtr = Material;
	}
	// 공유 에셋이면 지금 복제한다.
	// 이 시점까지 미루면 손대지 않은 오브젝트는 계속 공유 에셋을 쓴다.
	else if (!Material->bIsInstance || !Material->GetPath().empty())
	{
		Material = UMaterial::CreateInstance(Material);
		*MaterialPtr = Material;
	}

	if (!Material)
	{
		return;
	}

	// 슬롯이 배열 범위 밖이면 그 자리까지 늘려 준다
	while (static_cast<int32>(Material->Textures.Num()) <= SlotIndex)
	{
		Material->Textures.Add(nullptr);
	}
	Material->Textures[SlotIndex] = Texture;
}

void DrawTextureSlot(UMaterial** MaterialPtr, int32 SlotIndex, const char* SlotLabel)
{
	const float ThumbnailSize = EditorUIScale::Px(64.0f);
	const float ItemThumbnailSize = EditorUIScale::Px(24.0f);

	UMaterial* Material = *MaterialPtr;
	UTexture2D* Texture = nullptr;
	if (Material && SlotIndex < static_cast<int32>(Material->Textures.Num()))
	{
		Texture = Material->Textures[SlotIndex];
	}

	ImGui::PushID(SlotIndex);

	if (Texture && Texture->GetResource())
	{
		ImGui::Image(
			(ImTextureID)Texture->GetResource()->GetSRV(),
			{ ThumbnailSize, ThumbnailSize });
	}
	else
	{
		ImGui::Button("Empty", { ThumbnailSize, ThumbnailSize });
	}

	// 바로 위 위젯이 드롭을 받는다
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Texture))
		{
			UTexture2D* DroppedTexture = *static_cast<UTexture2D**>(Payload->Data);
			SetTextureOnMaterialRef(MaterialPtr, SlotIndex, DroppedTexture);
			Texture = DroppedTexture;
		}
		ImGui::EndDragDropTarget();
	}

	ImGui::SameLine();

	ImGui::BeginGroup();
	ImGui::TextUnformatted(SlotLabel);

	// 메시 머티리얼 탭(DrawMaterialTextureSlot)과 같은 텍스처 드롭다운
	const FString CurrentPath = Texture ? Texture->GetPath() : "";
	const FString CurrentFileName = Texture ? GetAssetDisplayName(CurrentPath) : "None";
	ImGui::SetNextItemWidth(-1.0f);
	EditorUIScale::LimitNextComboPopupWidth();
	const bool bOpen = ImGui::BeginCombo("##Texture", CurrentFileName.c_str());
	if (ImGui::IsItemHovered() && Texture)
	{
		ImGui::SetTooltip("%s", CurrentPath.c_str());
	}

	if (bOpen)
	{
		if (ImGui::Selectable("None", Texture == nullptr) && Texture)
		{
			SetTextureOnMaterialRef(MaterialPtr, SlotIndex, nullptr);
		}

		for (TObjectIterator<UTexture2D> Iterator; Iterator; ++Iterator)
		{
			UTexture2D* ItemTexture = *Iterator;
			if (ItemTexture->IsRetired() || !ItemTexture->GetResource() || ItemTexture->GetPath().empty())
			{
				continue;
			}

			const bool bSelected = ItemTexture == Texture;
			const FString ItemPath = ItemTexture->GetPath();
			ImGui::PushID(ItemTexture);
			ImGui::Image((ImTextureID)ItemTexture->GetResource()->GetSRV(), { ItemThumbnailSize, ItemThumbnailSize });
			ImGui::SameLine();
			if (ImGui::Selectable(GetAssetDisplayName(ItemPath).c_str(), bSelected, ImGuiSelectableFlags_None, { 0.0f, ItemThumbnailSize }) && !bSelected)
			{
				SetTextureOnMaterialRef(MaterialPtr, SlotIndex, ItemTexture);
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", ItemPath.c_str());
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	ImGui::EndGroup();

	ImGui::PopID();
}

void DrawFontSlot(UObject* Owner, UFont** FontPtr)
{
	UFont* Current = *FontPtr;

	FString CurrentPath = Current ? Current->GetPath() : "";
	FString CurrentFileName = Current ? GetAssetDisplayName(CurrentPath) : "None";

	ImGui::SetNextItemWidth(-1.0f);

	EditorUIScale::LimitNextComboPopupWidth();
	bool bOpen = ImGui::BeginCombo("##Font", CurrentFileName.c_str());
	if (ImGui::IsItemHovered() && Current)
	{
		ImGui::SetTooltip("%s", CurrentPath.c_str());
	}

	if (bOpen)
	{
		for (TObjectIterator<UFont> Itr; Itr; ++Itr)
		{
			if (Itr->IsRetired()) continue;
			bool bSelected = (*Itr == Current);

			FString ItemPath = Itr->GetPath();
			FString ItemFileName = GetAssetDisplayName(ItemPath);

			ImGui::PushID(*Itr);

			const bool bClicked = ImGui::Selectable(ItemFileName.c_str(), bSelected);
			// 클릭한 프레임이 아니라 올려 둔 동안 계속 경로를 보여준다
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", ItemPath.c_str());
			}

			if (bClicked)
			{
				// 메모리를 직접 바꾸지 않고 SetFont를 거친다
				if (UTextRenderComponent* TextComponent = Cast<UTextRenderComponent>(Owner))
				{
					TextComponent->SetFont(*Itr);
				}
				else
				{
					*FontPtr = *Itr;
				}
			}
			ImGui::PopID();
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
}

void DrawMeshSlot(UObject* Owner, UStaticMesh** StaticMeshPtr)
{
	UStaticMesh* Current = *StaticMeshPtr;

	FString CurrentPath = Current ? Current->GetPath() : "";
	FString CurrentFileName = Current ? GetAssetDisplayName(CurrentPath) : "None";

	ImGui::SetNextItemWidth(-1.0f);

	EditorUIScale::LimitNextComboPopupWidth();
	bool bOpen = ImGui::BeginCombo("##StaticMesh", CurrentFileName.c_str());
	if (ImGui::IsItemHovered() && Current)
	{
		ImGui::SetTooltip("%s", CurrentPath.c_str());
	}

	if (bOpen)
	{
		struct FMeshItem
		{
			FString DisplayName;
			FString Path;
			UStaticMesh* Mesh;
		};

		// 객체 생성 순서가 아니라 표시 이름의 알파벳 순(대소문자 무시)으로 보여준다.
		TArray<FMeshItem> Items;
		for (TObjectIterator<UStaticMesh> Itr; Itr; ++Itr)
		{
			// 기즈모·빌보드용 내부 메시는 고를 수 없게 숨긴다.
			if (Itr->bInternalOnly || Itr->IsRetired())
			{
				continue;
			}
			FString ItemPath = Itr->GetPath();
			Items.Add({ GetAssetDisplayName(ItemPath), ItemPath, *Itr });
		}
		std::sort(Items.begin(), Items.end(), [](const FMeshItem& A, const FMeshItem& B)
			{ return _stricmp(A.DisplayName.c_str(), B.DisplayName.c_str()) < 0; });

		for (const FMeshItem& Item : Items)
		{
			bool bSelected = (Item.Mesh == Current);

			ImGui::PushID(Item.Mesh);

			const bool bClicked = ImGui::Selectable(Item.DisplayName.c_str(), bSelected);
			// 클릭한 프레임이 아니라 올려 둔 동안 계속 경로를 보여준다
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", Item.Path.c_str());
			}

			if (bClicked)
			{
				// 메모리를 직접 바꾸지 않고 SetStaticMesh를 거친다
				if (UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Owner))
				{
					MeshComponent->SetStaticMesh(Item.Mesh);
				}
				else
				{
					*StaticMeshPtr = Item.Mesh;
				}
			}
			ImGui::PopID();
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
}

void DrawSkeletalMeshSlot(UObject* Owner, USkeletalMesh** SkeletalMeshPtr)
{
	USkeletalMesh* Current = *SkeletalMeshPtr;

	FString CurrentPath = Current ? Current->GetPath() : "";
	FString CurrentFileName = Current ? GetAssetDisplayName(CurrentPath) : "None";

	ImGui::SetNextItemWidth(-1.0f);

	EditorUIScale::LimitNextComboPopupWidth();
	bool bOpen = ImGui::BeginCombo("##SkeletalMesh", CurrentFileName.c_str());
	if (ImGui::IsItemHovered() && Current)
	{
		ImGui::SetTooltip("%s", CurrentPath.c_str());
	}

	if (bOpen)
	{
		struct FMeshItem
		{
			FString DisplayName;
			FString Path;
			USkeletalMesh* Mesh;
		};

		// 객체 생성 순서가 아니라 표시 이름의 알파벳 순(대소문자 무시)으로 보여준다.
		TArray<FMeshItem> Items;
		for (TObjectIterator<USkeletalMesh> Itr; Itr; ++Itr)
		{
			FString ItemPath = Itr->GetPath();
			Items.Add({ GetAssetDisplayName(ItemPath), ItemPath, *Itr });
		}
		std::sort(Items.begin(), Items.end(), [](const FMeshItem& A, const FMeshItem& B)
			{ return _stricmp(A.DisplayName.c_str(), B.DisplayName.c_str()) < 0; });

		for (const FMeshItem& Item : Items)
		{
			bool bSelected = (Item.Mesh == Current);

			ImGui::PushID(Item.Mesh);

			const bool bClicked = ImGui::Selectable(Item.DisplayName.c_str(), bSelected);
			// 클릭한 프레임이 아니라 올려 둔 동안 계속 경로를 보여준다
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", Item.Path.c_str());
			}

			if (bClicked)
			{
				// 메모리를 직접 바꾸지 않고 SetStaticMesh를 거친다
				if (USkeletalMeshComponent* MeshComponent = Cast<USkeletalMeshComponent>(Owner))
				{
					MeshComponent->SetSkeletalMesh(Item.Mesh);
				}
				else
				{
					*SkeletalMeshPtr = Item.Mesh;
				}
			}
			ImGui::PopID();
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
}

// 스켈레탈 메시 컴포넌트면 메시와 스켈레톤이 같은 애니메이션만 보여 준다 (UE Anim to Play 선택 칸의 스켈레톤 필터)
bool DrawAnimationAssetSlot(UObject* Owner, UAnimationAsset** AnimationPtr)
{
	UAnimationAsset* Current = *AnimationPtr;
	const FString CurrentPath = Current ? Current->GetPath() : "";
	const FString CurrentFileName = Current ? GetAssetDisplayName(CurrentPath) : "None";

	const USkeletalMeshComponent* MeshComponent = Cast<USkeletalMeshComponent>(Owner);
	const USkeletalMesh* Mesh = MeshComponent ? MeshComponent->GetSkeletalMesh() : nullptr;
	const USkeleton* RequiredSkeleton = Mesh ? Mesh->Skeleton : nullptr;

	ImGui::SetNextItemWidth(-1.0f);
	EditorUIScale::LimitNextComboPopupWidth();
	const bool bOpen = ImGui::BeginCombo("##AnimationAsset", CurrentFileName.c_str());
	if (ImGui::IsItemHovered() && Current)
	{
		ImGui::SetTooltip("%s", CurrentPath.c_str());
	}
	if (!bOpen)
	{
		return false;
	}

	bool bChanged = false;
	if (ImGui::Selectable("None", Current == nullptr) && Current)
	{
		*AnimationPtr = nullptr;
		bChanged = true;
	}

	struct FAnimationItem
	{
		FString DisplayName;
		FString Path;
		UAnimationAsset* Animation;
	};
	TArray<FAnimationItem> Items;
	for (TObjectIterator<UAnimationAsset> Itr; Itr; ++Itr)
	{
		// 스켈레톤이 다르면 SingleNodeInstance가 받지 않으므로 목록에서 뺀다
		if (Itr->IsRetired() || (MeshComponent && Itr->GetSkeleton() != RequiredSkeleton))
		{
			continue;
		}
		const FString ItemPath = Itr->GetPath();
		Items.Add({ GetAssetDisplayName(ItemPath), ItemPath, *Itr });
	}
	std::sort(Items.begin(), Items.end(), [](const FAnimationItem& A, const FAnimationItem& B)
		{ return _stricmp(A.DisplayName.c_str(), B.DisplayName.c_str()) < 0; });

	for (const FAnimationItem& Item : Items)
	{
		const bool bSelected = Item.Animation == Current;
		ImGui::PushID(Item.Animation);
		const bool bClicked = ImGui::Selectable(Item.DisplayName.c_str(), bSelected);
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%s", Item.Path.c_str());
		}
		if (bClicked && !bSelected)
		{
			*AnimationPtr = Item.Animation;
			bChanged = true;
		}
		ImGui::PopID();
		if (bSelected)
		{
			ImGui::SetItemDefaultFocus();
		}
	}
	ImGui::EndCombo();
	return bChanged;
}

// 탐색기나 드래그로 고른 파일을 적용한다. 스크립트는 Assets/Scripts 기준으로 저장되므로 밖의 파일은 받지 않는다.
bool AssignScriptFile(UScriptComponent& ScriptComponent, const std::filesystem::path& SelectedFile)
{
	const std::filesystem::path ScriptRoot = "Assets/Scripts";

	std::error_code ErrorCode;
	const std::filesystem::path AbsoluteRoot = std::filesystem::weakly_canonical(ScriptRoot, ErrorCode);
	const std::filesystem::path AbsoluteFile = std::filesystem::weakly_canonical(SelectedFile, ErrorCode);
	const std::filesystem::path RelativePath = AbsoluteFile.lexically_relative(AbsoluteRoot);

	if (ErrorCode || RelativePath.empty() || *RelativePath.begin() == "..")
	{
		HTR_LOG(Warning, "[Lua] Script must be inside {}: {}", ScriptRoot.generic_string(), SelectedFile.generic_string());
		return false;
	}

	return ScriptComponent.SetScriptPath((ScriptRoot / RelativePath).generic_string());
}

void DrawScriptComponentActions(UScriptComponent& ScriptComponent)
{
	static UScriptComponent* EditingComponent = nullptr;
	static char FileNameBuffer[256] = {};

	// 확장자가 자동으로 붙거나 파일을 새로 고를 수 있으므로 적용된 파일명으로 다시 맞춘다.
	const auto SyncFileName = [&ScriptComponent]()
	{
		const FString AppliedFileName = std::filesystem::path(ScriptComponent.GetScriptPath()).filename().string();
		strncpy_s(FileNameBuffer, sizeof(FileNameBuffer), AppliedFileName.c_str(), _TRUNCATE);
	};

	// 다른 ScriptComponent를 선택했을 때만 버퍼 초기화
	if (EditingComponent != &ScriptComponent)
	{
		EditingComponent = &ScriptComponent;
		const FString FileName = std::filesystem::path(
			ScriptComponent.GetScriptPath())
									 .filename()
									 .string();

		strncpy_s(FileNameBuffer, sizeof(FileNameBuffer), FileName.c_str(), _TRUNCATE);
	}

	ImGui::SeparatorText("Lua Script");

	const FString& CurrentPath = ScriptComponent.GetScriptPath();

	if (!CurrentPath.empty())
	{
		ImGui::TextWrapped("File: %s", CurrentPath.c_str());
	}
	else
	{
		ImGui::TextDisabled("No script assigned.");
	}

	// Enter를 눌렀을 때만 true
	const float ButtonsWidth = EditorUIScale::ButtonWidth("Apply") + EditorUIScale::ButtonWidth("Browse...") + ImGui::GetStyle().ItemSpacing.x * 2.0f;
	ImGui::SetNextItemWidth(-ButtonsWidth);

	const bool bEnterPressed = ImGui::InputText(
		"##ScriptFileName",
		FileNameBuffer,
		sizeof(FileNameBuffer),
		ImGuiInputTextFlags_EnterReturnsTrue);

	// Content Drawer에서 끌어 온 .lua 파일을 받는다.
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::ScriptFile))
		{
			const std::string Utf8Path = static_cast<const char*>(Payload->Data);
			if (AssignScriptFile(ScriptComponent, std::u8string(Utf8Path.begin(), Utf8Path.end())))
			{
				SyncFileName();
			}
		}
		ImGui::EndDragDropTarget();
	}
	if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
	{
		ImGui::SetTooltip("Type a file name, or drag a .lua file from the Content Drawer.");
	}

	ImGui::SameLine();

	const bool bApplyClicked = ImGui::Button("Apply");

	ImGui::SameLine();

	if (ImGui::Button("Browse..."))
	{
		const FString SelectedFile = FEditorFileUtils::OpenScriptDialog(std::filesystem::absolute("Assets/Scripts").string());
		if (!SelectedFile.empty() && AssignScriptFile(ScriptComponent, SelectedFile))
		{
			SyncFileName();
		}
	}

	if (bEnterPressed || bApplyClicked)
	{
		if (ScriptComponent.SetScriptPath(FileNameBuffer))
		{
			SyncFileName();
		}
	}

	if (!ImGui::Button("Edit Script", ImVec2(-FLT_MIN, 0.0f)))
	{
		return;
	}

	// 아직 파일이 없다면 template.lua로 생성한다.
	if (!ScriptComponent.EnsureScriptFile())
	{
		HTR_LOG(Error, "[Lua] Failed to prepare script file");
		return;
	}

	std::error_code ErrorCode;

	const std::filesystem::path AbsolutePath = std::filesystem::absolute(
		ScriptComponent.GetScriptPath(), ErrorCode)
												   .lexically_normal();

	if (ErrorCode)
	{
		HTR_LOG(Error, "[Lua] Failed to resolve script path '{}': {}",
			ScriptComponent.GetScriptPath(), ErrorCode.message());
		return;
	}

	const FString PathString = AbsolutePath.string();

	ImGuiPlatformIO& PlatformIO = ImGui::GetPlatformIO();

	if (!PlatformIO.Platform_OpenInShellFn)
	{
		HTR_LOG(Error, "[Lua] Open-in-shell function is unavailable");
		return;
	}

	const bool bOpened = PlatformIO.Platform_OpenInShellFn(
		ImGui::GetCurrentContext(), PathString.c_str());

	if (!bOpened)
	{
		HTR_LOG(Error, "[Lua] Failed to open script '{}'", PathString);
	}
}


UMaterial* EnsureMaterialOverride(UMeshComponent* MeshComponent, int32 Slot, UMaterial* Effective, UMaterial* Override)
{
	// 이 컴포넌트 전용 인스턴스가 아니면 지금 적용 중인 머티리얼을 복제해서 덮어쓰기로 만든다.
	// 메시의 머티리얼은 같은 메시를 쓰는 모든 액터가 공유하므로 직접 고치면 안 된다.
	if (Override && Override->bIsInstance && Override->GetPath().empty())
	{
		return Override;
	}

	UMaterial* Source = Effective ? Effective : UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");

	Override = UMaterial::CreateInstance(Source);
	MeshComponent->SetMaterial(Slot, Override);

	return Override;
}

bool SetMaterialTexture(UMeshComponent* MeshComponent, int32 MaterialSlot, int32 TextureSlot, UTexture2D* Texture, UMaterial*& Effective, UMaterial*& Override)
{
	UTexture2D* Current{ Effective && Effective->Textures.IsValidIndex(TextureSlot) ? Effective->Textures[TextureSlot] : nullptr };
	if (Current == Texture)
	{
		return false;
	}

	Override = EnsureMaterialOverride(MeshComponent, MaterialSlot, Effective, Override);
	if (!Override)
	{
		return false;
	}

	while (!Override->Textures.IsValidIndex(TextureSlot))
	{
		Override->Textures.Add(nullptr);
	}
	Override->Textures[TextureSlot] = Texture;
	Effective = Override;
	return true;
}

void DrawMaterialTextureSlot(UMeshComponent* MeshComponent, int32 MaterialSlot, int32 TextureSlot, const char* Label, UMaterial*& Effective, UMaterial*& Override)
{
	const float ThumbnailSize{ EditorUIScale::Px(64.0f) };
	const float ItemThumbnailSize{ EditorUIScale::Px(24.0f) };
	UTexture2D* Texture{ Effective && Effective->Textures.IsValidIndex(TextureSlot) ? Effective->Textures[TextureSlot] : nullptr };

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::TextUnformatted(Label);
	ImGui::TableSetColumnIndex(1);
	ImGui::PushID(TextureSlot);

	if (Texture && Texture->GetResource())
	{
		ImGui::Image((ImTextureID)Texture->GetResource()->GetSRV(), { ThumbnailSize, ThumbnailSize });
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%s", Texture->GetPath().c_str());
		}
	}
	else
	{
		ImGui::Button("Empty", { ThumbnailSize, ThumbnailSize });
	}

	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload{ ImGui::AcceptDragDropPayload(EditorDragDrop::Texture) })
		{
			UTexture2D* DroppedTexture{ *static_cast<UTexture2D**>(Payload->Data) };
			if (SetMaterialTexture(MeshComponent, MaterialSlot, TextureSlot, DroppedTexture, Effective, Override))
			{
				Texture = DroppedTexture;
			}
		}
		ImGui::EndDragDropTarget();
	}

	const FString CurrentPath{ Texture ? Texture->GetPath() : "" };
	const FString CurrentFileName{ Texture ? GetAssetDisplayName(CurrentPath) : "None" };
	ImGui::SetNextItemWidth(-1.0f);
	EditorUIScale::LimitNextComboPopupWidth();
	const bool Open{ ImGui::BeginCombo("##Texture", CurrentFileName.c_str()) };
	if (ImGui::IsItemHovered() && Texture)
	{
		ImGui::SetTooltip("%s", CurrentPath.c_str());
	}

	if (Open)
	{
		if (ImGui::Selectable("None", Texture == nullptr))
		{
			SetMaterialTexture(MeshComponent, MaterialSlot, TextureSlot, nullptr, Effective, Override);
		}

		for (TObjectIterator<UTexture2D> Iterator{}; Iterator; ++Iterator)
		{
			UTexture2D* ItemTexture{ *Iterator };
			if (ItemTexture->IsRetired() || !ItemTexture->GetResource() || ItemTexture->GetPath().empty())
			{
				continue;
			}

			const bool Selected{ ItemTexture == Texture };
			const FString ItemPath{ ItemTexture->GetPath() };
			const FString ItemFileName{ GetAssetDisplayName(ItemPath) };
			ImGui::PushID(ItemTexture);
			ImGui::Image((ImTextureID)ItemTexture->GetResource()->GetSRV(), { ItemThumbnailSize, ItemThumbnailSize });
			ImGui::SameLine();
			if (ImGui::Selectable(ItemFileName.c_str(), Selected, ImGuiSelectableFlags_None, { 0.0f, ItemThumbnailSize }))
			{
				SetMaterialTexture(MeshComponent, MaterialSlot, TextureSlot, ItemTexture, Effective, Override);
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", ItemPath.c_str());
			}
			if (Selected)
			{
				ImGui::SetItemDefaultFocus();
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}

	ImGui::PopID();
}

// 메시 컴포넌트의 머티리얼 슬롯을 한 줄씩 그린다.
// 텍스처를 드롭하면 그 슬롯의 덮어쓰기에 반영하고, 메시 에셋의 머티리얼은 건드리지 않는다.
void DrawMaterialSlots(UMeshComponent* MeshComponent)
{
	const int32 NumSlots = MeshComponent->GetNumMaterials();
	if (NumSlots <= 0)
	{
		return;
	}

	if (!ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen))
	{
		return;
	}

	// 메쉬 내의 매터리얼 별 Slot 순회
	for (int32 Slot = 0; Slot < NumSlots; ++Slot)
	{
		ImGui::PushID(Slot);

		FString SlotLabel = std::format("[{}] {}", Slot, MeshComponent->GetMaterialSlotName(Slot));
		ImGui::AlignTextToFramePadding(); // 같은 줄의 Reset 버튼과 높이를 맞춘다
		bool bOpen = ImGui::TreeNode(SlotLabel.c_str());

		UMaterial* Override = MeshComponent->GetOverrideMaterial(Slot);
		UMaterial* Effective = MeshComponent->GetMaterial(Slot);

		ImGui::SameLine();

		if (Override)
		{
			ImGui::TextDisabled("Override");
			ImGui::SameLine();

			if (ImGui::Button("Reset"))
			{
				MeshComponent->SetMaterial(Slot, nullptr);
				Override = nullptr;
				Effective = MeshComponent->GetMaterial(Slot);
			}
		}
		else
		{
			ImGui::TextDisabled("Default");
		}

		if (bOpen)
		{
			if (ImGui::BeginTable("MaterialProperties", 2))
			{
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("World Tile Space").x);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Shading Model");
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1.0f);
				const char* ShadingModels[] = { "Gouraud", "Lambert", "Blinn-Phong", "Unlit" };
				int ModelIndex = static_cast<int>(Effective ? Effective->ShadingModel : ELightingModel::BlinnPhong);
				if (ImGui::Combo("##ShadingModel", &ModelIndex, ShadingModels, IM_ARRAYSIZE(ShadingModels)))
				{
					Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
					if (Override)
					{
						Override->ShadingModel = static_cast<ELightingModel>(ModelIndex);
						Effective = Override;
					}
				}

				DrawMaterialTextureSlot(MeshComponent, Slot, 0, "Texture", Effective, Override);
				DrawMaterialTextureSlot(MeshComponent, Slot, 1, "Normal Map", Effective, Override);

				ImGui::TableNextRow();

				// 3: Diffuse (DiffuseOrFactor.rgb)
				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Diffuse");

				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1.0f);

				FVector4 TempColor = Effective ? Effective->DiffuseOrFactor : FVector4(1.0f, 1.0f, 1.0f, 1.0f);
				// 알파는 아래 Opacity 행에서 따로 편집한다. 저장은 DiffuseOrFactor.a 그대로다.
				if (DrawLinearColorEdit4("##DiffuseOrFactor", &TempColor.X, ImGuiColorEditFlags_NoAlpha))
				{
					Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
					if (Override)
					{
						Override->DiffuseOrFactor = TempColor;
						Effective = Override;
					}
				}

				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Opacity");

				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1.0f);

				// Opaque는 셰이더가 알파를 1로 고정하고, Additive는 알파를 색에 곱하지 않아 값을 바꿔도 변화가 없다.
				const bool bOpacityUsed = Effective && Effective->BlendState == EBlendState::AlphaBlend;
				float TempOpacity = Effective ? Effective->DiffuseOrFactor.W : 1.0f;
				ImGui::BeginDisabled(!bOpacityUsed);
				if (ImGui::SliderFloat("##Opacity", &TempOpacity, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
				{
					Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
					if (Override)
					{
						Override->DiffuseOrFactor.W = TempOpacity;
						Effective = Override;
					}
				}
				ImGui::EndDisabled();
				if (!bOpacityUsed && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				{
					ImGui::SetTooltip("Opacity applies only when Blend Mode is Alpha Blend.");
				}

				ImGui::TableNextRow();

				// Specular는 Blinn-Phong만 계산한다. 다른 모델에서는 값을 바꿔도 변화가 없다.
				const bool bSpecularUsed = !Effective || Effective->ShadingModel == ELightingModel::BlinnPhong;
				const char* SpecularHint = !bSpecularUsed ? "Specular applies only to the Blinn-Phong shading model."
					: (Effective && Effective->SpecularFactor <= 0.0f) ? "Specular Factor is 0, so no highlight is visible."
					: nullptr;
				const auto DrawSpecularHint = [SpecularHint]()
				{
					if (SpecularHint && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					{
						ImGui::SetTooltip("%s", SpecularHint);
					}
				};

				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Shininess");

				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1.0f);

				float TempShininess = Effective ? Effective->Shininess : 32.0f;
				ImGui::BeginDisabled(!bSpecularUsed);
				// MTL Ns와 FBX 변환값은 256을 넘을 수 있으므로 위쪽은 제한하지 않는다.
				const bool bShininessChanged = ImGui::DragFloat("##Shininess", &TempShininess, 0.5f, 1.0f, FLT_MAX);
				ImGui::EndDisabled();
				DrawSpecularHint();
				if (bShininessChanged)
				{
					Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
					if (Override)
					{
						Override->Shininess = TempShininess;
						Effective = Override;
					}
				}

				ImGui::TableNextRow();

				auto DrawMaterialColor = [&](const char* Label, const char* ID, FVector UMaterial::* Member, const FVector& Default)
				{
					ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(Label);
					ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1.f);
					FVector Value = Effective ? Effective->*Member : Default;
					if (DrawLinearColorEdit3(ID, &Value.X))
					{
						Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
						if (Override) { Override->*Member = Value; Effective = Override; }
					}
					if (Member == &UMaterial::SpecularColor) { DrawSpecularHint(); }
					ImGui::TableNextRow();
				};
				auto DrawMaterialFactor = [&](const char* Label, const char* ID, float UMaterial::* Member)
				{
					ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(Label);
					ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1.f);
					float Value = Effective ? Effective->*Member : 1.f;
					if (ImGui::DragFloat(ID, &Value, .01f))
					{
						Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
						if (Override) { Override->*Member = std::max(0.f, Value); Effective = Override; }
					}
					if (Member == &UMaterial::SpecularFactor) { DrawSpecularHint(); }
					ImGui::TableNextRow();
				};
				ImGui::BeginDisabled(!bSpecularUsed);
				DrawMaterialColor("Specular Color", "##SpecularColor", &UMaterial::SpecularColor, FVector(1, 1, 1));
				DrawMaterialFactor("Specular Factor", "##SpecularFactor", &UMaterial::SpecularFactor);
				ImGui::EndDisabled();
				DrawMaterialColor("Emissive Color", "##EmissiveColor", &UMaterial::EmissiveColor, FVector(0, 0, 0));
				DrawMaterialFactor("Emissive Factor", "##EmissiveFactor", &UMaterial::EmissiveFactor);

				// 4: UV Scroll Speed
				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::Text("UV Scroll");

				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1.0f);

				FVector2 TempUVScrollSpeed = Effective ? Effective->UVScrollSpeed : FVector2();
				if (ImGui::DragFloat2("##UVScrollSpeed", &TempUVScrollSpeed.X, 0.01f))
				{
					Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
					if (Override)
					{
						Override->UVScrollSpeed = TempUVScrollSpeed;
						Effective = Override;
					}
				}

				ImGui::TableNextRow();

				// UV Tiling: 메시 UV에 곱하는 반복 횟수. 반복해 보이려면 Sampler가 Linear Wrap이어야 한다.
				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::Text("UV Tiling");

				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1.0f);

				FVector2 TempUVTiling = Effective ? Effective->UVTiling : FVector2(1.0f, 1.0f);
				if (ImGui::DragFloat2("##UVTiling", &TempUVTiling.X, 0.05f, 0.01f, 1000.0f))
				{
					Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
					if (Override)
					{
						Override->UVTiling = TempUVTiling;
						Effective = Override;
					}
				}

				ImGui::TableNextRow();

				// World Tile Size: 0보다 크면 메시 UV 대신 월드 좌표로 이 크기마다 한 장씩 깐다.
				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::Text("World Tile");

				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1.0f);

				// (가로, 세로) 월드 크기. X가 0이면 꺼지고, Y가 0이면 X와 같은 정사각형으로 깐다.
				FVector2 TempWorldUVTileSize = Effective ? Effective->WorldUVTileSize : FVector2(0.0f, 0.0f);
				const char* WorldTileFormat = TempWorldUVTileSize.X > 0.0f ? "%.2f" : "Off";
				if (ImGui::DragFloat2("##WorldUVTileSize", &TempWorldUVTileSize.X, 0.05f, 0.0f, 1000.0f, WorldTileFormat))
				{
					Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
					if (Override)
					{
						Override->WorldUVTileSize = FVector2(std::max(TempWorldUVTileSize.X, 0.0f), std::max(TempWorldUVTileSize.Y, 0.0f));
						Effective = Override;
					}
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("텍스처 한 장이 덮는 월드 크기 (가로, 세로)\nX가 0이면 꺼짐, Y가 0이면 X와 같게");
				}

				ImGui::TableNextRow();

				// World Tile Space: 무늬를 월드 축에 고정할지, 메시 로컬 축을 따라 돌릴지
				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::Text("World Tile Space");

				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1.0f);

				const char* TileSpaceItems[] = { "World", "Local" };
				int TileSpaceIndex = (Effective && Effective->bWorldUVLocalSpace) ? 1 : 0;
				if (ImGui::BeginCombo("##WorldUVTileSpace", TileSpaceItems[TileSpaceIndex]))
				{
					for (int i = 0; i < IM_ARRAYSIZE(TileSpaceItems); ++i)
					{
						if (ImGui::Selectable(TileSpaceItems[i], TileSpaceIndex == i) && TileSpaceIndex != i)
						{
							Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
							if (Override)
							{
								Override->bWorldUVLocalSpace = (i == 1);
								Effective = Override;
							}
						}
					}
					ImGui::EndCombo();
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("World: 메시를 돌려도 무늬는 월드에 고정\nLocal: 무늬가 메시를 따라 돈다");
				}

				ImGui::TableNextRow();

				// 5: Sampler State
				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Sampler");

				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1.0f);

				// ESamplerState에는 그림자 전용(ShadowPCF)도 있어서 Count가 아니라 이 목록 길이만큼만 보여 준다.
				const char* SamplerItems[] = { "Linear Clamp", "Linear Wrap" };
				int SamplerIndex = Effective ? static_cast<int>(Effective->SamplerState) : static_cast<int>(ESamplerState::LinearClamp);
				if (SamplerIndex < 0 || SamplerIndex >= IM_ARRAYSIZE(SamplerItems))
				{
					SamplerIndex = static_cast<int>(ESamplerState::LinearClamp);
				}
				if (ImGui::BeginCombo("##SamplerState", SamplerItems[SamplerIndex]))
				{
					for (int i = 0; i < IM_ARRAYSIZE(SamplerItems); ++i)
					{
						if (ImGui::Selectable(SamplerItems[i], SamplerIndex == i))
						{
							Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
							if (Override)
							{
								Override->SamplerState = static_cast<ESamplerState>(i);
								Effective = Override;
							}
						}
					}
					ImGui::EndCombo();
				}

				ImGui::TableNextRow();

				// 6: Blend State
				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Blend Mode");

				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1.0f);

				// EBlendState 순서와 같다.
				const char* BlendItems[] = { "Opaque", "Alpha Blend", "Additive" };
				int BlendIndex = Effective ? static_cast<int>(Effective->BlendState) : static_cast<int>(EBlendState::Opaque);
				// 목록에 없는 블렌드 상태(NoColorWrite 등)면 배열 밖을 읽지 않도록 Opaque로 보여 준다.
				if (BlendIndex < 0 || BlendIndex >= IM_ARRAYSIZE(BlendItems))
				{
					BlendIndex = static_cast<int>(EBlendState::Opaque);
				}
				if (ImGui::BeginCombo("##BlendState", BlendItems[BlendIndex]))
				{
					for (int i = 0; i < IM_ARRAYSIZE(BlendItems); ++i)
					{
						if (ImGui::Selectable(BlendItems[i], BlendIndex == i))
						{
							Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
							if (Override)
							{
								Override->SetBlendState(static_cast<EBlendState>(i));
								Effective = Override;
							}
						}
					}
					ImGui::EndCombo();
				}

				ImGui::EndTable();
			}
			ImGui::TreePop();
		}
		ImGui::PopID();

		ImGui::Separator();
	}
}

// UClass에 등록된 프로퍼티를 타입에 맞는 위젯으로 그린다
bool DrawProperty(UObject* Object, const FProperty& Property, ImFont* CustomFont)
{
	void* ValuePtr = reinterpret_cast<char*>(Object) + Property.Offset;
	const FString Label = "##" + Property.Name;

	const char* DisplayName = Property.Name.c_str();

	// 패널이 넓으면 라벨 | 값을 한 줄에, 좁거나 라벨이 길면 라벨 아래 줄에 값을 놓는다.
	const float StartX = ImGui::GetCursorPosX();
	const float AvailableWidth = ImGui::GetContentRegionAvail().x;
	const float LabelWidth = AvailableWidth * 0.5f;

	// 데칼 머티리얼은 슬롯이 여러 줄이라 맞춰지도록 모두 라벨 아래에 놓는다.
	const bool bDecalMaterial = Property.Type == EPropertyType::Object && Property.Class == UMaterial::StaticClass() && Cast<UDecalComponent>(Object);

	const bool bInline = Property.Type != EPropertyType::Transform && !bDecalMaterial && AvailableWidth >= EditorUIScale::Px(260.0f) && ImGui::CalcTextSize(DisplayName).x < LabelWidth;

	ImGui::AlignTextToFramePadding();
	ImGui::TextWrapped("%s", DisplayName);

	if (bInline)
	{
		ImGui::SameLine(StartX + LabelWidth);
	}

	ImGui::SetNextItemWidth(-1.0f);

	bool bChanged = false;

	switch (Property.Type)
	{
	case EPropertyType::Float:
		bChanged = ImGui::DragFloat(Label.c_str(), static_cast<float*>(ValuePtr), 0.1f);
		break;
	case EPropertyType::Int:
		bChanged = ImGui::DragInt(Label.c_str(), static_cast<int*>(ValuePtr), 1.0f);
		break;
	case EPropertyType::Bool:
		bChanged = ImGui::Checkbox(Label.c_str(), static_cast<bool*>(ValuePtr));
		break;
	case EPropertyType::Vector:
	{
		FVector* Value = static_cast<FVector*>(ValuePtr);
		bChanged = ImGui::DragFloat3(Label.c_str(), Value->V, 0.1f);
		break;
	}
	case EPropertyType::Rotator:
	{
		FRotator* Value = static_cast<FRotator*>(ValuePtr);
		// Transform의 Rotation과 같은 X/Y/Z 축 순서로 보여준다.
		float Euler[3] = { Value->Roll, Value->Pitch, Value->Yaw };
		if (ImGui::DragFloat3(Label.c_str(), Euler, 0.1f))
		{
			Value->Roll = Euler[0];
			Value->Pitch = Euler[1];
			Value->Yaw = Euler[2];
			bChanged = true;
		}
		break;
	}


	case EPropertyType::Vector4:
	{
		FVector4* Value = static_cast<FVector4*>(ValuePtr);
		bChanged = ImGui::DragFloat4(Label.c_str(), &Value->X, 0.1f);
		break;
	}

	case EPropertyType::Color:
	{
		FColor* C = static_cast<FColor*>(ValuePtr);
		float V[4] = { C->R / 255.f, C->G / 255.f, C->B / 255.f, C->A / 255.f };
		if (ImGui::ColorEdit4(Label.c_str(), V))
		{
			*C = FColor(uint8(V[0] * 255.f + 0.5f), uint8(V[1] * 255.f + 0.5f),
				uint8(V[2] * 255.f + 0.5f), uint8(V[3] * 255.f + 0.5f));
			bChanged = true;
		}
		break;
	}

	case EPropertyType::LinearColor:
	{
		FLinearColor* Value = static_cast<FLinearColor*>(ValuePtr);
		bChanged = DrawLinearColorEdit4(Label.c_str(), Value->RGBA);
		break;
	}
	case EPropertyType::String:
	{
		FString* Value = static_cast<FString*>(ValuePtr);

		char Buffer[256] = {};
		strncpy_s(Buffer, Value->c_str(), sizeof(Buffer) - 1);
		if (CustomFont)
		{
			ImGui::PushFont(CustomFont);
		}
		if (ImGui::InputText(Label.c_str(), Buffer, sizeof(Buffer)))
		{
			*Value = Buffer;
			bChanged = true;
		}
		if (CustomFont)
		{
			ImGui::PopFont();
		}
		break;
	}
	case EPropertyType::Transform:
	{
		FTransform* Value = static_cast<FTransform*>(ValuePtr);

		FTransform Transform = *Value;
		bool bChange = false;

		bChanged |= DrawVector3Controller("Location", Value->Location.V, 0.0f);
		FRotator Rotation = Value->Rotation.ToFRotator();
		if (DrawRotatorAsXYZ("Rotation", Rotation))
		{
			Value->Rotation = Rotation.Quaternion();
			bChanged = true;
		}
		bChanged |= DrawVector3Controller("Scale", Value->Scale.V, 1.0f);

		break;
	}
	case EPropertyType::Object:
	{
		UObject** ObjPtr = static_cast<UObject**>(ValuePtr);

		if (Property.Class == UMaterial::StaticClass())
		{
			UMaterial** MaterialPtr = reinterpret_cast<UMaterial**>(ObjPtr);
			// 데칼은 메시 머티리얼처럼 두 번째 슬롯을 노멀맵으로 쓴다.
			if (bDecalMaterial)
			{
				DrawTextureSlot(MaterialPtr, 0, "Texture");
				DrawTextureSlot(MaterialPtr, 1, "Normal Map");
			}
			else
			{
				DrawTextureSlot(MaterialPtr, 0, "Slot 0");
			}
		}
		else if (Property.Class == UFont::StaticClass())
		{
			DrawFontSlot(Object, reinterpret_cast<UFont**>(ObjPtr));
		}
		else if (Property.Class == UStaticMesh::StaticClass())
		{
			DrawMeshSlot(Object, reinterpret_cast<UStaticMesh**>(ObjPtr));
		}
		else if (Property.Class == USkeletalMesh::StaticClass())
		{
			DrawSkeletalMeshSlot(Object, reinterpret_cast<USkeletalMesh**>(ObjPtr));
		}
		else if (Property.Class == UAnimationAsset::StaticClass())
		{
			bChanged = DrawAnimationAssetSlot(Object, reinterpret_cast<UAnimationAsset**>(ObjPtr));
		}
		break;
	}
	case EPropertyType::Enum:
	{
		int32* Value = static_cast<int32*>(ValuePtr);
		const FString Preview = Property.GetEnumNameStringByValue(*Value);
		if (ImGui::BeginCombo(Label.c_str(), Preview.c_str()))
		{
			for (int32 Index = 0; Index < Property.EnumNames.Num(); ++Index)
			{
				const bool bSelected = Index == *Value;
				if (ImGui::Selectable(Property.EnumNames[Index].c_str(), bSelected) && !bSelected)
				{
					*Value = Index;
					bChanged = true;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		break;
	}
	default:
		ImGui::TextDisabled("(Unsupported)");
		break;
	}

	if (bChanged)
	{
		Object->PostEditChangeProperty(Property);
	}
	return bChanged;
}

// 클래스 선택은 현재 세션에 적용한다. 클래스 참조의 저장·복원은 후속 작업이다.
bool DrawAnimationInstanceSettings(USkeletalMeshComponent& Component)
{
	bool bChanged = false;
	const EAnimationMode::Type Modes[] = {
		EAnimationMode::AnimationDefault, EAnimationMode::AnimationSingleNode, EAnimationMode::AnimationClass
	};
	const char* Labels[] = { "Default Anim Instance", "Single Node Instance", "Custom Anim Instance" };
	int32 Selected = 0;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (Component.GetAnimationMode() == Modes[Index]) { Selected = Index; }
	}
	if (ImGui::BeginCombo("Animation Mode", Labels[Selected]))
	{
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (ImGui::Selectable(Labels[Index], Selected == Index) && Selected != Index)
			{
				Component.SetAnimationMode(Modes[Index]);
				bChanged = true;
			}
		}
		ImGui::EndCombo();
	}
	if (Component.GetAnimationMode() == EAnimationMode::AnimationDefault)
	{
		ImGui::TextWrapped("Connect a graph from Lua. No root uses the reference pose.");
	}
	else if (Component.GetAnimationMode() == EAnimationMode::AnimationClass)
	{
		UClass* SelectedClass = Component.GetAnimInstanceClass();
		if (ImGui::BeginCombo("Anim Class", SelectedClass ? SelectedClass->Name.c_str() : "None"))
		{
			if (ImGui::Selectable("None", !SelectedClass) && SelectedClass)
			{
				Component.SetAnimInstanceClass(nullptr);
				bChanged = true;
			}
			TArray<UClass*> Classes;
			GetDerivedClasses(UAnimInstance::StaticClass(), Classes);
			std::sort(Classes.begin(), Classes.end(), [](const UClass* A, const UClass* B) { return A->Name < B->Name; });
			for (UClass* Class : Classes)
			{
				if (!Class->Constructor || Class == ULuaAnimInstance::StaticClass() || Class == UAnimSingleNodeInstance::StaticClass()) { continue; }
				if (ImGui::Selectable(Class->Name.c_str(), Class == SelectedClass) && Class != SelectedClass)
				{
					Component.SetAnimInstanceClass(Class);
					bChanged = true;
				}
			}
			ImGui::EndCombo();
		}
		SelectedClass = Component.GetAnimInstanceClass();
		ImGui::TextWrapped("Lua graphs require a class derived from ULuaAnimInstance.");
		if (SelectedClass)
		{
			ImGui::TextDisabled("Lua Graph: %s", SelectedClass->IsChildOf(ULuaAnimInstance::StaticClass()) ? "Supported" : "Not supported");
		}
		ImGui::TextDisabled("Class selection is not saved yet.");
	}
	return bChanged;
}

// 클래스 계층을 따라 올라가며 각 단계의 프로퍼티를 표시
bool DrawProperties(UObject* Object, ImFont* CustomFont)
{
	if (!Object)
	{
		return false;
	}

	bool bChanged = false;
	TArray<UClass*> ClassChain;
	for (UClass* Class = Object->GetClass(); Class; Class = Class->Super)
	{
		ClassChain.Add(Class);
	}

	// 기반 클래스부터 표시
	for (auto It = ClassChain.rbegin(); It != ClassChain.rend(); ++It)
	{
		UClass* Class = *It;

		// 노출할 프로퍼티가 하나도 없으면 헤더도 그리지 않는다
		bool bHasVisibleProperty = false;
		for (const FProperty& Property : Class->GetProperties())
		{
			if (Property.HasAnyFlags(CPF_Edit))
			{
				bHasVisibleProperty = true;
				break;
			}
		}
		if (!bHasVisibleProperty)
		{
			continue;
		}

		ImGui::PushID(Class->Name.c_str());
		if (ImGui::CollapsingHeader(Class->Name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (const FProperty& Property : Class->GetProperties())
			{
				if (!Property.HasAnyFlags(CPF_Edit))
				{
					continue;
				}
				if (auto* Mesh = Cast<USkeletalMeshComponent>(Object); Mesh && Class == USkeletalMeshComponent::StaticClass())
				{
					if (Property.Name == "AnimationMode")
					{
						bChanged |= DrawAnimationInstanceSettings(*Mesh);
						continue;
					}
					const bool bPlaybackSetting = Property.Name == "AnimToPlay" || Property.Name == "bSavedLooping" ||
						Property.Name == "bSavedPlaying" || Property.Name == "SavedPosition" || Property.Name == "SavedPlayRate";
					if (bPlaybackSetting && Mesh->GetAnimationMode() != EAnimationMode::AnimationSingleNode) { continue; }
				}

				const bool bReadOnly = Property.HasAnyFlags(CPF_EditConst) || !Object->CanEditChange(Property);
				if (bReadOnly)
				{
					ImGui::BeginDisabled();
				}
				bChanged |= DrawProperty(Object, Property, CustomFont);
				if (bReadOnly)
				{
					ImGui::EndDisabled();
				}
			}
		}
		ImGui::PopID();
	}
	return bChanged;
}

void DrawCameraDetailRow(const char* Label, const FString& Value)
{
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::TextUnformatted(Label);
	ImGui::TableSetColumnIndex(1);
	ImGui::TextWrapped("%s", Value.c_str());
}

const char* GetCameraShakePatternName(const FCameraShakePattern* Pattern)
{
	if (!Pattern)
	{
		return "None";
	}
	if (dynamic_cast<const FWaveOscillatorCameraShakePattern*>(Pattern))
	{
		return "Wave";
	}
	if (dynamic_cast<const FPerlinNoiseCameraShakePattern*>(Pattern))
	{
		return "Perlin";
	}
	if (dynamic_cast<const FCompositeCameraShakePattern*>(Pattern))
	{
		return "Composite";
	}
	return "Custom";
}

void DrawPlayerCameraManagerDetails(const APlayerCameraManager& CameraManager)
{
	if (ImGui::CollapsingHeader("View Target", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const AActor* ViewTarget{ CameraManager.GetViewTarget() };
		const UCameraComponent* Camera{ CameraManager.GetViewTargetCamera() };
		if (ImGui::BeginTable("ViewTargetDetails", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Shake Scale").x);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
			DrawCameraDetailRow("Actor", ViewTarget ? ViewTarget->GetName() : "None");
			DrawCameraDetailRow("Class", ViewTarget ? ViewTarget->GetClass()->Name : "None");
			DrawCameraDetailRow("Camera", Camera ? Camera->GetName() : "None");
			DrawCameraDetailRow("Valid View", CameraManager.HasValidCameraView() ? "Yes" : "No");
			ImGui::EndTable();
		}
	}
	if (!ImGui::CollapsingHeader("Camera Modifiers", ImGuiTreeNodeFlags_DefaultOpen))
	{
		return;
	}
	TArray<UCameraModifier*> Modifiers{ CameraManager.GetCameraModifiers() };
	ImGui::Text("Count: %d", static_cast<int32>(Modifiers.Num()));
	if (Modifiers.IsEmpty())
	{
		ImGui::TextDisabled("No camera modifiers");
		return;
	}
	std::stable_sort(Modifiers.begin(), Modifiers.end(), [](const UCameraModifier* Left, const UCameraModifier* Right)
		{ return Left->GetPriority() < Right->GetPriority(); });
	for (UCameraModifier* Modifier : Modifiers)
	{
		if (!ImGui::TreeNodeEx(Modifier, ImGuiTreeNodeFlags_DefaultOpen, "%s", Modifier->GetName().c_str()))
		{
			continue;
		}
		if (ImGui::BeginTable("ModifierDetails", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Shake Scale").x);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
			const char* State{ Modifier->IsDisabled() ? "Disabled" : Modifier->IsPendingDisable() ? "Blending out"
																								  : "Enabled" };
			DrawCameraDetailRow("Class", Modifier->GetClass()->Name);
			DrawCameraDetailRow("Priority", std::format("{}", Modifier->GetPriority()));
			DrawCameraDetailRow("Alpha", std::format("{:.3f}", Modifier->GetAlpha()));
			DrawCameraDetailRow("State", State);
			if (const UCameraModifier_CameraShake* Shake{ Cast<UCameraModifier_CameraShake>(Modifier) })
			{
				DrawCameraDetailRow("Pattern", GetCameraShakePatternName(Shake->GetRootPattern()));
				DrawCameraDetailRow("Shake Scale", std::format("{:.3f}", Shake->GetShakeScale()));
				DrawCameraDetailRow("Finished", Shake->IsFinished() ? "Yes" : "No");
			}
			ImGui::EndTable();
		}
		ImGui::TreePop();
	}
}

void DrawActorHeader(AActor* Actor, UActorComponent*& OutSelectedComponent, UActorComponent*& OutAddedComponent)
{
	if (!Actor)
	{
		return;
	}

	static char ComponentNameInputBuf[256] = {};

	ImGui::Text("Actor: %s", Actor->GetName().c_str());
	ImGui::Text("UUID: %u", Actor->GetUUID());

	if (ImGui::Button("Add Component"))
	{
		ImGui::OpenPopup("AddComponentPopup");
	}

	if (ImGui::BeginPopup("AddComponentPopup"))
	{
		ImGui::Text("Component Name:");
		ImGui::SameLine();
		ImGui::InputText("##Component Name", ComponentNameInputBuf, sizeof(ComponentNameInputBuf));
		TArray<UClass*> Classes;
		GetDerivedClasses(UActorComponent::StaticClass(), Classes);
		std::sort(Classes.begin(), Classes.end(), [](const UClass* A, const UClass* B)
			{ return A->Name < B->Name; });
		for (UClass* Class : Classes)
		{
			if (ImGui::MenuItem(Class->Name.c_str()))
			{
				OutAddedComponent = Actor->AddComponent(Class, FString(ComponentNameInputBuf));
				if (OutAddedComponent)
				{
					OutAddedComponent->MarkUserCreated();
					OutSelectedComponent = OutAddedComponent;
				}
				ComponentNameInputBuf[0] = '\0';
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndPopup();
	}
}
}

void FDetailsPanel::DrawShadowMapPreview(const ULightComponent& Light)
{
	if (!ImGui::CollapsingHeader("Shadow Depth Map", ImGuiTreeNodeFlags_DefaultOpen))
	{
		return;
	}

	// UI에서 Cast Shadows를 끄면 이전에 생성한 맵도 즉시 숨긴다.
	if (!Light.CastsShadows())
	{
		ImGui::TextDisabled("Cast Shadows is disabled.");
		return;
	}
	// 선택한 라이트가 속한 World를 표시해 Editor·PIE 결과를 구분한다.
	UWorld* LightWorld = Light.GetWorld();
	if (LightWorld)
	{
		ImGui::TextDisabled("World: %s", LightWorld->IsPlayInEditor() ? "PIE" : "Editor");
	}

	if (Light.GetLightType() == ELightType::Directional)
	{
		// 화면 조명에 선택되지 않은 Directional에는 그림자 작업이 생성되지 않는다.
		if (LightWorld && LightWorld->GetScene().GetPrimaryDirectionalLight() != Light.SceneProxy)
		{
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextDisabled("This directional light is not used by this world.");
			ImGui::PopTextWrapPos();
			ImGui::TextWrapped("Only the first visible directional light is used.");
			return;
		}

		// 현재 편집값을 표시한다. 실제 투영 범위는 각 View의 클립 범위로 제한된다.
		const auto& Directional = static_cast<const UDirectionalLightComponent&>(Light);
		ImGui::Text("Requested ShadowDistance: %.1f", Directional.GetShadowDistance());
		// Directional은 View마다 단일 맵 또는 Cascade 맵을 만든다.
		ImGui::TextWrapped("Each view has its own directional shadow projection(s).");
	}

	// 설정 UI보다 그림자 패스가 먼저 실행되므로 변경 결과는 다음 프레임에 반영된다.
	ImGui::TextDisabled("Shadow settings apply next frame.");

	// 선택한 실제 라이트의 현재 프레임 작업만 사용한다.
	if (!Renderer || !Light.SceneProxy)
	{
		ImGui::TextDisabled("No shadow map generated this frame.");
		return;
	}

	uint32 ViewId = 0;
	uint32 ProjectionIndex = 0;

	if (Light.GetLightType() == ELightType::Directional)
	{
		TArray<uint32> ViewIds;
		Renderer->GetShadowViewIds(*Light.SceneProxy, ViewIds);
		if (ViewIds.IsEmpty())
		{
			ImGui::TextDisabled("No shadow map generated this frame.");
			return;
		}

		// 레이아웃·PIE·ViewMode 변경으로 사라진 선택을 복구한다.
		if (ViewIds.Find(ShadowPreviewViewId) == INDEX_NONE)
		{
			ShadowPreviewViewId = ViewIds[0];
		}

		const FString ViewLabel = std::format("View {}", ShadowPreviewViewId);
		if (ImGui::BeginCombo("Shadow View", ViewLabel.c_str()))
		{
			for (uint32 Candidate : ViewIds)
			{
				const FString Label = std::format("View {}", Candidate);
				if (ImGui::Selectable(Label.c_str(), Candidate == ShadowPreviewViewId))
				{
					ShadowPreviewViewId = Candidate;
				}
			}
			ImGui::EndCombo();
		}

		// 선택된 View의 Client 상태를 편집한다. 라이트 설정으로 저장하지 않는다.
		if (ShadowCascadeDebugGetter && ShadowCascadeDebugSetter)
		{
			bool bVisualize = ShadowCascadeDebugGetter(ShadowPreviewViewId);
			if (ImGui::Checkbox("Cascade Colors (Selected View)", &bVisualize))
			{
				ShadowCascadeDebugSetter(ShadowPreviewViewId, bVisualize);
			}
		}

		// 편집된 Cascade 개수보다 실제 생성된 타깃을 기준으로 목록을 만든다.
		TArray<uint32> Projections;
		for (uint32 Index = 0; Index < UDirectionalLightComponent::MaxShadowCascades; ++Index)
		{
			if (Renderer->GetShadowIndex(*Light.SceneProxy, Index, ShadowPreviewViewId) != INDEX_NONE)
			{
				Projections.Add(Index);
			}
		}
		if (Projections.IsEmpty())
		{
			return;
		}

		if (Projections.Find(ShadowPreviewProjectionIndex) == INDEX_NONE)
		{
			ShadowPreviewProjectionIndex = Projections[0];
		}

		const FString ProjectionLabel = std::format("Cascade {}", ShadowPreviewProjectionIndex + 1);
		if (ImGui::BeginCombo("Shadow Cascade", ProjectionLabel.c_str()))
		{
			for (uint32 Candidate : Projections)
			{
				const FString Label = std::format("Cascade {}", Candidate + 1);
				if (ImGui::Selectable(Label.c_str(), Candidate == ShadowPreviewProjectionIndex))
				{
					ShadowPreviewProjectionIndex = Candidate;
				}
			}
			ImGui::EndCombo();
		}

		ViewId = ShadowPreviewViewId;
		ProjectionIndex = ShadowPreviewProjectionIndex;
	}
	else if (Light.GetLightType() == ELightType::Point)
	{
		// Point 투영과 Shader의 면 선택 순서에 맞춘 표시 이름이다.
		constexpr uint32 FaceCount = 6;
		static constexpr const char* FaceNames[FaceCount] = {
			"+X", "-X", "+Y", "-Y", "+Z", "-Z"
		};

		// Point는 View 0을 공유하며, 실제 Depth 타깃이 있는 면만 표시한다.
		TArray<uint32> Faces;
		for (uint32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
		{
			if (Renderer->GetShadowIndex(*Light.SceneProxy, FaceIndex, 0) != INDEX_NONE)
			{
				Faces.Add(FaceIndex);
			}
		}

		if (Faces.IsEmpty())
		{
			ImGui::TextDisabled("No shadow map generated this frame.");
			return;
		}

		// 라이트 전환이나 타깃 변경으로 선택한 면이 사라지면 복구한다.
		if (Faces.Find(ShadowPreviewProjectionIndex) == INDEX_NONE)
		{
			ShadowPreviewProjectionIndex = Faces[0];
		}

		if (ImGui::BeginCombo("Shadow Face", FaceNames[ShadowPreviewProjectionIndex]))
		{
			for (uint32 Candidate : Faces)
			{
				const bool bSelected = Candidate == ShadowPreviewProjectionIndex;
				if (ImGui::Selectable(FaceNames[Candidate], bSelected))
				{
					ShadowPreviewProjectionIndex = Candidate;
				}

				// 콤보박스를 열었을 때 현재 선택한 면에 포커스를 둔다.
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		// 아래 공통 조회·Viewport·UV 계산에 선택한 면을 전달한다.
		ProjectionIndex = ShadowPreviewProjectionIndex;
	}
	if (Renderer->IsShadowKnownEmpty(*Light.SceneProxy, ProjectionIndex, ViewId))
	{
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextDisabled("No casters: depth target allocation skipped.");
		ImGui::PopTextWrapPos();
		return;
	}
	// Spot은 기존 공유 키 (0, 0)를 사용한다.
	const FTexture2D* DepthTexture = Renderer->GetShadowDepthTexture(*Light.SceneProxy, ProjectionIndex, ViewId);
	FViewportSettings ShadowViewport;
	if (!DepthTexture || !Renderer->GetShadowDepthViewport(*Light.SceneProxy, ShadowViewport, ProjectionIndex, ViewId))
	{
		ImGui::TextDisabled("No shadow map generated this frame.");
		return;
	}

	// 물리 Atlas 크기와 선택한 라이트의 실제 타일 해상도를 구분한다.
	ImGui::Text("Rendered: %u x %u", ShadowViewport.Width, ShadowViewport.Height);

	// 이번 프레임에 캐시를 재사용했는지, 다시 그렸다면 그 이유를 표시한다.
	FRenderer::FShadowCacheDebugInfo CacheInfo;
	if (Renderer->GetShadowCacheDebugInfo(*Light.SceneProxy, CacheInfo, ProjectionIndex, ViewId))
	{
		if (CacheInfo.Result == EShadowCacheResult::Hit)
		{
			ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Cache: Hit (updated %llu frames ago)",
				static_cast<unsigned long long>(CacheInfo.FramesSinceUpdate));
		}
		else
		{
			ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "Cache: Updated (%s)",
				GetShadowCacheResultName(CacheInfo.Result));
		}
		ImGui::TextDisabled("Casters: %u", CacheInfo.CasterCount);
	}
	ImGui::TextDisabled("Atlas: %u x %u", DepthTexture->GetWidth(), DepthTexture->GetHeight());
	// 요청값과 품질 상한을 구분하고, 실제 다음 프레임 변경만 대기 상태로 표시한다.
	const uint32 EffectiveResolution = static_cast<uint32>(FShadowSettings::GetEffectiveResolution(Light.GetShadowMapResolution()));
	ImGui::TextDisabled("Requested: %d  Global max: %d", Light.GetShadowMapResolution(), FShadowSettings::GetMaxResolution());
	if (ShadowViewport.Width != EffectiveResolution)
	{
		ImGui::TextDisabled("Resolution applies next frame.");
	}

	// 현재 Spot Shadow Map은 정사각형이며, 패널 폭 안에서 표시한다.
	const float PreviewSize = std::min(EditorUIScale::Px(256.0f), ImGui::GetContentRegionAvail().x);
	if (PreviewSize <= 0.0f)
	{
		return;
	}

	// 동일한 SRV를 사용하되 선택한 라이트에 배정된 타일만 표시한다.
	const float InvWidth = 1.0f / DepthTexture->GetWidth();
	const float InvHeight = 1.0f / DepthTexture->GetHeight();
	const ImVec2 UVMin(ShadowViewport.StartX * InvWidth, ShadowViewport.StartY * InvHeight);
	const ImVec2 UVMax((ShadowViewport.StartX + ShadowViewport.Width) * InvWidth,
		(ShadowViewport.StartY + ShadowViewport.Height) * InvHeight);
	ImGui::Image((ImTextureID)DepthTexture->GetSRV(), ImVec2(PreviewSize, PreviewSize), UVMin, UVMax);
}

bool DrawObjectProperties(UObject* Object, ImFont* CustomFont)
{
	return DrawProperties(Object, CustomFont);
}

bool FDetailsPanel::Init()
{
	ImGuiIO& io = ImGui::GetIO();

	io.Fonts->AddFontDefault();

	const char* fontPath = EditorResourcePaths::UIFont;
	float fontSize = 15.0f;
	const ImWchar* koreanRanges = io.Fonts->GetGlyphRangesKorean();

	// 기본 폰트(영문 전용)에 한글 글리프만 합친다. PushFont 없이 그리는 툴팁·콤보 같은 곳에서도 한글이 나오게 한다.
	// 기본 폰트에 이미 있는 영문은 그대로 쓰고, 없는 글자만 이 폰트에서 가져온다. 크기 0 = 기본 폰트 크기에 맞춤.
	ImFontConfig KoreanMergeConfig;
	KoreanMergeConfig.MergeMode = true;
	io.Fonts->AddFontFromFileTTF(fontPath, 0.0f, &KoreanMergeConfig, koreanRanges);

	CustomFont = io.Fonts->AddFontFromFileTTF(fontPath, fontSize, nullptr, koreanRanges);
	assert(CustomFont != nullptr);

	return true;
}

void FDetailsPanel::Tick(float DeltaTime)
{
}


void FDetailsPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(400.0f), EditorUIScale::Px(200.0f)), ImGuiCond_FirstUseEver);

	ImGui::Begin("Details");

	if (TargetActor)
	{
		// 빌보드처럼 디테일에 숨겨진 컴포넌트가 선택되면 노출되는 부모로 올린다.
		// (예: 라이트는 빌보드를 클릭해서 고르지만 수치는 SpotLight 쪽에 있다)
		while (TargetComponent && TargetComponent->IsHiddenInDetails())
		{
			USceneComponent* HiddenComponent = Cast<USceneComponent>(TargetComponent);
			TargetComponent = HiddenComponent ? HiddenComponent->GetAttachParent() : nullptr;
		}

		UActorComponent* SelectCandidateComponent = TargetComponent;

		UActorComponent* AddedComponent = nullptr;
		DrawActorHeader(TargetActor, TargetComponent, AddedComponent);
		if (USceneComponent* AddedSceneComponent = Cast<USceneComponent>(AddedComponent))
		{
			USceneComponent* Root = TargetActor->GetRootComponent();
			bool bReplacesDefaultRoot = TargetActor->IsDefaultSceneRoot(Root);
			if (bReplacesDefaultRoot)
			{
				for (USceneComponent* Child : Root->GetAttachChildren())
				{
					if (Child && Child != AddedSceneComponent && Child->GetOwner() == TargetActor && !Child->IsHiddenInDetails())
					{
						bReplacesDefaultRoot = false;
					}
				}
			}

			if (bReplacesDefaultRoot)
			{
				PendingMakeRootComponent = AddedSceneComponent;
			}
		}
		DrawCompoenetList(TargetActor, TargetComponent);
		ImGui::Separator();

		// 액터 -> 컴포넌트 순으로, 클래스별 프로퍼티 표시
		ImGui::PushID(TargetActor);
		DrawProperties(TargetActor, CustomFont);
		if (const APlayerCameraManager* CameraManager{ Cast<APlayerCameraManager>(TargetActor) })
		{
			DrawPlayerCameraManagerDetails(*CameraManager);
		}
		ImGui::PopID();

		if (TargetComponent)
		{
			ImGui::PushID(TargetComponent);
			ImGui::SeparatorText("Component Properties");
			DrawProperties(TargetComponent, CustomFont);

			if (UScriptComponent* ScriptComponent = Cast<UScriptComponent>(TargetComponent))
			{
				DrawScriptComponentActions(*ScriptComponent);
			}

			// Light Component를 선택했을 때 기존 Property 아래에 Depth Map을 표시한다.
			if (const ULightComponent* Light = Cast<ULightComponent>(TargetComponent))
			{
				DrawShadowMapPreview(*Light);
			}

			if (SelectCandidateComponent != TargetComponent)
			{
				SelectComponent(TargetComponent);
			}
			if (UMeshComponent* MeshComponent = Cast<UMeshComponent>(TargetComponent))
			{
				DrawMaterialSlots(MeshComponent);
			}
			ImGui::PopID();
		}
	}

	if (PendingDeleteComponent)
	{
		if (DeleteCallback)
		{
			DeleteCallback(PendingDeleteComponent);
		}

		PendingDeleteComponent = nullptr;
	}

	if (PendingMakeRootComponent)
	{
		if (MakeRootCallback)
		{
			MakeRootCallback(PendingMakeRootComponent);
		}

		PendingMakeRootComponent = nullptr;
	}
	ImGui::End();
}


void FDetailsPanel::SelectComponent(UActorComponent* Component)
{
	if (!Component)
	{
		TargetComponent = nullptr;
		EditingNameComponent = nullptr;
		bFocusNameEdit = false;

		if (Callback)
		{
			Callback(nullptr);
		}

		return;
	}

	TargetComponent = Component;

	if (Callback)
	{
		Callback(TargetComponent);
	}
}

void FDetailsPanel::DrawCompoenetList(AActor* SelectedActor, UActorComponent*& OutSelectedComponent)
{
	if (!SelectedActor)
	{
		return;
	}

	if (PendingDragComponent && PendingDroppedComponent)
	{
		TryReparent(PendingDroppedComponent, PendingDragComponent);
		PendingDragComponent = nullptr;
		PendingDroppedComponent = nullptr;
	}

	USceneComponent* Root = SelectedActor->GetRootComponent();

	if (Root && ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen))
	{
		DrawSceneComponentNode(Root, Root, OutSelectedComponent);
	}

	ImGui::Separator();

	// Draw ActorComponent
	for (UActorComponent* ActorComp : SelectedActor->GetComponents())
	{
		if (!ActorComp || Cast<USceneComponent>(ActorComp))
		{
			continue;
		}

		ImGui::PushID(ActorComp);
		ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (ActorComp == OutSelectedComponent)
		{
			Flags |= ImGuiTreeNodeFlags_Selected;
		}

		FString Label = ActorComp->GetName();
		const ImVec2 RowStart = ImGui::GetCursorScreenPos();
		const float RowWidth = ImGui::GetContentRegionAvail().x;
		const bool bIsEditingName = EditingNameComponent == ActorComp;

		ImGui::TreeNodeEx("ActorComponentNode", Flags, "%s", bIsEditingName ? "" : Label.c_str());

		if (bIsEditingName)
		{
			DrawComponentRenameField(ActorComp);
			ImGui::PopID();
			continue;
		}

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			OutSelectedComponent = ActorComp;
		}

		HandleComponentRenameShortcut(ActorComp, OutSelectedComponent);

		if (ImGui::BeginPopupContextItem("Component Context"))
		{
			DrawComponentRenameMenuItem(ActorComp);
			DrawComponentDeleteMenuItem(ActorComp);
			ImGui::EndPopup();
		}

		const FString ClassName = std::format("({})", ActorComp->GetClass()->Name);
		const float TextX = RowStart.x + RowWidth - ImGui::CalcTextSize(ClassName.c_str()).x;
		ImGui::GetWindowDrawList()->AddText(ImVec2(TextX, RowStart.y), IM_COL32(200, 200, 200, 255), ClassName.c_str());

		ImGui::PopID();
	}
}

void FDetailsPanel::BeginComponentRename(UActorComponent* Component)
{
	EditingNameComponent = Component;
	strncpy_s(ComponentNameEditBuffer, sizeof(ComponentNameEditBuffer), Component->GetName().c_str(), _TRUNCATE);
	bFocusNameEdit = true;
}

void FDetailsPanel::HandleComponentRenameShortcut(UActorComponent* Component, const UActorComponent* SelectedComponent)
{
	if (Component == SelectedComponent && Component->IsUserCreated() && !ImGui::GetIO().WantTextInput
		&& ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_F2, false))
	{
		BeginComponentRename(Component);
	}
}

void FDetailsPanel::DrawComponentRenameMenuItem(UActorComponent* Component)
{
	if (ImGui::MenuItem("Rename", "F2", false, Component->IsUserCreated()))
	{
		BeginComponentRename(Component);
	}
	if (!Component->IsUserCreated() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		ImGui::SetTooltip("Components created by the actor class cannot be renamed.");
	}
}

void FDetailsPanel::DrawComponentDeleteMenuItem(UActorComponent* Component)
{
	const AActor* Owner = Component->GetOwner();
	const bool bCanDelete = Owner && Owner->CanUserDestroyComponent(Component);
	if (ImGui::MenuItem("Delete", nullptr, false, bCanDelete))
	{
		PendingDeleteComponent = Component;
	}
	if (!bCanDelete && Owner && !Component->IsUserCreated() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		ImGui::SetTooltip("Components created by the actor class cannot be deleted.");
	}
}

void FDetailsPanel::DrawComponentRenameField(UActorComponent* Component)
{
	ImGui::SameLine();
	if (bFocusNameEdit)
	{
		ImGui::SetKeyboardFocusHere();
		bFocusNameEdit = false;
	}

	ImGui::SetNextItemWidth(-FLT_MIN);
	const bool bConfirmed = ImGui::InputText("##Rename", ComponentNameEditBuffer,
		sizeof(ComponentNameEditBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
	const bool bCancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
	// 수정 없이 다른 곳을 눌러도 편집을 끝낸다.
	if (!bCancelled && !bConfirmed && !ImGui::IsItemDeactivated())
	{
		return;
	}

	const FName NewName(ComponentNameEditBuffer);
	AActor* Owner = Component->GetOwner();
	if (!bCancelled && ComponentNameEditBuffer[0] != '\0' && NewName != NAME_None && NewName != Component->GetFName())
	{
		// 같은 Outer의 같은 이름은 NewObject가 재활용하므로 컴포넌트가 아닌 객체까지 함께 검사한다.
		if (Owner && !Owner->FindComponentByName(NewName) && !StaticFindObjectFast(nullptr, Owner, NewName))
		{
			Component->SetName(NewName);
		}
		else
		{
			HTR_LOG(Warning, "A component with this name already exists in this Actor.");
		}
	}
	EditingNameComponent = nullptr;
}

void FDetailsPanel::DrawSceneComponentNode(USceneComponent* Component, USceneComponent* Root, UActorComponent*& OutSelectedComponent)
{
	// 빌보드 같은 에디터 표시용 컴포넌트는 트리에 노출하지 않는다
	if (!Component || Component->IsHiddenInDetails())
	{
		return;
	}

	// 숨겨진 자식만 있는 노드가 펼침 화살표를 갖지 않도록 노출되는 자식만 센다
	TArray<USceneComponent*> Children;
	for (USceneComponent* Child : Component->GetAttachChildren())
	{
		if (Child && Child->GetOwner() == Component->GetOwner() && !Child->IsHiddenInDetails())
		{
			Children.Add(Child);
		}
	}

	const bool bIsLeaf = Children.IsEmpty();
	const bool bIsEditingName = EditingNameComponent == Component;

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
	if (Component == OutSelectedComponent)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (bIsLeaf)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	FString Label = Component->GetName();
	if (Component == Root)
	{
		Label += " (Root)";
	}

	ImGui::PushID(Component);
	const ImVec2 RowStart = ImGui::GetCursorScreenPos();
	const float RowWidth = ImGui::GetContentRegionAvail().x;
	const bool bOpen = ImGui::TreeNodeEx("ComponentNode", Flags, "%s", bIsEditingName ? "" : Label.c_str());

	USceneComponent* DragComponent = Component;

	// source: Root 제외, 현재 Component를 payload에 담음
	if (Component != Root && ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload(EditorDragDrop::Component, &Component, sizeof(USceneComponent*));
		ImGui::EndDragDropSource();
	}
	// target: 모든 행이 가능, payload를 받아 현재 Component를 부모로 사용
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Component))
		{
			USceneComponent* DroppedComponent = *static_cast<USceneComponent**>(Payload->Data);
			PendingDroppedComponent = DroppedComponent;
			PendingDragComponent = DragComponent;
		}
		ImGui::EndDragDropTarget();
	}

	if (!bIsEditingName)
	{
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			OutSelectedComponent = Component;
		}

		HandleComponentRenameShortcut(Component, OutSelectedComponent);

		if (ImGui::BeginPopupContextItem("Component Context"))
		{
			DrawComponentRenameMenuItem(Component);
			if (ImGui::MenuItem("Make Root", nullptr, false, Component != Root))
			{
				PendingMakeRootComponent = Component;
			}

			DrawComponentDeleteMenuItem(Component);
			ImGui::EndPopup();
		}

		const FString ClassName = std::format("({})", Component->GetClass()->Name);
		const float TextX = RowStart.x + RowWidth - ImGui::CalcTextSize(ClassName.c_str()).x;
		ImGui::GetWindowDrawList()->AddText(ImVec2(TextX, RowStart.y), IM_COL32(200, 200, 200, 255), ClassName.c_str());
	}
	else
	{
		DrawComponentRenameField(Component);
	}

	if (bOpen && !bIsLeaf)
	{
		for (USceneComponent* Child : Children)
		{
			DrawSceneComponentNode(Child, Root, OutSelectedComponent);
		}
		ImGui::TreePop();
	}

	ImGui::PopID();
}

void FDetailsPanel::TryReparent(USceneComponent* DroppedComponent, USceneComponent* DragComponent)
{
	if (DroppedComponent != nullptr && DroppedComponent->GetAttachParent() != DragComponent && DroppedComponent != DragComponent)
	{
		DroppedComponent->AttachToComponent(DragComponent, EAttachmentRule::KeepWorld);
	}
}


FDetailsPanel::~FDetailsPanel()
{
}
