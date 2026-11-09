#include "EnginePCH.h"
#include "FbxAssetLoader.h"
#include "FbxAssetBake.h"
#include "FbxImporter.h"
#include "FbxPath.h"
#include "Asset/AssetManager.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/RenderCommand.h"
#include "Render/Texture2D.h"
#include <chrono>
#include <functional>
#include <map>
#include <set>

namespace
{
// 아직 등록하지 않은 의존 애셋도 같은 배치에서 찾는다. 실패 시 기존 검색표는 그대로 유지된다.
class FLoadBatch
{
public:
	std::map<FString, FFbxAssetData> Data;
	std::map<FString, URenderAsset*> Objects;
	std::set<FString> Loading;
	std::vector<URenderAsset*> Created;
	FString Error;
	bool bPublished = false;
	bool bPrepared = false;
	TMap<FString, URenderAsset*> PreparedRegistry;
	bool bAllowDiskDependencies = true;
	std::function<URenderAsset*(const FImageData&, FString&)> TextureFactory;

	~FLoadBatch()
	{
		if (!bPublished)
			for (auto It = Created.rbegin(); It != Created.rend(); ++It) delete *It;
	}

	template<class T> T* Allocate()
	{
		T* Object = NewObject<T>();
		if (Object) Created.push_back(Object);
		return Object;
	}

	URenderAsset* Resolve(const FString& Key)
	{
		if (auto It = Objects.find(Key); It != Objects.end()) return It->second;
		if (!Data.contains(Key))
		{
			if (auto* Existing = UAssetManager::GetAssetByPath<URenderAsset>(Key)) return Existing;
			if (!bAllowDiskDependencies) { Error = "Missing imported dependency: " + Key; return nullptr; }
			fs::path Path;
			if (!FbxPath::ResolveKey(Key, Path, Error)) return nullptr;
			FFbxAssetData Asset;
			if (Path.extension() != ".fbxbin" || !FFbxAssetBake::ReadBaked(FbxPath::ToString(Path), Asset, Error)) return nullptr;
			Asset.Key = Key; Data.emplace(Key, std::move(Asset));
		}
		if (!Loading.insert(Key).second) { Error = "Cyclic FBX asset reference: " + Key; return nullptr; }
		const auto& Asset = Data.at(Key);
		URenderAsset* Object = nullptr;
		if (Asset.Type == EFbxAssetType::Texture2D)
		{
			Object = TextureFactory(Asset.Texture, Error);
			if (Object) Created.push_back(Object);
		}
		else if (Asset.Type == EFbxAssetType::Skeleton)
		{
			auto* Skeleton = Allocate<USkeleton>();
			Skeleton->RefSkeleton = Asset.RefSkeleton; Object = Skeleton;
		}
		else if (Asset.Type == EFbxAssetType::AnimSequence)
		{
			auto* Skeleton = Cast<USkeleton>(Resolve(Asset.SkeletonKey));
			if (!Skeleton) { if (Error.empty()) Error = "Invalid FBX skeleton reference"; return nullptr; }
			for (const auto& Track : Asset.Animation.Tracks)
			{
				if (Track.BoneTreeIndex >= Skeleton->RefSkeleton.GetNum() || Skeleton->RefSkeleton.RefBoneInfo[Track.BoneTreeIndex].Name != Track.Name)
				{ Error = "Animation track does not match skeleton: " + Track.Name; return nullptr; }
			}
			auto* Sequence = Allocate<UAnimSequence>(); Object = Sequence;
			Sequence->SetSkeleton(Skeleton);
			Sequence->DataModel = NewObject<UAnimDataModel>();
			Sequence->DataModel->BoneAnimationTracks = Asset.Animation.Tracks;
			Sequence->DataModel->FrameRate = Asset.Animation.FrameRate;
			Sequence->DataModel->NumberOfFrames = Asset.Animation.NumberOfFrames;
			Sequence->DataModel->NumberOfKeys = Asset.Animation.NumberOfKeys;
			json NotifyData = json::parse(Asset.Animation.NotifyData);
			Sequence->Serialize(NotifyData, true);
		}
		else if (Asset.Type == EFbxAssetType::Material)
		{
			auto* Base = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
			if (!Base) { Error = "DefaultMaterial must be initialized before loading FBX materials"; return nullptr; }
			auto* Material = UMaterial::CreateInstance(Base);
			if (!Material) { Error = "Cannot create FBX material"; return nullptr; }
			Created.push_back(Material); Object = Material;
			if ((Base->ParamBuffer && !Material->ParamBuffer) || Material->Textures.Num() < 2)
			{ Error = "Cannot initialize FBX material resources"; return nullptr; }
			Material->DiffuseOrFactor = Asset.Material.DiffuseOrFactor;
			Material->Shininess = Asset.Material.Shininess;
			Material->SpecularColor = Asset.Material.SpecularColor;
			Material->SpecularFactor = Asset.Material.SpecularFactor;
			Material->EmissiveColor = Asset.Material.EmissiveColor;
			Material->EmissiveFactor = Asset.Material.EmissiveFactor;
			Material->ShadingModel = Asset.Material.ShadingModel;
			Material->SamplerState = Asset.Material.SamplerState;
			Material->UVTiling = FVector2(1, 1);
			Material->UVScrollSpeed = FVector2(0, 0);
			Material->WorldUVTileSize = FVector2(0, 0);
			Material->SetBlendState(Asset.Material.BlendState);
			const FString Keys[]{ Asset.Material.DiffuseTexture, Asset.Material.NormalTexture };
			for (int I = 0; I < 2; ++I) if (!Keys[I].empty())
			{
				auto* Texture = Cast<UTexture2D>(Resolve(Keys[I]));
				if (!Texture) { if (Error.empty()) Error = "Invalid FBX texture reference: " + Keys[I]; return nullptr; }
				Material->Textures[I] = Texture;
			}
		}
		else
		{
			TArray<UMaterial*> Materials;
			for (const auto& MaterialKey : Asset.MaterialKeys)
			{
				auto* Material = Cast<UMaterial>(Resolve(MaterialKey));
				if (!Material) { if (Error.empty()) Error = "Invalid FBX material reference: " + MaterialKey; return nullptr; }
				Materials.Add(Material);
			}
			if (Asset.Type == EFbxAssetType::StaticMesh)
			{
				auto* Mesh = Allocate<UStaticMesh>(); Object = Mesh;
				Mesh->Materials = std::move(Materials); Mesh->ScreenThresholds = Asset.ScreenThresholds;
				for (int32 L = 0; L < Asset.LODs.Num(); ++L)
				{
					FStaticMeshLODResource Resource;
					Resource.Data = Asset.LODs[L].MeshData;
					// Cook에서 완성한 UV와 탄젠트를 사용한다.
					Resource.Data.OptimizeTriangleOrderForVertexCache();
					if (!Resource.Data.Indices.IsEmpty())
					{
						Resource.VertexBuffer = RenderCommand::CreateStaticVertexBuffer(Resource.Data.Vertices.GetData(),
							static_cast<uint32>(Resource.Data.Vertices.Num() * sizeof(FVertex)), sizeof(FVertex));
						Resource.IndexBuffer = RenderCommand::CreateStaticIndexBuffer(Resource.Data.Indices.GetData(), Resource.Data.Indices.Num());
						if (!Resource.VertexBuffer || !Resource.IndexBuffer) { Error = "Cannot create FBX mesh GPU buffers"; return nullptr; }
					}
					Resource.Data.BuildTriangleBVH();
					if (L == 0)
					{
						Mesh->MeshData = std::move(Resource.Data); Mesh->VertexBuffer = std::move(Resource.VertexBuffer);
						Mesh->IndexBuffer = std::move(Resource.IndexBuffer);
					}
					else Mesh->AdditionalLODs.Add(std::move(Resource));
				}
				Mesh->RebuildRenderData();
			}
			else if (Asset.Type == EFbxAssetType::SkeletalMesh)
			{
				auto* Skeleton = Cast<USkeleton>(Resolve(Asset.SkeletonKey));
				if (!Skeleton) { if (Error.empty()) Error = "Invalid FBX skeleton reference"; return nullptr; }
				auto* Mesh = Allocate<USkeletalMesh>(); Object = Mesh;
				Mesh->Skeleton = Skeleton; Mesh->RefSkeleton = Asset.RefSkeleton;
				Mesh->Materials = std::move(Materials); Mesh->ScreenThresholds = Asset.ScreenThresholds;
				for (int32 L = 0; L < Asset.LODs.Num(); ++L)
				{
					FSkeletalMeshLODResource Resource{ Asset.LODs[L].MeshData, Asset.LODs[L].SkinWeights };
					// 정점 배열은 변경하지 않는다. 본 영향 배열은 이 인덱스와 일치해야 한다.
					Resource.Data.OptimizeTriangleOrderForVertexCache();
					// 각 LOD의 기준 기하로 만든다. 컴포넌트별 스키닝 결과는 반영하지 않는다.
					Resource.Data.BuildTriangleBVH();
					if (L == 0) { Mesh->MeshData = std::move(Resource.Data); Mesh->SkinWeights = std::move(Resource.SkinWeights); }
					else Mesh->AdditionalLODs.Add(std::move(Resource));
				}
				if (!Mesh->CalculateInvRefMatrices(Error)) return nullptr;
				Mesh->InitResources();
			}
		}
		Loading.erase(Key);
		if (!Object) { if (Error.empty()) Error = "Cannot create FBX asset: " + Key; return nullptr; }
		Object->SetPath(Key); Objects.emplace(Key, Object);
		return Object;
	}

	void PreparePublish()
	{
		TMap<FString, URenderAsset*> Replacements;
		for (const auto& [Key, Object] : Objects)
			Replacements.Add(Key, Object);
		UAssetManager::Get().PrepareAssetRegistration(Replacements, PreparedRegistry);
		bPrepared = true;
	}
	void Publish()
	{
		if (!bPrepared) PreparePublish();
		UAssetManager::Get().CommitAssetRegistration(PreparedRegistry);
		bPublished = true;
	}
};

// 임시 파일은 Assets 안의 이번 임포트 전용 폴더에만 만든다.
struct FImportFolder
{
	fs::path Path;
	~FImportFolder()
	{
		FString Key, Error;
		if (!Path.empty() && Path.filename().wstring().starts_with(L".fbx-import-") &&
			FbxPath::GetAssetKey(FbxPath::ToString(Path), Key, Error, false))
		{
			std::error_code Ignored; fs::remove_all(Path, Ignored);
		}
	}
};
}

URenderAsset* FFbxAssetLoader::CreateTexture(const FImageData& Image, FString& Error)
{
	D3D11_TEXTURE2D_DESC Desc{};
	const bool GenerateMips = Image.BytesPerPixel != 0;
	Desc.Width = Image.Width; Desc.Height = Image.Height; Desc.MipLevels = GenerateMips ? 0 : 1; Desc.ArraySize = 1;
	Desc.Format = Image.Format; Desc.SampleDesc.Count = 1; Desc.Usage = GenerateMips ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
	Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (GenerateMips ? D3D11_BIND_RENDER_TARGET : 0);
	Desc.MiscFlags = GenerateMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
	auto Resource = RenderCommand::CreateTexture2D(Desc, Image);
	if (!Resource || !Resource->GetSRV()) { Error = "Cannot create FBX texture GPU resource"; return nullptr; }
	auto* Object = NewObject<UTexture2D>(); Object->SetResource(std::move(Resource)); return Object;
}

URenderAsset* FFbxAssetLoader::LoadAsset(const FString& Path, FString& Error)
{
	try
	{
		FString Key;
		if (!FbxPath::GetAssetKey(Path, Key, Error)) return nullptr;
		if (auto* Existing = UAssetManager::GetAssetByPath<URenderAsset>(Key)) return Existing;
		FLoadBatch Batch; Batch.TextureFactory = CreateTexture;
		auto* Object = Batch.Resolve(Key);
		if (!Object) { Error = Batch.Error; return nullptr; }
		Batch.Publish(); return Object;
	}
	catch (const std::exception& E) { Error = E.what(); return nullptr; }
}

FFbxImportResult FFbxAssetLoader::Import(const FString& SourcePath, const FFbxImportOptions& Options, const FString& OutputDirectory)
{
	FFbxImportResult Result;
	try
	{
		FString SourceKey;
		fs::path Source, Destination;
		if (!FbxPath::GetSourcePath(SourcePath, Source, SourceKey, Result.Error) ||
			!FbxPath::GetOutputDirectory(OutputDirectory.empty() ? FbxPath::DefaultOutputDirectory(SourcePath) : OutputDirectory, Destination, Result.Error)) return Result;
		fs::create_directories(Destination);
		FImportFolder Folder;
		for (uint32 I = 0; I < 100; ++I)
		{
			Folder.Path = Destination / (".fbx-import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(I));
			if (fs::create_directory(Folder.Path)) break;
			Folder.Path.clear();
		}
		if (Folder.Path.empty()) { Result.Error = "Cannot create FBX staging directory"; return Result; }
		TArray<FFbxAssetData> Cooked;
		if (!FFbxImporter::Cook(FbxPath::ToString(Source), Options, FbxPath::ToString(Folder.Path), Cooked, Result.Warnings, Result.Error,
			FbxPath::ToString(Destination))) return Result;
		FLoadBatch Batch; Batch.TextureFactory = CreateTexture; Batch.bAllowDiskDependencies = false;
		struct FReplacement { fs::path Target, Staged, Backup; bool bBackedUp = false, bInstalled = false; };
		std::vector<FReplacement> Files;
		TArray<FString> OutputKeys;
		for (auto& Data : Cooked)
		{
			fs::path Target;
			if (!FbxPath::ResolveKey(Data.Key, Target, Result.Error)) return Result;
			if (!FFbxAssetBake::PreserveAnimationNotifies(FbxPath::ToString(Target), Data, Result.Error)) return Result;
			FReplacement File{ Target, Folder.Path / (std::to_string(Files.size()) + ".new"), Folder.Path / (std::to_string(Files.size()) + ".old") };
			if (!FFbxAssetBake::WriteBaked(FbxPath::ToString(File.Staged), Data, Result.Error)) return Result;
			FFbxAssetData Verified;
			if (!FFbxAssetBake::ReadBaked(FbxPath::ToString(File.Staged), Verified, Result.Error)) return Result;
			Verified.Key = Data.Key;
			if (!Batch.Data.emplace(Data.Key, std::move(Verified)).second) { Result.Error = "Duplicate FBX output key"; return Result; }
			Files.push_back(std::move(File)); OutputKeys.Add(Data.Key);
		}
		for (const auto& Key : OutputKeys)
			if (!Batch.Resolve(Key)) { Result.Error = Batch.Error; return Result; }
		Batch.PreparePublish();
		// 모든 데이터/리소스가 준비된 뒤 파일을 교체한다. 중간 실패 시 백업을 되돌린다.
		try
		{
			for (auto& File : Files)
			{
				if (fs::exists(File.Target)) { fs::rename(File.Target, File.Backup); File.bBackedUp = true; }
				fs::rename(File.Staged, File.Target); File.bInstalled = true;
			}
		}
		catch (const std::exception& E)
		{
			Result.Error = FString("Cannot replace FBX outputs: ") + E.what();
			bool Restored = true;
			for (auto It = Files.rbegin(); It != Files.rend(); ++It)
			{
				std::error_code Ec;
				if (It->bInstalled) fs::remove(It->Target, Ec);
				if (Ec) Restored = false;
				Ec.clear();
				if (It->bBackedUp) fs::rename(It->Backup, It->Target, Ec);
				if (Ec) Restored = false;
			}
			// 복원까지 실패한 경우 복구용 백업을 지우지 않는다.
			if (!Restored) { Result.Error += "; backups retained at " + FbxPath::ToString(Folder.Path); Folder.Path.clear(); }
			return Result;
		}
		Batch.Publish(); Result.AssetKeys = std::move(OutputKeys); Result.bSuccess = true;
	}
	catch (const std::exception& E) { Result.Error = E.what(); }
	return Result;
}


bool FFbxAssetLoader::SaveAnimationAsset(UAnimSequenceBase* Sequence, FString& Error)
{
	fs::path Temporary;
	try
	{
		if (!Sequence || Sequence->GetPath().empty() || Sequence->IsRetired()) { Error = "Animation Asset has no path or is retired"; return false; }
		fs::path Target;
		if (!FbxPath::ResolveKey(Sequence->GetPath(), Target, Error)) return false;
		FFbxAssetData Data;
		if (!FFbxAssetBake::ReadBaked(FbxPath::ToString(Target), Data, Error)) return false;
		if (Data.Type != EFbxAssetType::AnimSequence) { Error = "Asset is not an Animation Sequence"; return false; }
		json NotifyData = json::object();
		Sequence->Serialize(NotifyData, false);
		Data.Animation.NotifyData = NotifyData.dump();
		Temporary = Target; Temporary += ".notify-save.tmp";
		if (!FFbxAssetBake::WriteBaked(FbxPath::ToString(Temporary), Data, Error))
		{
			std::error_code Ignored; fs::remove(Temporary, Ignored); return false;
		}
		// 임시 파일을 완성한 뒤 교체하여 저장 실패로 원래 Asset을 잘라 쓰지 않는다.
		if (!MoveFileExW(Temporary.c_str(), Target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			Error = "Cannot replace Animation Asset: " + std::to_string(GetLastError());
			std::error_code Ignored; fs::remove(Temporary, Ignored); return false;
		}
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		if (!Temporary.empty()) { std::error_code Ignored; fs::remove(Temporary, Ignored); }
		return false;
	}
}
