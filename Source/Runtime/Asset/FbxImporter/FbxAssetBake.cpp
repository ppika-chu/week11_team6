#include "EnginePCH.h"
#include "FbxAssetBake.h"
#include "FbxPath.h"
#include "Render/Texture.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <type_traits>

namespace
{
// 헤더나 직렬화 필드를 변경하면 올린다. 구버전은 에디터에서 명시적으로 재임포트한다.
constexpr uint32 FbxBakeVersion = 6;
// 6: 애니메이션 Root Motion 설정 추가. 5는 그 필드만 없으므로 기본값으로 읽는다
constexpr uint32 FbxBakeMinReadVersion = 5;
constexpr uint32 MaxElements = 1000000;
constexpr uint32 MaxImageBytes = 512u * 1024u * 1024u;
struct FFbxBakeHeader
{
	char Magic[4]{ 'F', 'B', 'X', 'B' };
	uint32 Version = FbxBakeVersion;
	uint32 Type = 0;
};

// 고정 크기 필드·배열·문자열을 순서대로 직렬화한다.
class FBinary
{
public:
	std::istream* In = nullptr;
	std::ostream* Out = nullptr;
	bool bGood = true;
	uint32 Version = FbxBakeVersion; // 읽을 때는 파일 헤더의 버전

	template<class T> void Pod(T& Value)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		if (In) bGood = bGood && static_cast<bool>(In->read(reinterpret_cast<char*>(&Value), sizeof(T)));
		else bGood = bGood && static_cast<bool>(Out->write(reinterpret_cast<const char*>(&Value), sizeof(T)));
	}
	void String(FString& Value)
	{
		uint32 Size = static_cast<uint32>(Value.size());
		Pod(Size);
		if (!bGood || Size > 32768) { bGood = false; return; }
		if (In) { Value.resize(Size); bGood = static_cast<bool>(In->read(Value.data(), Size)); }
		else bGood = static_cast<bool>(Out->write(Value.data(), Size));
	}
	template<class T> void Array(TArray<T>& Values, uint32 Limit = MaxElements)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		uint32 Size = static_cast<uint32>(Values.Num());
		Pod(Size);
		if (!bGood || Size > Limit) { bGood = false; return; }
		const uint64 Bytes = static_cast<uint64>(Size) * sizeof(T);
		if (In)
		{
			// 잘린 파일의 거짓 개수로 큰 메모리를 할당하지 않는다.
			const auto Pos = In->tellg();
			In->seekg(0, std::ios::end);
			const auto End = In->tellg();
			In->seekg(Pos);
			if (Pos < 0 || End < Pos || Bytes > static_cast<uint64>(End - Pos)) { bGood = false; return; }
			Values.SetNum(static_cast<int32>(Size));
			bGood = static_cast<bool>(In->read(reinterpret_cast<char*>(Values.GetData()), Bytes));
		}
		else bGood = static_cast<bool>(Out->write(reinterpret_cast<const char*>(Values.GetData()), Bytes));
	}
};

void Skeleton(FBinary& Ar, FReferenceSkeleton& Ref)
{
	uint32 Count = static_cast<uint32>(Ref.GetNum());
	Ar.Pod(Count);
	if (!Ar.bGood || Count > 65536) { Ar.bGood = false; return; }
	if (Ar.In) { Ref.RefBoneInfo.SetNum(Count); Ref.RefBonePose.SetNum(Count); }
	for (uint32 I = 0; I < Count && Ar.bGood; ++I)
	{
		Ar.String(Ref.RefBoneInfo[I].Name);
		Ar.Pod(Ref.RefBoneInfo[I].ParentIndex);
		auto& T = Ref.RefBonePose[I];
		Ar.Pod(T.Location);
		Ar.Pod(T.Rotation.X); Ar.Pod(T.Rotation.Y); Ar.Pod(T.Rotation.Z); Ar.Pod(T.Rotation.W);
		Ar.Pod(T.Scale);
	}
}

void Serialize(FBinary& Ar, FFbxAssetData& Data)
{
	Ar.String(Data.SourcePath); Ar.String(Data.SourceObject);
	uint8 Flags = (Data.Options.bBakeStaticMeshes ? 1 : 0) | (Data.Options.bCombineStaticMeshes ? 2 : 0) |
		(Data.Options.bBakeSkeletalMeshes ? 4 : 0) | (Data.Options.bCombineSkeletalMeshes ? 8 : 0) |
		(Data.Options.bKeepFbxNamespace ? 16 : 0);
	Ar.Pod(Flags);
	if (Ar.In)
	{
		Data.Options = { (Flags & 1) != 0, (Flags & 2) != 0, (Flags & 4) != 0, (Flags & 8) != 0 };
		Data.Options.bKeepFbxNamespace = (Flags & 16) != 0;
	}
	if (Data.Type == EFbxAssetType::StaticMesh || Data.Type == EFbxAssetType::SkeletalMesh)
	{
		Ar.Pod(Data.ScreenThresholds);
		uint32 Slots = Data.MaterialKeys.Num(); Ar.Pod(Slots);
		if (!Ar.bGood || Slots > 65536) { Ar.bGood = false; return; }
		if (Ar.In) Data.MaterialKeys.SetNum(Slots);
		for (auto& Key : Data.MaterialKeys) Ar.String(Key);
		uint32 LODCount = Data.LODs.Num(); Ar.Pod(LODCount);
		if (!Ar.bGood || LODCount == 0 || LODCount > 4) { Ar.bGood = false; return; }
		if (Ar.In) Data.LODs.SetNum(LODCount);
		for (auto& LOD : Data.LODs)
		{
			Ar.Array(LOD.MeshData.Vertices); Ar.Array(LOD.MeshData.Indices, MaxElements * 6);
			Ar.Array(LOD.MeshData.LODSourceVertices);
			Ar.Array(LOD.MeshData.Sections, 65536); Ar.Pod(LOD.MeshData.AABB);
			for (uint32 I = 0; I < Slots; ++I)
			{
				if (Ar.In) LOD.MeshData.MaterialSlots.Add(FStaticMaterialSlot{});
				Ar.String(LOD.MeshData.MaterialSlots[I].Name);
			}
			if (Data.Type == EFbxAssetType::SkeletalMesh)
			{
				Ar.Array(LOD.SkinWeights.InfluenceBones); Ar.Array(LOD.SkinWeights.InfluenceWeights);
			}
		}
		if (Data.Type == EFbxAssetType::SkeletalMesh) { Ar.String(Data.SkeletonKey); Skeleton(Ar, Data.RefSkeleton); }
	}
	else if (Data.Type == EFbxAssetType::Skeleton) Skeleton(Ar, Data.RefSkeleton);
	else if (Data.Type == EFbxAssetType::Material)
	{
		Ar.Pod(Data.Material.DiffuseOrFactor); Ar.Pod(Data.Material.Shininess);
		Ar.Pod(Data.Material.SpecularColor); Ar.Pod(Data.Material.SpecularFactor);
		Ar.Pod(Data.Material.EmissiveColor); Ar.Pod(Data.Material.EmissiveFactor);
		uint32 Model = static_cast<uint32>(Data.Material.ShadingModel); Ar.Pod(Model);
		if (Model > static_cast<uint32>(ELightingModel::Unlit)) { Ar.bGood = false; return; }
		Data.Material.ShadingModel = static_cast<ELightingModel>(Model);
		uint32 Sampler = static_cast<uint32>(Data.Material.SamplerState); Ar.Pod(Sampler);
		if (Sampler > static_cast<uint32>(ESamplerState::LinearWrap)) { Ar.bGood = false; return; }
		Data.Material.SamplerState = static_cast<ESamplerState>(Sampler);
		uint32 Blend = static_cast<uint32>(Data.Material.BlendState); Ar.Pod(Blend);
		if (Blend > static_cast<uint32>(EBlendState::AlphaBlend)) { Ar.bGood = false; return; }
		Data.Material.BlendState = static_cast<EBlendState>(Blend);
		Ar.String(Data.Material.DiffuseTexture); Ar.String(Data.Material.NormalTexture);
	}
	else if (Data.Type == EFbxAssetType::AnimSequence)
	{
		Ar.String(Data.SkeletonKey); Ar.String(Data.Animation.Name);
		Ar.Pod(Data.Animation.FrameRate); Ar.Pod(Data.Animation.NumberOfFrames); Ar.Pod(Data.Animation.NumberOfKeys);
		uint32 TrackCount = Data.Animation.Tracks.Num(); Ar.Pod(TrackCount);
		if (!Ar.bGood || TrackCount > 65536) { Ar.bGood = false; return; }
		if (Ar.In) Data.Animation.Tracks.SetNum(TrackCount);
		for (auto& Track : Data.Animation.Tracks)
		{
			Ar.String(Track.Name); Ar.Pod(Track.BoneTreeIndex);
			Ar.Array(Track.InternalTrackData.PosKeys); Ar.Array(Track.InternalTrackData.RotKeys); Ar.Array(Track.InternalTrackData.ScaleKeys);
		}
		if (Ar.Version >= 6)
		{
			uint8 RootMotionFlags = (Data.Animation.bEnableRootMotion ? 1 : 0) | (Data.Animation.bForceRootLock ? 2 : 0);
			Ar.Pod(RootMotionFlags); Ar.Pod(Data.Animation.RootMotionRootLock);
			if (Ar.In)
			{
				Data.Animation.bEnableRootMotion = (RootMotionFlags & 1) != 0;
				Data.Animation.bForceRootLock = (RootMotionFlags & 2) != 0;
			}
			if (Data.Animation.RootMotionRootLock < 0 || Data.Animation.RootMotionRootLock > 2) { Ar.bGood = false; return; }
		}
	}
	else if (Data.Type == EFbxAssetType::Texture2D)
	{
		Ar.Pod(Data.Texture.Width); Ar.Pod(Data.Texture.Height); Ar.Pod(Data.Texture.BytesPerPixel);
		Ar.Pod(Data.Texture.RowPitch); Ar.Pod(Data.Texture.Format); Ar.Array(Data.Texture.Pixels, MaxImageBytes);
	}
}

bool Finite(const FVector& V) { return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z); }
bool Finite(const FVector4& V) { return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z) && std::isfinite(V.W); }
}

bool FFbxAssetData::Validate(FString& Error) const
{
	if (Type == EFbxAssetType::StaticMesh || Type == EFbxAssetType::SkeletalMesh)
	{
		if (LODs.IsEmpty() || LODs.Num() > 4 || LODs[0].MeshData.Vertices.IsEmpty() || MaterialKeys.IsEmpty())
		{ Error = "Missing base geometry or material slots"; return false; }
		if (!std::isfinite(ScreenThresholds[0]) || !std::isfinite(ScreenThresholds[1]) || !std::isfinite(ScreenThresholds[2]) ||
			!(ScreenThresholds[0] > ScreenThresholds[1] && ScreenThresholds[1] > ScreenThresholds[2] && ScreenThresholds[2] > 0))
		{ Error = "LOD thresholds must be positive and descending"; return false; }
		if (Type == EFbxAssetType::SkeletalMesh)
		{
			if (SkeletonKey.empty()) { Error = "Missing Skeleton asset reference"; return false; }
			if (!RefSkeleton.Validate(Error)) return false;
		}
		for (const auto& LOD : LODs)
		{
			const auto& Mesh = LOD.MeshData;
			if (!Finite(Mesh.AABB.Min) || !Finite(Mesh.AABB.Max) || Mesh.AABB.Min.X > Mesh.AABB.Max.X ||
				Mesh.AABB.Min.Y > Mesh.AABB.Max.Y || Mesh.AABB.Min.Z > Mesh.AABB.Max.Z) { Error = "Invalid mesh bounds"; return false; }
			if (Mesh.Vertices.Num() > MaxElements || Mesh.Indices.Num() > MaxElements * 6 || Mesh.Indices.Num() % 3 ||
				(!Mesh.LODSourceVertices.IsEmpty() && Mesh.LODSourceVertices.Num() != Mesh.Vertices.Num()) ||
				Mesh.MaterialSlots.Num() != MaterialKeys.Num()) { Error = "Invalid mesh array size"; return false; }
			for (const auto& V : Mesh.Vertices)
				if (!Finite(V.Position) || !Finite(V.Normal) || !Finite(V.Tangent) || !Finite(V.Color) || !std::isfinite(V.UV.X) || !std::isfinite(V.UV.Y))
				{ Error = "Non-finite vertex"; return false; }
			for (uint32 I : Mesh.Indices) if (I >= static_cast<uint32>(Mesh.Vertices.Num())) { Error = "Invalid triangle index"; return false; }
			uint64 End = 0;
			for (const auto& S : Mesh.Sections)
			{
				if (S.StartIndex != End || S.IndexCount % 3 || S.MaterialSlotIndex >= static_cast<uint32>(MaterialKeys.Num()))
				{ Error = "Invalid section or material index"; return false; }
				End += S.IndexCount;
			}
			if (End != static_cast<uint64>(Mesh.Indices.Num())) { Error = "Sections must cover all triangles"; return false; }
			if (Type == EFbxAssetType::SkeletalMesh && !LOD.SkinWeights.Validate(Mesh.Vertices.Num(), RefSkeleton.GetNum(), Error)) return false;
		}
	}
	else if (Type == EFbxAssetType::Skeleton) return RefSkeleton.Validate(Error);
	else if (Type == EFbxAssetType::Material)
	{
		const auto& C = Material.DiffuseOrFactor;
		if (!std::isfinite(C.X) || !std::isfinite(C.Y) || !std::isfinite(C.Z) || !std::isfinite(C.W) || !std::isfinite(Material.Shininess))
		{ Error = "Non-finite material"; return false; }
		for (const FVector Color : { Material.SpecularColor, Material.EmissiveColor })
		{
			if (!std::isfinite(Color.X) || !std::isfinite(Color.Y) || !std::isfinite(Color.Z))
			{ Error = "Non-finite material color"; return false; }
		}
		if (!std::isfinite(Material.SpecularFactor) || !std::isfinite(Material.EmissiveFactor) ||
			Material.SpecularFactor < 0.f || Material.EmissiveFactor < 0.f)
		{ Error = "Invalid material factor"; return false; }
		if (Material.SamplerState != ESamplerState::LinearClamp && Material.SamplerState != ESamplerState::LinearWrap)
		{ Error = "Unsupported FBX material sampler"; return false; }
		if (Material.BlendState != EBlendState::Opaque && Material.BlendState != EBlendState::AlphaBlend)
		{ Error = "Unsupported FBX material blend mode"; return false; }
	}
	else if (Type == EFbxAssetType::AnimSequence)
	{
		if (SkeletonKey.empty()) { Error = "Missing Skeleton asset reference"; return false; }
		if (!std::isfinite(Animation.FrameRate) || Animation.FrameRate <= 0.f || Animation.NumberOfFrames < 1 || Animation.NumberOfKeys != Animation.NumberOfFrames + 1)
		{ Error = "Invalid animation frame data"; return false; }
		const auto ValidKeyCount = [&](int32 Count) { return Count == 1 || Count == Animation.NumberOfKeys; };
		for (const auto& Track : Animation.Tracks)
		{
			const auto& Keys = Track.InternalTrackData;
			if (Track.BoneTreeIndex < 0 || !ValidKeyCount(Keys.PosKeys.Num()) || !ValidKeyCount(Keys.RotKeys.Num()) || !ValidKeyCount(Keys.ScaleKeys.Num()))
			{ Error = "Invalid animation track size"; return false; }
			for (const auto& Key : Keys.PosKeys) if (!Finite(Key)) { Error = "Non-finite animation key"; return false; }
			for (const auto& Key : Keys.ScaleKeys) if (!Finite(Key)) { Error = "Non-finite animation key"; return false; }
			for (const auto& Key : Keys.RotKeys)
				if (!std::isfinite(Key.X) || !std::isfinite(Key.Y) || !std::isfinite(Key.Z) || !std::isfinite(Key.W)) { Error = "Non-finite animation key"; return false; }
		}
	}
	else if (Type == EFbxAssetType::Texture2D)
	{
		uint32 BlockBytes = 0;
		switch (Texture.Format)
		{
		case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM: BlockBytes = 8; break;
		case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM: case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB: BlockBytes = 16; break;
		default: break;
		}
		const uint64 PixelBytes = FormatToBytes(Texture.Format);
		const uint64 Rows = BlockBytes ? (Texture.Height + 3u) / 4u : Texture.Height;
		const uint64 MinPitch = BlockBytes ? static_cast<uint64>((Texture.Width + 3u) / 4u) * BlockBytes : Texture.Width * PixelBytes;
		if (!Texture.IsValid() || !Texture.Width || !Texture.Height || Texture.Width > 16384 || Texture.Height > 16384 ||
			(!BlockBytes && !PixelBytes) || Texture.BytesPerPixel != (BlockBytes ? 0 : PixelBytes) || Texture.GetRowPitch() < MinPitch ||
			static_cast<uint64>(Texture.GetRowPitch()) * Rows != static_cast<uint64>(Texture.Pixels.Num()))
		{ Error = "Invalid texture dimensions, format or pixel layout"; return false; }
	}
	else { Error = "Unknown FBX asset type"; return false; }
	return true;
}

bool FFbxAssetBake::ReadBaked(const FString& Path, FFbxAssetData& Out, FString& Error)
{
	std::ifstream In(FbxPath::FromString(Path), std::ios::binary);
	if (!In) { Error = "Cannot open FBX binary: " + Path; return false; }
	FFbxBakeHeader Header, Expected;
	if (!In.read(reinterpret_cast<char*>(&Header), sizeof(Header)) || std::memcmp(Header.Magic, Expected.Magic, 4) ||
		Header.Version < FbxBakeMinReadVersion || Header.Version > FbxBakeVersion || Header.Type < 1 || Header.Type > 6)
	{ Error = "Invalid .fbxbin header/version; import the FBX again: " + Path; return false; }
	FFbxAssetData Data; Data.Type = static_cast<EFbxAssetType>(Header.Type);
	FBinary Ar; Ar.In = &In; Ar.Version = Header.Version; Serialize(Ar, Data);
	if (!Ar.bGood || In.peek() != std::char_traits<char>::eof()) { Error = "Truncated or trailing FBX binary data: " + Path; return false; }
	if (!Data.Validate(Error)) return false;
	Out = std::move(Data);
	return true;
}

bool FFbxAssetBake::ReadSourceIdentity(const FString& Path, FString& Source, FString& Object)
{
	std::ifstream In(FbxPath::FromString(Path), std::ios::binary);
	FFbxBakeHeader Header, Expected;
	if (!In.read(reinterpret_cast<char*>(&Header), sizeof(Header)) || std::memcmp(Header.Magic, Expected.Magic, 4) ||
		Header.Type < 1 || Header.Type > 6) return false;
	FBinary Ar; Ar.In = &In; Ar.String(Source); Ar.String(Object);
	return Ar.bGood;
}

bool FFbxAssetBake::WriteBaked(const FString& Path, const FFbxAssetData& Input, FString& Error)
{
	if (!Input.Validate(Error)) return false;
	std::ofstream Out(FbxPath::FromString(Path), std::ios::binary | std::ios::trunc);
	if (!Out) { Error = "Cannot create FBX binary: " + Path; return false; }
	FFbxBakeHeader Header; Header.Type = static_cast<uint32>(Input.Type);
	Out.write(reinterpret_cast<const char*>(&Header), sizeof(Header));
	// 직렬화 코드는 읽기/쓰기 필드 순서를 함께 유지한다.
	FFbxAssetData Data = Input;
	FBinary Ar; Ar.Out = &Out; Serialize(Ar, Data); Out.flush();
	if (!Ar.bGood || !Out) { Error = "Cannot write FBX binary: " + Path; return false; }
	return true;
}
