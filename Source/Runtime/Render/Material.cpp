#include "EnginePCH.h"

#include "RenderCommand.h"

#include "Material.h"

#include "Asset/AssetManager.h"
#include "Components/ActorComponent.h"
#include "Render/Mesh.h"
#include "Render/SkeletalMesh.h"
#include "UObject/UObjectGlobals.h"
#include "Serialization/TypeSerializer.h"
#include "Serialization/MathSerialization.h"

namespace
{
// 저장된 경로로 텍스처를 찾고, 아직 로드 전이면 로드한다
UTexture2D* FindOrLoadTexture(const FString& Path)
{
	if (UTexture2D* Texture = UAssetManager::GetAssetByPath<UTexture2D>(Path))
	{
		return Texture;
	}
	return UAssetManager::Get().LoadTexture(Path);
}

// 저장 파일의 블렌드 문자열. 모르는 값은 Opaque로 읽는다.
const char* BlendStateToString(EBlendState State)
{
	switch (State)
	{
	case EBlendState::AlphaBlend: return "AlphaBlend";
	case EBlendState::Additive: return "Additive";
	default: return "Opaque";
	}
}

EBlendState BlendStateFromString(const FString& State)
{
	if (State == "AlphaBlend")
	{
		return EBlendState::AlphaBlend;
	}
	if (State == "Additive")
	{
		return EBlendState::Additive;
	}
	return EBlendState::Opaque;
}

const char* ShadingModelToString(ELightingModel Model)
{
	switch (Model)
	{
	case ELightingModel::Gouraud: return "Gouraud";
	case ELightingModel::Lambert: return "Lambert";
	case ELightingModel::Unlit: return "Unlit";
	default: return "BlinnPhong";
	}
}

ELightingModel ShadingModelFromString(const FString& Model)
{
	if (Model == "Gouraud") return ELightingModel::Gouraud;
	if (Model == "Lambert") return ELightingModel::Lambert;
	if (Model == "Unlit") return ELightingModel::Unlit;
	return ELightingModel::BlinnPhong;
}
}

void UMaterial::SetBlendState(EBlendState InBlendState)
{
	BlendState = InBlendState;
	if (ParamLayout == EMaterialParamLayout::StaticMesh)
	{
		if (BlendState == EBlendState::Opaque)
		{
			DepthStencilState = EDepthStencilState::Default;
		}
		else if (BlendState == EBlendState::AlphaBlend || BlendState == EBlendState::Additive)
		{
			DepthStencilState = EDepthStencilState::ReadOnly;
		}
	}
}

UMaterial* UMaterial::CreateInstance(const UMaterial* Source)
{
	if (Source == nullptr)
	{
		return nullptr;
	}

	UMaterial* Instance = NewObject<UMaterial>();

	Instance->ParamLayout = Source->ParamLayout;
	Instance->Shader = Source->Shader;
	Instance->Textures = Source->Textures;
	Instance->DepthStencilState = Source->DepthStencilState;
	Instance->SetBlendState(Source->BlendState);
	Instance->SamplerState = Source->SamplerState;
	Instance->ShadingModel = Source->ShadingModel;
	Instance->DiffuseOrFactor = Source->DiffuseOrFactor;
	Instance->UVScrollSpeed = Source->UVScrollSpeed;
	Instance->UVTiling = Source->UVTiling;
	Instance->WorldUVTileSize = Source->WorldUVTileSize;
	Instance->bWorldUVLocalSpace = Source->bWorldUVLocalSpace;
	Instance->Shininess = Source->Shininess;
	Instance->SpecularColor = Source->SpecularColor;
	Instance->SpecularFactor = Source->SpecularFactor;
	Instance->EmissiveColor = Source->EmissiveColor;
	Instance->EmissiveFactor = Source->EmissiveFactor;

	// ParamBuffer는 TUniquePtr라 복사할 수 없다.
	// 원본이 갖고 있으면 같은 크기로 새로 만들어 준다. 내용은 매 프레임 갱신되므로 옮기지 않는다.
	if (Source->ParamBuffer)
	{
		Instance->ParamBuffer = RenderCommand::CreateConstantBuffer(Source->ParamBuffer->GetBufferSize());
	}

	Instance->bIsInstance = true;
	Instance->Parent = Source;

	return Instance;
}

void UMaterial::TickUnusedInstanceRelease(float DeltaTime)
{
	if (!bUnusedInstanceReleaseRequested)
	{
		return;
	}

	// 컴포넌트를 계속 지우는 게임에서 매 프레임 GUObjectArray를 훑지 않게 한다.
	constexpr float ReleaseInterval = 1.0f;
	UnusedInstanceReleaseElapsed += DeltaTime;
	if (UnusedInstanceReleaseElapsed < ReleaseInterval)
	{
		return;
	}

	UnusedInstanceReleaseElapsed = 0.0f;
	bUnusedInstanceReleaseRequested = false;
	ReleaseUnusedInstances();
}

void UMaterial::ReleaseUnusedInstances()
{
	TSet<UMaterial*> Referenced;
	for (UObject* Object : GUObjectArray)
	{
		if (UMaterial* Material = Cast<UMaterial>(Object); Material && !Material->GetPath().empty())
		{
			Referenced.Add(Material);
		}
		else if (const UActorComponent* Component = Cast<UActorComponent>(Object))
		{
			Component->AddReferencedMaterials(Referenced);
		}
		else if (const UStaticMesh* Mesh = Cast<UStaticMesh>(Object))
		{
			for (UMaterial* Material : Mesh->Materials)
			{
				Referenced.Add(Material);
			}
		}
		else if (const USkeletalMesh* Mesh = Cast<USkeletalMesh>(Object))
		{
			for (UMaterial* Material : Mesh->Materials) Referenced.Add(Material);
		}
	}

	// 인스턴스의 원본도 GetBaseAsset이 따라가므로 함께 남긴다.
	TSet<const UMaterial*> InUse;
	for (UObject* Object : GUObjectArray)
	{
		UMaterial* Material = Cast<UMaterial>(Object);
		if (!Material || !Referenced.Contains(Material))
		{
			continue;
		}
		for (const UMaterial* Current = Material; Current && InUse.Add(Current); Current = Current->Parent)
		{
		}
	}

	TArray<UMaterial*> Unused;
	for (UObject* Object : GUObjectArray)
	{
		UMaterial* Material = Cast<UMaterial>(Object);
		if (Material && Material->bIsInstance && Material->GetPath().empty() && !InUse.Contains(Material))
		{
			Unused.Add(Material);
		}
	}

	for (UMaterial* Material : Unused)
	{
		delete Material;
	}
}

const UMaterial* UMaterial::GetBaseAsset() const
{
	for (const UMaterial* Current = this; Current; Current = Current->Parent)
	{
		if (!Current->GetPath().empty())
		{
			return Current;
		}
	}
	return nullptr;
}

// 머티리얼 하나를 JSON으로.
// 에셋이면 경로만, 경로 없는 인스턴스면 "기준 에셋 + 텍스처 + 색"으로 풀어서 쓴다
json UMaterial::SaveMaterial(const UMaterial* Material)
{
	json Out;

	if (!Material->GetPath().empty())
	{
		Out["Asset"] = Material->GetPath();
		return Out;
	}

	const UMaterial* Base = Material->GetBaseAsset();
	Out["Base"] = Base ? Base->GetPath() : FString("DefaultMaterial");
	Out["DiffuseOrFactor"] = Material->DiffuseOrFactor;
	Out["SpecularColor"] = Material->SpecularColor;
	Out["SpecularFactor"] = Material->SpecularFactor;
	Out["EmissiveColor"] = Material->EmissiveColor;
	Out["EmissiveFactor"] = Material->EmissiveFactor;
	Out["ShadingModel"] = ShadingModelToString(Material->ShadingModel);
	Out["Shininess"] = Material->Shininess;
	Out["UVScrollSpeed"] = {
		Material->UVScrollSpeed.X,
		Material->UVScrollSpeed.Y
	};
	Out["UVTiling"] = {
		Material->UVTiling.X,
		Material->UVTiling.Y
	};
	Out["WorldUVTileSize"] = {
		Material->WorldUVTileSize.X,
		Material->WorldUVTileSize.Y
	};
	Out["WorldUVLocalSpace"] = Material->bWorldUVLocalSpace;
	Out["SamplerState"] = Material->SamplerState == ESamplerState::LinearWrap ? "LinearWrap" : "LinearClamp";
	Out["BlendState"] = BlendStateToString(Material->BlendState);

	json Textures = json::array();
	for (UTexture2D* Texture : Material->Textures)
	{
		if (Texture && !Texture->GetPath().empty())
		{
			Textures.push_back(Texture->GetPath());
		}
		else
		{
			Textures.push_back(nullptr);
		}
	}
	Out["Textures"] = Textures;

	return Out;
}

UMaterial* UMaterial::LoadMaterial(const json& In)
{
	if (In.contains("Asset"))
	{
		return UAssetManager::GetAssetByPath<UMaterial>(In["Asset"].get<FString>());
	}

	UMaterial* Base = nullptr;
	if (In.contains("Base"))
	{
		Base = UAssetManager::GetAssetByPath<UMaterial>(In["Base"].get<FString>());
	}
	if (!Base)
	{
		Base = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
	}

	UMaterial* Instance = UMaterial::CreateInstance(Base);
	if (!Instance)
	{
		return nullptr;
	}

	if (In.contains("DiffuseOrFactor"))
	{
		In["DiffuseOrFactor"].get_to(Instance->DiffuseOrFactor);
	}
	if (In.contains("SpecularColor")) In["SpecularColor"].get_to(Instance->SpecularColor);
	if (In.contains("SpecularFactor")) In["SpecularFactor"].get_to(Instance->SpecularFactor);
	if (In.contains("EmissiveColor")) In["EmissiveColor"].get_to(Instance->EmissiveColor);
	if (In.contains("EmissiveFactor")) In["EmissiveFactor"].get_to(Instance->EmissiveFactor);
	if (In.contains("ShadingModel"))
	{
		Instance->ShadingModel = ShadingModelFromString(In["ShadingModel"].get<FString>());
	}

	if (In.contains("Shininess"))
	{
		In["Shininess"].get_to(Instance->Shininess);
	}

	if (In.contains("Textures"))
	{
		Instance->Textures.Reset();
		for (const json& TexturePath : In["Textures"])
		{
			Instance->Textures.Add(TexturePath.is_null() ? nullptr : FindOrLoadTexture(TexturePath.get<FString>()));
		}
	}
	if (In.contains("UVScrollSpeed"))
	{
		const auto& UV = In["UVScrollSpeed"];
		Instance->UVScrollSpeed = FVector2(UV[0].get<float>(), UV[1].get<float>());
	}
	if (In.contains("UVTiling"))
	{
		const auto& Tiling = In["UVTiling"];
		Instance->UVTiling = FVector2(Tiling[0].get<float>(), Tiling[1].get<float>());
	}
	if (In.contains("WorldUVTileSize"))
	{
		const auto& Tile = In["WorldUVTileSize"];
		Instance->WorldUVTileSize = FVector2(Tile[0].get<float>(), Tile[1].get<float>());
	}
	if (In.contains("WorldUVLocalSpace"))
	{
		Instance->bWorldUVLocalSpace = In["WorldUVLocalSpace"].get<bool>();
	}
	if (In.contains("SamplerState"))
	{
		const FString State = In["SamplerState"].get<FString>();
		Instance->SamplerState = State == "LinearWrap" ? ESamplerState::LinearWrap : ESamplerState::LinearClamp;
	}
	if (In.contains("BlendState"))
	{
		const FString State = In["BlendState"].get<FString>();
		Instance->SetBlendState(BlendStateFromString(State));
	}

	return Instance;
}

// Material.cpp
void UMaterial::SerializeMaterialReference(FStructuredArchive::FSlot Slot, UMaterial*& Material)
{
	FArchive& Ar = Slot.GetUnderlyingArchive();
	FStructuredArchive::FRecord Record = Slot.EnterRecord();

	// ── 에셋 참조 ──
	// 저장과 불러오기가 같은 순서로 같은 함수를 부르게 TryEnterField를 쓴다 (바이너리 포맷에서도 맞도록)
	const bool bIsAsset = Ar.IsSaving() && Material && !Material->GetPath().empty();
	if (TOptional<FStructuredArchive::FSlot> AssetSlot = Record.TryEnterField("Asset", bIsAsset))
	{
		FString Path = Ar.IsSaving() ? Material->GetPath() : FString();
		AssetSlot.GetValue() << Path;
		if (Ar.IsLoading())
		{
			Material = UAssetManager::GetAssetByPath<UMaterial>(Path);
		}
		return;
	}

	// ── 인스턴스 ──
	const bool bIsInstance = Ar.IsSaving() && Material != nullptr;
	TOptional<FStructuredArchive::FSlot> BaseSlot = Record.TryEnterField("Base", bIsInstance);
	if (!BaseSlot.IsSet())
	{
		if (Ar.IsLoading())
		{
			Material = nullptr; // 빈 Record = 머티리얼 없음
		}
		return;
	}

	FString BasePath;
	if (Ar.IsSaving())
	{
		const UMaterial* Base = Material->GetBaseAsset();
		BasePath = Base ? Base->GetPath() : FString("DefaultMaterial");
	}
	BaseSlot.GetValue() << BasePath;

	if (Ar.IsLoading())
	{
		UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>(BasePath);
		if (!Base)
		{
			HTR_LOG(Warning, "Load: base material '{}' not found, using DefaultMaterial", BasePath);
			Base = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
		}
		Material = CreateInstance(Base); // 먼저 만들고
	}

	if (Material)
	{
		Material->Serialize(Record.EnterRecord("Instance")); // 그다음 값을 채운다
	}
}

void UMaterial::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record); // "Properties" (리플렉션 프로퍼티. 지금은 없음)

	FArchive& Ar = Record.GetUnderlyingArchive();

	Record << SA_VALUE("DiffuseOrFactor", DiffuseOrFactor);
	Record << SA_VALUE("Shininess", Shininess)
		   << SA_VALUE("UVScrollSpeed", UVScrollSpeed);

	if (TOptional<FStructuredArchive::FSlot> ModelSlot = Record.TryEnterField("ShadingModel", Ar.IsSaving()))
	{
		FString Model = ShadingModelToString(ShadingModel);
		ModelSlot.GetValue() << Model;
		if (Ar.IsLoading())
		{
			ShadingModel = ShadingModelFromString(Model);
		}
	}

	// 기본값 (1, 1)은 생략한다.
	const bool bCustomTiling = UVTiling.X != 1.0f || UVTiling.Y != 1.0f;
	if (TOptional<FStructuredArchive::FSlot> TilingSlot = Record.TryEnterField("UVTiling", Ar.IsSaving() && bCustomTiling))
	{
		TilingSlot.GetValue() << UVTiling;
	}

	if (TOptional<FStructuredArchive::FSlot> TileSlot = Record.TryEnterField("WorldUVTile", Ar.IsSaving() && WorldUVTileSize.X > 0.0f))
	{
		TileSlot.GetValue() << WorldUVTileSize;
	}
	// 기본값(월드 축)이면 저장하지 않는다.
	if (TOptional<FStructuredArchive::FSlot> SpaceSlot = Record.TryEnterField("WorldUVLocalSpace", Ar.IsSaving() && bWorldUVLocalSpace))
	{
		SpaceSlot.GetValue() << bWorldUVLocalSpace;
	}

	// enum은 이름 문자열로 (UE 텍스트 포맷도 enum을 이름으로 쓴다)
	FString Sampler = SamplerState == ESamplerState::LinearWrap ? "LinearWrap" : "LinearClamp";
	Record << SA_VALUE("SamplerState", Sampler);
	if (Ar.IsLoading())
	{
		SamplerState = Sampler == "LinearWrap" ? ESamplerState::LinearWrap : ESamplerState::LinearClamp;
	}

	FString Blend = BlendStateToString(BlendState);
	Record << SA_VALUE("BlendState", Blend);
	if (Ar.IsLoading())
	{
		SetBlendState(BlendStateFromString(Blend));
	}

	// 텍스처: 경로 문자열 배열. 없는 칸은 빈 문자열
	int32 Num = Textures.Num();
	FStructuredArchive::FArray TextureArray = Record.EnterArray("Textures", Num);
	if (Ar.IsLoading())
	{
		Textures.Reset();
	}

	for (int32 i = 0; i < Num; ++i)
	{
		FString Path;
		if (Ar.IsSaving() && Textures[i])
		{
			Path = Textures[i]->GetPath();
		}

		TextureArray.EnterElement() << Path;

		if (Ar.IsLoading())
		{
			Textures.Add(Path.empty() ? nullptr : FindOrLoadTexture(Path));
		}
	}

	if (auto ValueSlot = Record.TryEnterField("SpecularColor", Ar.IsSaving())) ValueSlot.GetValue() << SpecularColor;
	if (auto ValueSlot = Record.TryEnterField("SpecularFactor", Ar.IsSaving())) ValueSlot.GetValue() << SpecularFactor;
	if (auto ValueSlot = Record.TryEnterField("EmissiveColor", Ar.IsSaving())) ValueSlot.GetValue() << EmissiveColor;
	if (auto ValueSlot = Record.TryEnterField("EmissiveFactor", Ar.IsSaving())) ValueSlot.GetValue() << EmissiveFactor;
}
