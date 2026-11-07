#include "EnginePCH.h"
#include "Serialization/TypeSerializer.h"
#include "Serialization/MathSerialization.h"
#include "Asset/FbxImporter/FbxImporter.h"
#include "Asset/FbxImporter/FbxAssetBake.h"
#include "Asset/FbxImporter/FbxAssetLoader.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Asset/AssetManager.h"
#include "Animation/Skeleton.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/RenderCommand.h"
#include "Render/Texture2D.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/Formatter/BinaryArchiveFormatter.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"
#include <fbxsdk.h>
#include <cmath>
#include <stdexcept>
#include <set>
#include <algorithm>
#include <functional>

namespace
{
void Check(bool Value, const FString& Message) { if (!Value) throw std::runtime_error(Message); }
bool Near(float A, float B) { return std::abs(A - B) < .0005f; }
const fs::path Root = "Assets/.fbx-test-fixtures";

void WriteImage(const fs::path& Path)
{
	// 1x1 24-bit BMP. 추가 이미지 인코더 없이 기존 stb 로더를 검증한다.
	uint8 Bytes[58]{};
	Bytes[0] = 'B'; Bytes[1] = 'M'; Bytes[2] = 58; Bytes[10] = 54; Bytes[14] = 40;
	Bytes[18] = 1; Bytes[22] = 1; Bytes[26] = 1; Bytes[28] = 24; Bytes[34] = 4;
	Bytes[54] = 89; Bytes[55] = 47; Bytes[56] = 23;
	std::ofstream File(Path, std::ios::binary); File.write(reinterpret_cast<const char*>(Bytes), sizeof(Bytes));
	Check(static_cast<bool>(File), "Create fixture image");
}

void WriteRGBAImage(const fs::path& Path, uint16 Width, uint16 Height, const std::vector<std::array<uint8, 4>>& Pixels)
{
	Check(Pixels.size() == static_cast<size_t>(Width) * Height, "RGBA fixture dimensions");
	uint8 Header[18]{}; Header[2] = 2; Header[12] = Width & 255; Header[13] = Width >> 8;
	Header[14] = Height & 255; Header[15] = Height >> 8; Header[16] = 32; Header[17] = 0x28;
	std::ofstream File(Path, std::ios::binary); File.write(reinterpret_cast<const char*>(Header), sizeof(Header));
	for (const auto& P : Pixels)
	{
		const uint8 BGRA[]{ P[2], P[1], P[0], P[3] }; File.write(reinterpret_cast<const char*>(BGRA), 4);
	}
	Check(static_cast<bool>(File), "Create RGBA fixture image");
}

struct FScene
{
	FbxManager* Manager = FbxManager::Create();
	FbxScene* Scene = nullptr;
	FScene()
	{
		Manager->SetIOSettings(FbxIOSettings::Create(Manager, IOSROOT));
		Scene = FbxScene::Create(Manager, "Test");
		Scene->GetGlobalSettings().SetAxisSystem(FbxAxisSystem(FbxAxisSystem::eZAxis,
			static_cast<FbxAxisSystem::EFrontVector>(-FbxAxisSystem::eParityOdd), FbxAxisSystem::eRightHanded));
		Scene->GetGlobalSettings().SetSystemUnit(FbxSystemUnit::cm);
	}
	~FScene() { Manager->Destroy(); }
	FbxNode* Node(const char* Name, FbxNode* Parent = nullptr)
	{
		auto* N = FbxNode::Create(Scene, Name); (Parent ? Parent : Scene->GetRootNode())->AddChild(N); return N;
	}
	FbxNode* Bone(const char* Name, FbxNode* Parent = nullptr)
	{
		auto* N = Node(Name, Parent); auto* B = FbxSkeleton::Create(Scene, Name);
		B->SetSkeletonType(Parent ? FbxSkeleton::eLimbNode : FbxSkeleton::eRoot); N->SetNodeAttribute(B); return N;
	}
	FbxNode* Triangle(const char* Name, FbxNode* Parent = nullptr)
	{
		auto* N = Node(Name, Parent); auto* M = FbxMesh::Create(Scene, Name); N->SetNodeAttribute(M);
		M->InitControlPoints(3); auto* P = M->GetControlPoints();
		P[0] = FbxVector4(0, 0, 0); P[1] = FbxVector4(100, 0, 0); P[2] = FbxVector4(0, 100, 0);
		M->BeginPolygon(); M->AddPolygon(0); M->AddPolygon(1); M->AddPolygon(2); M->EndPolygon();
		auto* UV = M->CreateElementUV("UV0"); UV->SetMappingMode(FbxLayerElement::eByControlPoint); UV->SetReferenceMode(FbxLayerElement::eDirect);
		UV->GetDirectArray().Add(FbxVector2(0, 0)); UV->GetDirectArray().Add(FbxVector2(1, 0)); UV->GetDirectArray().Add(FbxVector2(0, 1));
		return N;
	}
	void Skin(FbxNode* Mesh, const std::vector<FbxNode*>& Bones, const std::vector<double>& Weights, bool DQ = false)
	{
		auto* Skin = FbxSkin::Create(Scene, "Skin"); Skin->SetSkinningType(DQ ? FbxSkin::eDualQuaternion : FbxSkin::eLinear);
		for (size_t I = 0; I < Bones.size(); ++I)
		{
			auto* C = FbxCluster::Create(Scene, "Cluster"); C->SetLink(Bones[I]); C->SetLinkMode(FbxCluster::eNormalize);
			C->SetTransformMatrix(Mesh->EvaluateGlobalTransform(FBXSDK_TIME_ZERO)); C->SetTransformLinkMatrix(Bones[I]->EvaluateGlobalTransform(FBXSDK_TIME_ZERO));
			for (int CP = 0; CP < Mesh->GetMesh()->GetControlPointsCount(); ++CP) C->AddControlPointIndex(CP, Weights[I]);
			Skin->AddCluster(C);
		}
		Mesh->GetMesh()->AddDeformer(Skin);
	}
	FString Save(const char* Name, bool Embedded = false, const fs::path& Directory = Root)
	{
		const FString Path = FbxPath::ToString(Directory / Name);
		Manager->GetIOSettings()->SetBoolProp(EXP_FBX_EMBEDDED, Embedded);
		auto* Exporter = FbxExporter::Create(Manager, "");
		Check(Exporter->Initialize(Path.c_str(), -1, Manager->GetIOSettings()), "Exporter initialize");
		Check(Exporter->Export(Scene), Exporter->GetStatus().GetErrorString()); Exporter->Destroy(); return Path;
	}
};

TArray<FFbxAssetData> Cook(const FString& Path, FFbxImportOptions Options = {}, TArray<FString>* CapturedWarnings = nullptr)
{
	TArray<FFbxAssetData> Out; TArray<FString> Warnings; FString Error;
	Check(FFbxImporter::Cook(Path, Options, FbxPath::ToString(Root / "extracted"), Out, Warnings, Error, FbxPath::ToString(Root)), Error);
	for (const auto& W : Warnings) std::cout << "  warning: " << W << '\n';
	if (CapturedWarnings) *CapturedWarnings = Warnings;
	return Out;
}
std::vector<const FFbxAssetData*> Type(const TArray<FFbxAssetData>& Data, EFbxAssetType T)
{
	std::vector<const FFbxAssetData*> Result; for (const auto& D : Data) if (D.Type == T) Result.push_back(&D); return Result;
}

bool HasWarning(const TArray<FString>& Warnings, const char* Text)
{
	return std::any_of(Warnings.begin(), Warnings.end(), [&](const auto& W) { return W.find(Text) != FString::npos; });
}

void AddUV(FbxMesh* Mesh, const char* Name, double U, double V)
{
	auto* UV = Mesh->CreateElementUV(Name); UV->SetMappingMode(FbxLayerElement::eByControlPoint); UV->SetReferenceMode(FbxLayerElement::eDirect);
	UV->GetDirectArray().Add(FbxVector2(U, V)); UV->GetDirectArray().Add(FbxVector2(U + 1, V)); UV->GetDirectArray().Add(FbxVector2(U, V + 1));
}

void AddBasis(FbxMesh* Mesh, int Layer, const FbxVector4& T, const FbxVector4& B)
{
	auto* Normals = Mesh->CreateElementNormal(); Normals->SetMappingMode(FbxLayerElement::eAllSame); Normals->SetReferenceMode(FbxLayerElement::eDirect);
	Normals->GetDirectArray().Add(FbxVector4(0, 0, 1));
	auto* Tangents = FbxLayerElementTangent::Create(Mesh, Mesh->GetLayer(Layer)->GetUVs()->GetName());
	Tangents->SetMappingMode(FbxLayerElement::eByPolygonVertex); Tangents->SetReferenceMode(FbxLayerElement::eIndexToDirect);
	Tangents->GetDirectArray().Add(T); for (int I = 0; I < Mesh->GetPolygonVertexCount(); ++I) Tangents->GetIndexArray().Add(0);
	auto* Binormals = FbxLayerElementBinormal::Create(Mesh, Mesh->GetLayer(Layer)->GetUVs()->GetName());
	Binormals->SetMappingMode(FbxLayerElement::eByControlPoint); Binormals->SetReferenceMode(FbxLayerElement::eDirect);
	for (int I = 0; I < Mesh->GetControlPointsCount(); ++I) Binormals->GetDirectArray().Add(B);
	Mesh->GetLayer(Layer)->SetTangents(Tangents); Mesh->GetLayer(Layer)->SetBinormals(Binormals);
}

void UVAndTangents()
{
	const auto ImagePath = fs::absolute(Root / "uv-pixel.bmp"); WriteImage(ImagePath);
	using Setup = std::function<void(FbxNode*, FbxNode*, FbxFileTexture*, FbxFileTexture*)>;
	auto Run = [&](const char* Name, const Setup& Configure, FVector2 ExpectedUV, FVector4 ExpectedTangent,
		ESamplerState ExpectedSampler, const char* Warning = nullptr)
	{
		FScene S; auto* Static = S.Triangle("Static"); auto* Skinned = S.Triangle("Skinned");
		auto* Bone = S.Bone("Root"); S.Skin(Skinned, { Bone }, { 1 });
		auto* M = FbxSurfacePhong::Create(S.Scene, "Paint");
		auto* D = FbxFileTexture::Create(S.Scene, "Diffuse"); auto* N = FbxFileTexture::Create(S.Scene, "Normal");
		for (auto* Texture : { D, N }) { Texture->SetFileName(FbxPath::ToString(ImagePath).c_str()); Texture->UVSet.Set("UV1"); }
		M->Diffuse.ConnectSrcObject(D); M->NormalMap.ConnectSrcObject(N);
		for (auto* Node : { Static, Skinned })
		{
			Node->AddMaterial(M); AddUV(Node->GetMesh(), "UV1", .25, .5);
			AddBasis(Node->GetMesh(), 1, FbxVector4(0, 1, 0), FbxVector4(-1, 0, 0));
		}
		Configure(Static, Skinned, D, N);
		TArray<FString> Warnings; auto Data = Cook(S.Save(Name), {}, &Warnings);
		Check(Warning ? HasWarning(Warnings, Warning) : Warnings.IsEmpty(), FString(Name) + ": expected warnings");
		const auto* Material = Type(Data, EFbxAssetType::Material)[0];
		Check(Material->Material.SamplerState == ExpectedSampler, FString(Name) + ": Wrap mapping");
		for (auto Kind : { EFbxAssetType::StaticMesh, EFbxAssetType::SkeletalMesh })
		{
			const auto* Asset = Type(Data, Kind)[0]; const auto& V = Asset->LODs[0].MeshData.Vertices[0];
			Check(Near(V.UV.X, ExpectedUV.X) && Near(V.UV.Y, ExpectedUV.Y), FString(Name) + ": final UV");
			Check(Near(V.Tangent.X, ExpectedTangent.X) && Near(V.Tangent.Y, ExpectedTangent.Y) && Near(V.Tangent.Z, ExpectedTangent.Z) &&
				Near(V.Tangent.W, ExpectedTangent.W), FString(Name) + ": tangent direction/handedness");
			if (Kind == EFbxAssetType::SkeletalMesh)
				Check(Asset->LODs[0].SkinWeights.InfluenceBones.Num() == Asset->LODs[0].MeshData.Vertices.Num(), "UV processing retains skin weight correspondence");
		}
		FString Error; FFbxAssetData Read;
		Check(FFbxAssetBake::WriteBaked(FbxPath::ToString(Root / "uv-material.fbxbin"), *Material, Error) &&
			FFbxAssetBake::ReadBaked(FbxPath::ToString(Root / "uv-material.fbxbin"), Read, Error), Error);
		Check(Read.Material.SamplerState == ExpectedSampler, "Material sampler binary roundtrip");
		FFbxAssetData Invalid = *Material; Invalid.Material.SamplerState = ESamplerState::ShadowPCF;
		Check(!FFbxAssetBake::WriteBaked(FbxPath::ToString(Root / "invalid-sampler.fbxbin"), Invalid, Error), "Shadow sampler rejected for imported material");
	};
	const Setup None = [](auto*, auto*, auto*, auto*) {};
	Run("uv-selected.fbx", None, {.25f, .5f}, {0, -1, 0, 1}, ESamplerState::LinearWrap);
	Run("uv-scale.fbx", [](auto*, auto*, auto* D, auto* N) { for (auto* T : {D, N}) T->SetScale(2, 3); },
		{.5f, -.5f}, {0, -1, 0, 1}, ESamplerState::LinearWrap);
	Run("uv-rotate.fbx", [](auto*, auto*, auto* D, auto* N) { for (auto* T : {D, N}) { T->SetRotation(0, 0, 90); T->SetTranslation(.1, .2); } },
		{-.4f, .55f}, {1, 0, 0, 1}, ESamplerState::LinearWrap);
	Run("uv-pivot.fbx", [](auto*, auto*, auto* D, auto* N) { for (auto* T : {D, N}) { T->SetScale(2, 2); T->ScalingPivot.Set(FbxDouble3(.5, .5, 0)); } },
		{0, .5f}, {0, -1, 0, 1}, ESamplerState::LinearWrap);
	Run("uv-mirror.fbx", [](auto*, auto*, auto* D, auto* N) { for (auto* T : {D, N}) T->SetScale(-1, 1); },
		{-.25f, .5f}, {0, 1, 0, -1}, ESamplerState::LinearWrap);
	Run("uv-swap.fbx", [](auto*, auto*, auto* D, auto* N) { for (auto* T : {D, N}) T->SetSwapUV(true); },
		{.5f, .75f}, {-1, 0, 0, -1}, ESamplerState::LinearWrap);
	Run("uv-different-sets.fbx", [](auto*, auto*, auto* D, auto* N) { D->UVSet.Set("UV0"); for (auto* T : {D, N}) T->SetScale(2, 3); },
		{0, 1}, {1, 0, 0, 1}, ESamplerState::LinearWrap, "Different texture UV sets");
	Run("uv-different-transforms.fbx", [](auto*, auto*, auto* D, auto*) { D->SetScale(2, 3); },
		{.25f, .5f}, {0, -1, 0, 1}, ESamplerState::LinearWrap, "Different texture UV transforms");
	Run("uv-transform-tolerance.fbx", [](auto*, auto*, auto*, auto* N) { N->SetTranslation(1.e-8, 0); },
		{.25f, .5f}, {0, -1, 0, 1}, ESamplerState::LinearWrap);
	Run("uv-missing-set.fbx", [](auto*, auto*, auto* D, auto*) { D->UVSet.Set("Missing"); D->SetScale(2, 2); },
		{.25f, .5f}, {0, -1, 0, 1}, ESamplerState::LinearWrap, "UV set not found");
	Run("wrap-clamp.fbx", [](auto*, auto*, auto* D, auto* N) { for (auto* T : {D, N}) T->SetWrapMode(FbxTexture::eClamp, FbxTexture::eClamp); },
		{.25f, .5f}, {0, -1, 0, 1}, ESamplerState::LinearClamp);
	Run("wrap-conflict.fbx", [](auto*, auto*, auto*, auto* N) { N->SetWrapMode(FbxTexture::eClamp, FbxTexture::eClamp); },
		{.25f, .5f}, {0, -1, 0, 1}, ESamplerState::LinearWrap, "Different texture Wrap modes");
	Run("wrap-mixed.fbx", [](auto*, auto*, auto* D, auto* N) { for (auto* T : {D, N}) T->SetWrapMode(FbxTexture::eRepeat, FbxTexture::eClamp); },
		{.25f, .5f}, {0, -1, 0, 1}, ESamplerState::LinearClamp, "Unsupported mixed U/V Wrap modes");
	Run("uv-normal-only.fbx", [](auto* A, auto*, auto* D, auto*) { A->GetMaterial(0)->FindProperty(FbxSurfaceMaterial::sDiffuse).DisconnectSrcObject(D); },
		{.25f, .5f}, {0, -1, 0, 1}, ESamplerState::LinearWrap);
	Run("tangent-invalid.fbx", [](auto* A, auto* B, auto*, auto*) { for (auto* Node : {A, B}) Node->GetMesh()->GetLayer(1)->GetTangents()->GetDirectArray().SetAt(0, FbxVector4(0, 0, 0)); },
		{.25f, .5f}, {1, 0, 0, 1}, ESamplerState::LinearWrap, "Invalid source tangent basis");
	Run("tangent-geometry-mirror.fbx", [](auto* A, auto* B, auto*, auto*) { for (auto* Node : {A, B}) Node->SetGeometricScaling(FbxNode::eSourcePivot, FbxVector4(-2, 3, 1)); },
		{.25f, .5f}, {0, -1, 0, -1}, ESamplerState::LinearWrap);
}

void UVSurfaceBoundaries()
{
	const auto ImagePath = fs::absolute(Root / "uv-pixel.bmp");
	FScene S; auto* Static = S.Triangle("Static"); auto* Skinned = S.Triangle("Skinned");
	auto* Bone = S.Bone("Root");
	FbxSurfacePhong* Materials[2];
	for (int I = 0; I < 2; ++I)
	{
		Materials[I] = FbxSurfacePhong::Create(S.Scene, I ? "Plain" : "Scaled");
		auto* Texture = FbxFileTexture::Create(S.Scene, I ? "Plain" : "Scaled");
		Texture->SetFileName(FbxPath::ToString(ImagePath).c_str()); Texture->UVSet.Set("UV0"); Texture->SetScale(I ? 1 : 2, 1);
		Materials[I]->Diffuse.ConnectSrcObject(Texture);
	}
	for (auto* Node : {Static, Skinned})
	{
		auto* Mesh = Node->GetMesh(); Mesh->InitControlPoints(4); auto* Points = Mesh->GetControlPoints();
		Points[0] = FbxVector4(0, 0, 0); Points[1] = FbxVector4(100, 0, 0); Points[2] = FbxVector4(0, 100, 0); Points[3] = FbxVector4(-100, 100, 0);
		Mesh->BeginPolygon(); Mesh->AddPolygon(0); Mesh->AddPolygon(2); Mesh->AddPolygon(3); Mesh->EndPolygon();
		Mesh->GetElementUV(0)->GetDirectArray().Add(FbxVector2(-1, 1));
		for (auto* Material : Materials) Node->AddMaterial(Material);
		auto* Slots = Mesh->CreateElementMaterial(); Slots->SetMappingMode(FbxLayerElement::eByPolygon); Slots->SetReferenceMode(FbxLayerElement::eIndexToDirect);
		Slots->GetIndexArray().Add(0); Slots->GetIndexArray().Add(1);
		AddBasis(Mesh, 0, FbxVector4(.6, .8, 0), FbxVector4(-.8, .6, 0));
	}
	S.Skin(Skinned, {Bone}, {1});
	TArray<FString> Warnings; auto Data = Cook(S.Save("uv-boundaries.fbx"), {}, &Warnings);
	Check(Warnings.IsEmpty(), "Different materials process UV transforms independently");
	for (auto Kind : {EFbxAssetType::StaticMesh, EFbxAssetType::SkeletalMesh})
	{
		const auto& LOD = Type(Data, Kind)[0]->LODs[0]; const auto& Mesh = LOD.MeshData;
		Check(Mesh.Vertices.Num() == 6 && Mesh.Sections.Num() == 2, "Different UV transforms split shared vertices even when corner UVs coincide");
		const auto& A = Mesh.Vertices[Mesh.Indices[Mesh.Sections[0].StartIndex]];
		const auto& B = Mesh.Vertices[Mesh.Indices[Mesh.Sections[1].StartIndex]];
		Check(Near(A.Position.X, 0) && Near(B.Position.X, 0) && Near(A.UV.X, B.UV.X) && Near(A.UV.Y, B.UV.Y), "Split corner retains source location and coincident UV");
		for (const auto& V : Mesh.Vertices) Check(Near(V.Tangent.X, .6f) && Near(V.Tangent.Y, -.8f), "Authored tangent retained across sections");
		if (Kind == EFbxAssetType::SkeletalMesh)
		{
			Check(LOD.SkinWeights.InfluenceWeights.Num() == 6, "Split UV vertices duplicate skin weights");
			for (const auto& Weights : LOD.SkinWeights.InfluenceWeights) Check(Near(Weights[0], 1), "Split UV vertices preserve influence values");
		}
	}
	// 한 코너만 잘못된 원본 기저여도 다른 코너의 유효한 원본 탄젠트는 유지한다.
	FScene Partial; auto* Node = Partial.Triangle("Partial"); auto* Mesh = Node->GetMesh();
	AddBasis(Mesh, 0, FbxVector4(.6, .8, 0), FbxVector4(-.8, .6, 0));
	auto* Tangents = Mesh->GetLayer(0)->GetTangents(); Tangents->GetDirectArray().Add(FbxVector4(0, 0, 0)); Tangents->GetIndexArray().SetAt(0, 1);
	Data = Cook(Partial.Save("tangent-partial.fbx"), {}, &Warnings);
	const auto& Vertices = Type(Data, EFbxAssetType::StaticMesh)[0]->LODs[0].MeshData.Vertices;
	Check(HasWarning(Warnings, "Invalid source tangent basis") && Near(Vertices[0].Tangent.X, 1) && Near(Vertices[0].Tangent.Y, 0), "Invalid corner tangent recalculated");
	Check(Near(Vertices[1].Tangent.X, .6f) && Near(Vertices[1].Tangent.Y, -.8f) && Near(Vertices[2].Tangent.X, .6f), "Valid corner tangents survive partial fallback");
}

void StaticOptions()
{
	FScene S; auto* Parent = S.Node("Parent"); Parent->LclTranslation.Set(FbxDouble3(200, 0, 0));
	auto* A = S.Triangle("A", Parent); A->LclTranslation.Set(FbxDouble3(100, 0, 0));
	S.Triangle("B"); auto* Hidden = S.Triangle("Hidden"); Hidden->Visibility.Set(0);
	S.Triangle("UCX_A_00"); const FString Path = S.Save("static.fbx");
	auto Data = Cook(Path); auto Meshes = Type(Data, EFbxAssetType::StaticMesh);
	Check(Meshes.size() == 2, "Static separation/hidden/collision filtering");
	bool Found = false;
	for (const auto* M : Meshes) if (M->Key.find("_A.") != FString::npos)
	{
		Found = true; Check(Near(M->LODs[0].MeshData.Vertices[0].Position.X, 3), "Baked static parent transform/meters");
		const auto& Geometry = M->LODs[0].MeshData;
		Check(Geometry.Vertices.Num() == Geometry.LODSourceVertices.Num(), "Static LOD topology retained");
		Check(Near(Geometry.Vertices[2].Position.Y, -1), "Handedness reflection");
		Check(Geometry.Indices[1] == 2 && Geometry.Indices[2] == 1, "Winding reflection");
	}
	Check(Found, "Static output name");
	FFbxImportOptions O; O.bBakeStaticMeshes = false; O.bCombineStaticMeshes = true;
	Data = Cook(Path, O); Meshes = Type(Data, EFbxAssetType::StaticMesh);
	Check(Meshes.size() == 1 && Meshes[0]->LODs[0].MeshData.Vertices.Num() == 6, "Static Combine");
	Check(Near(Meshes[0]->LODs[0].MeshData.Vertices[0].Position.X, 0), "Static Bake off excludes own transform");
}

void SkeletalOptions()
{
	FScene S; auto* Placement = S.Node("Placement"); Placement->LclTranslation.Set(FbxDouble3(1000, 0, 0));
	auto* RootBone = S.Bone("Root", Placement); RootBone->LclTranslation.Set(FbxDouble3(100, 0, 0));
	std::vector<FbxNode*> Bones{ RootBone };
	for (int I = 1; I < 6; ++I)
	{
		auto* B = S.Bone(("Bone" + std::to_string(I)).c_str(), Bones.back());
		B->LclTranslation.Set(FbxDouble3(0, 0, 25)); B->LclRotation.Set(FbxDouble3(10, 20, 30)); Bones.push_back(B);
	}
	auto* A = S.Triangle("Body"); A->LclTranslation.Set(FbxDouble3(1100, 0, 0));
	S.Skin(A, Bones, { .05, .10, .15, .20, .25, .25 }, true);
	auto* B = S.Triangle("Clothes"); S.Skin(B, Bones, { 1, 0, 0, 0, 0, 0 });
	S.Triangle("Rigid", Bones.back());
	const FString Path = S.Save("skeletal.fbx");
	FFbxImportOptions O; O.bBakeSkeletalMeshes = false;
	auto Data = Cook(Path, O); auto Meshes = Type(Data, EFbxAssetType::SkeletalMesh);
	Check(Meshes.size() == 1 && Type(Data, EFbxAssetType::Skeleton).size() == 1, "One rig combined output");
	const auto& M = *Meshes[0];
	Check(M.RefSkeleton.GetNum() == 7, "All ancestors and rigid mesh bone retained");
	Check(Near(M.RefSkeleton.RefBonePose[0].Location.X, 1), "Skeletal Bake off retains root own transform");
	const auto& Weights = M.LODs[0].SkinWeights;
	int BodyVertex = -1;
	for (int I = 0; I < Weights.InfluenceBones.Num(); ++I) if (Weights.InfluenceBones[I][0] == 4) { BodyVertex = I; break; }
	Check(BodyVertex >= 0, "Body influences found");
	Check(Near(M.LODs[0].MeshData.Vertices[BodyVertex].Position.X, 1), "Skeletal Bake off removes placement");
	Check(Weights.InfluenceBones[BodyVertex][1] == 5 && Weights.InfluenceBones[BodyVertex][3] == 2, "Largest four influences/global bone indices");
	Check(Near(Weights.InfluenceWeights[BodyVertex][0], .25f / .85f), "Culled weights normalized");
	TArray<FMatrix> Model; FString Error; Check(M.RefSkeleton.GetModelSpaceMatrices(Model, Error), Error);
	for (size_t I = 0; I < Bones.size(); ++I)
	{
		const auto Bind = Placement->EvaluateGlobalTransform(FBXSDK_TIME_ZERO).Inverse() * Bones[I]->EvaluateGlobalTransform(FBXSDK_TIME_ZERO);
		const auto OPos = Bind.MultT(FbxVector4(0, 0, 0));
		const FVector Actual = Model[I].TransformPosition(FVector(0, 0, 0));
		Check(Near(Actual.X, OPos[0] * .01f) && Near(Actual.Y, -OPos[1] * .01f) && Near(Actual.Z, OPos[2] * .01f), "Accumulated bone transform conversion");
	}
	O.bBakeSkeletalMeshes = true; O.bCombineSkeletalMeshes = false;
	Data = Cook(Path, O); Meshes = Type(Data, EFbxAssetType::SkeletalMesh);
	Check(Meshes.size() == 3 && Type(Data, EFbxAssetType::Skeleton).size() == 1, "Uncombined meshes share same source Skeleton");
	Check(Near(Meshes[0]->RefSkeleton.RefBonePose[0].Location.X, 11), "Skeletal Bake on includes placement");
}

void LODs()
{
	FScene S;
	for (int GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
	{
		auto* N = S.Node(("LODGroup" + std::to_string(GroupIndex)).c_str());
		auto* G = FbxLODGroup::Create(S.Scene, "LODs"); N->SetNodeAttribute(G);
		for (int L = 0; L < (GroupIndex ? 2 : 5); ++L)
		{
			S.Triangle(("Level" + std::to_string(L)).c_str(), N);
			if (L) G->AddThreshold(FbxDistance(1000.f * L, "cm"));
		}
	}
	const auto Path = S.Save("lod.fbx"); auto Data = Cook(Path);
	Check(Type(Data, EFbxAssetType::StaticMesh).size() == 2, "LODGroup is one output with Combine off");
	FFbxImportOptions O; O.bCombineStaticMeshes = true; Data = Cook(Path, O);
	const auto* M = Type(Data, EFbxAssetType::StaticMesh)[0];
	Check(M->LODs.Num() == 4, "Only LOD0 through LOD3");
	Check(M->LODs[0].MeshData.Indices.Num() == 6 && M->LODs[1].MeshData.Indices.Num() == 6 &&
		M->LODs[2].MeshData.Indices.Num() == 3 && M->LODs[3].MeshData.Indices.Num() == 3, "Missing LODs are not repeated");
	Check(M->ScreenThresholds[0] > M->ScreenThresholds[1], "Distance LOD thresholds converted");
}

void TexturesAndMaterials()
{
	fs::create_directory(Root / FbxPath::FromString("한글"));
	const auto ImagePath = fs::absolute(Root / FbxPath::FromString("한글/pixel.bmp"));
	WriteImage(ImagePath);
	FScene S; auto* N = S.Triangle("Textured");
	auto* M = FbxSurfacePhong::Create(S.Scene, "Paint"); M->Diffuse.Set(FbxDouble3(.2, .4, .6)); M->DiffuseFactor.Set(.5); M->Shininess.Set(17);
	M->Specular.Set(FbxDouble3(.2, .5, .8)); M->SpecularFactor.Set(.25);
	M->Emissive.Set(FbxDouble3(.1, .2, .4)); M->EmissiveFactor.Set(3);
	auto* T = FbxFileTexture::Create(S.Scene, "PaintDiffuse");
	T->SetFileName(FbxPath::ToString(ImagePath).c_str()); T->UVSet.Set("UV1"); T->SetTranslation(.1, .2);
	M->Diffuse.ConnectSrcObject(T); M->NormalMap.ConnectSrcObject(T); N->AddMaterial(M);
	N->SetGeometricTranslation(FbxNode::eSourcePivot, FbxVector4(100, 0, 0));
	const auto External = S.Save("textured.fbx");
	auto Data = Cook(External);
	Check(Type(Data, EFbxAssetType::Material).size() == 1 && Type(Data, EFbxAssetType::Texture2D).size() == 1, "Separate material/texture outputs and texture reuse");
	const auto* Material = Type(Data, EFbxAssetType::Material)[0];
	const auto& Factor = Material->Material.DiffuseOrFactor;
	Check(Near(Factor.X, .5f) && Near(Factor.Y, .5f) && Near(Factor.Z, .5f) && Near(Factor.W, 1), "Diffuse map preserves factor independently of diffuse color");
	Check(Near(Material->Material.Shininess, 68), "Phong exponent converted to Blinn-Phong");
	Check(Near(Material->Material.SpecularColor.X, .2f) && Near(Material->Material.SpecularColor.Y, .5f) &&
		Near(Material->Material.SpecularColor.Z, .8f) && Near(Material->Material.SpecularFactor, .25f), "Phong specular color and factor imported separately");
	Check(Near(Material->Material.EmissiveColor.X, .1f) && Near(Material->Material.EmissiveColor.Y, .2f) &&
		Near(Material->Material.EmissiveColor.Z, .4f) && Near(Material->Material.EmissiveFactor, 3), "HDR emissive color and factor imported separately");
	{
		FString Error; FFbxAssetData Read;
		Check(FFbxAssetBake::WriteBaked(FbxPath::ToString(Root / "lighting-material.fbxbin"), *Material, Error) &&
			FFbxAssetBake::ReadBaked(FbxPath::ToString(Root / "lighting-material.fbxbin"), Read, Error), Error);
		Check(Near(Read.Material.SpecularColor.Y, .5f) && Near(Read.Material.SpecularFactor, .25f) &&
			Near(Read.Material.EmissiveColor.Z, .4f) && Near(Read.Material.EmissiveFactor, 3), "Lighting material binary roundtrip");
	}
	Check(Material->Material.DiffuseTexture == Material->Material.NormalTexture, "Shared texture reference");
	const auto* Mesh = Type(Data, EFbxAssetType::StaticMesh)[0];
	Check(Near(Mesh->LODs[0].MeshData.Vertices[0].Position.X, 1), "Geometric transform baked");
	Check(Near(Mesh->LODs[0].MeshData.Vertices[0].UV.X, 0), "Unsupported texture transform does not change UV0");
	const auto Embedded = S.Save("embedded.fbx", true);
	fs::remove(ImagePath);
	Data = Cook(Embedded);
	const auto* Image = Type(Data, EFbxAssetType::Texture2D)[0];
	Check(Image->Texture.Pixels.Num() == 4 && Image->Texture.Pixels[0] == 23 && Image->Texture.Pixels[2] == 89, "Embedded image extracted without external source");
	FString Error; FFbxAssetData Read;
	const auto Baked = FbxPath::ToString(Root / "texture-roundtrip.fbxbin");
	Check(FFbxAssetBake::WriteBaked(Baked, *Image, Error) && FFbxAssetBake::ReadBaked(Baked, Read, Error), Error);
	Check(Read.Texture.Pixels.Num() == 4 && Read.Texture.Pixels[1] == 47, "Texture binary roundtrip");
	M->Diffuse.DisconnectSrcObject(T); M->NormalMap.DisconnectSrcObject(T); M->TransparencyFactor.Set(.25);
	Data = Cook(S.Save("constant-color.fbx"));
	const auto& Constant = Type(Data, EFbxAssetType::Material)[0]->Material;
	Check(Near(Constant.DiffuseOrFactor.X, .1f) && Near(Constant.DiffuseOrFactor.Y, .2f) &&
		Near(Constant.DiffuseOrFactor.Z, .3f) && Near(Constant.DiffuseOrFactor.W, 1), "No diffuse map preserves diffuse coefficient; scalar transparency stays opaque");
	M->Diffuse.ConnectSrcObject(T); M->NormalMap.ConnectSrcObject(T);
	// 기존에 존재하지 않던 테스트 파일만 만들고 제거한다.
	const fs::path Outside = "Build/fbx-test-external.bmp";
	Check(!fs::exists(Outside), "External texture fixture already exists");
	WriteImage(Outside);
	T->SetFileName(FbxPath::ToString(fs::absolute(Outside)).c_str());
	const auto ExternalTexture = S.Save("outside-texture.fbx");
	TArray<FFbxAssetData> Out; TArray<FString> Warnings;
	const bool Accepted = FFbxImporter::Cook(ExternalTexture, {}, FbxPath::ToString(Root / "extracted"), Out, Warnings, Error, FbxPath::ToString(Root));
	fs::remove(Outside);
	Check(Accepted && Type(Out, EFbxAssetType::Texture2D).size() == 1, "External texture is baked into an Assets output");
	Check(!FFbxImporter::Cook(ExternalTexture, {}, FbxPath::ToString(Root / "extracted"), Out, Warnings, Error, FbxPath::ToString(Root)) && Out.IsEmpty(),
		"Missing external texture still causes whole import failure");
}

void BumpPolicy()
{
	FScene S; auto* M = FbxSurfacePhong::Create(S.Scene, "BumpPolicy");
	S.Triangle("StaticBump")->AddMaterial(M);
	auto* Bone = S.Bone("BumpRoot"); auto* Skin = S.Triangle("SkinnedBump"); Skin->AddMaterial(M); S.Skin(Skin, {Bone}, {1});
	auto* Bump = FbxFileTexture::Create(S.Scene, "Height");
	Bump->SetFileName("missing-ignored-bump.png"); Bump->UVSet.Set("MissingUV");
	Bump->SetWrapMode(FbxTexture::eRepeat, FbxTexture::eClamp); Bump->SetScale(2, 3);
	M->Bump.ConnectSrcObject(Bump);
	TArray<FString> Warnings; auto Data = Cook(S.Save("bump-only.fbx"), {}, &Warnings);
	Check(Warnings.Num() == 1 && HasWarning(Warnings, "Unsupported Bump map"), "Bump-only material warns once and ignores file, UV and Wrap requirements");
	Check(Type(Data, EFbxAssetType::Material)[0]->Material.NormalTexture.empty() && Type(Data, EFbxAssetType::Texture2D).empty(), "Bump-only material has no normal reference or texture output");
	Check(Type(Data, EFbxAssetType::StaticMesh).size() == 1 && Type(Data, EFbxAssetType::SkeletalMesh).size() == 1, "Bump policy applies to static and skeletal meshes");
	M->Bump.DisconnectSrcObject(Bump);
	Data = Cook(S.Save("no-normal-or-bump.fbx"), {}, &Warnings);
	Check(Warnings.IsEmpty() && Type(Data, EFbxAssetType::Material)[0]->Material.NormalTexture.empty(), "Missing NormalMap and Bump are valid without warnings");
	const auto ImagePath = fs::absolute(Root / "bump-policy-normal.bmp"); WriteImage(ImagePath);
	auto* Normal = FbxFileTexture::Create(S.Scene, "Normal"); Normal->SetFileName(FbxPath::ToString(ImagePath).c_str()); Normal->UVSet.Set("UV0");
	M->NormalMap.ConnectSrcObject(Normal); M->Bump.ConnectSrcObject(Bump);
	Data = Cook(S.Save("normal-with-unused-bump.fbx"), {}, &Warnings);
	Check(Warnings.IsEmpty() && !Type(Data, EFbxAssetType::Material)[0]->Material.NormalTexture.empty() &&
		Type(Data, EFbxAssetType::Texture2D).size() == 1, "NormalMap imports normally and unused Bump produces no warnings");
}

void MaterialLighting()
{
	FScene S; auto* M = FbxSurfaceLambert::Create(S.Scene, "LambertGlow"); S.Triangle("Glow")->AddMaterial(M);
	M->Emissive.Set(FbxDouble3(.25, .5, 1)); M->EmissiveFactor.Set(4);
	TArray<FString> Warnings; auto Data = Cook(S.Save("lambert-glow.fbx"), {}, &Warnings);
	const auto& L = Type(Data, EFbxAssetType::Material)[0]->Material;
	Check(Warnings.IsEmpty() && L.ShadingModel == ELightingModel::Lambert && Near(L.SpecularFactor, 0) &&
		Near(L.EmissiveColor.Z, 1) && Near(L.EmissiveFactor, 4), "Lambert has no specular and preserves emissive");
	auto* Unsupported = FbxFileTexture::Create(S.Scene, "UnsupportedEmissive"); Unsupported->SetFileName("ignored-emissive.png");
	M->Emissive.ConnectSrcObject(Unsupported);
	Data = Cook(S.Save("emissive-map-warning.fbx"), {}, &Warnings);
	Check(Warnings.Num() == 1 && HasWarning(Warnings, "Unsupported material texture") &&
		Type(Data, EFbxAssetType::Texture2D).empty(), "Unsupported emissive map warns and uses constant without reading image");
	FFbxAssetData Invalid; Invalid.Type = EFbxAssetType::Material; FString Error;
	for (float* Value : { &Invalid.Material.SpecularColor.X, &Invalid.Material.SpecularFactor,
		&Invalid.Material.EmissiveColor.Z, &Invalid.Material.EmissiveFactor })
	{
		const float Before = *Value; *Value = std::numeric_limits<float>::quiet_NaN();
		Check(!Invalid.Validate(Error), "Non-finite lighting material rejected"); *Value = Before;
	}
}

void Transparency()
{
	const auto DiffusePath = fs::absolute(Root / "opacity-color.tga"), AlphaPath = fs::absolute(Root / "opacity-alpha.tga");
	const auto GrayPath = fs::absolute(Root / "opacity-gray.tga"), WhitePath = fs::absolute(Root / "opacity-white.tga");
	const auto WidePath = fs::absolute(Root / "opacity-wide.tga");
	WriteRGBAImage(DiffusePath, 2, 1, {{10, 20, 30, 128}, {40, 50, 60, 0}});
	WriteRGBAImage(AlphaPath, 2, 1, {{255, 255, 255, 64}, {0, 0, 0, 192}});
	WriteRGBAImage(GrayPath, 2, 1, {{0, 0, 0, 255}, {255, 255, 255, 255}});
	WriteRGBAImage(WhitePath, 2, 1, {{255, 255, 255, 255}, {255, 255, 255, 255}});
	WriteRGBAImage(WidePath, 4, 1, {{0, 0, 0, 255}, {64, 64, 64, 255}, {128, 128, 128, 255}, {255, 255, 255, 255}});
	FScene S; auto* Bone = S.Bone("OpacityRig");
	auto Texture = [&](const char* Name, const fs::path& Path)
	{
		auto* T = FbxFileTexture::Create(S.Scene, Name); T->SetFileName(FbxPath::ToString(Path).c_str()); T->UVSet.Set("UV0"); return T;
	};
	auto* Diffuse = Texture("SharedDiffuse", DiffusePath); auto* Alpha = Texture("OpacityAlpha", AlphaPath);
	auto* Gray = Texture("OpacityGray", GrayPath); auto* White = Texture("OpacityWhite", WhitePath); auto* Wide = Texture("OpacityWide", WidePath);
	auto Material = [&](const char* Name, FbxFileTexture* Color, FbxFileTexture* Opacity, double Factor)
	{
		auto* M = FbxSurfacePhong::Create(S.Scene, Name); M->TransparencyFactor.Set(Factor);
		if (Color) M->Diffuse.ConnectSrcObject(Color);
		if (Opacity) M->TransparentColor.ConnectSrcObject(Opacity);
		auto* Static = S.Triangle(Name); Static->AddMaterial(M);
		auto* Skinned = S.Triangle((FString(Name) + "Skin").c_str()); Skinned->AddMaterial(M); S.Skin(Skinned, {Bone}, {1});
	};
	Material("OpaqueShared", Diffuse, nullptr, 0);
	Material("AlphaShared", Diffuse, Alpha, 0);
	Material("RGBShared", Diffuse, Gray, 0);
	Material("OpacityOnly", nullptr, Alpha, 0);
	Material("Constant", Diffuse, nullptr, .25);
	Material("TextureWithFactor", Diffuse, Alpha, 1);
	Material("SameTexture", Diffuse, Diffuse, 0);
	Material("OpaqueMap", Diffuse, White, 0);
	Material("Resampled", Diffuse, Wide, 0);
	TArray<FString> Warnings; auto Data = Cook(S.Save("transparency.fbx"), {}, &Warnings);
	Check(Warnings.IsEmpty(), "Supported opacity maps import without warnings");
	auto Find = [&](const char* Name) -> const FFbxMaterialData&
	{
		for (const auto& A : Data) if (A.Type == EFbxAssetType::Material && A.SourceObject.starts_with(FString("Material:") + Name + "[")) return A.Material;
		throw std::runtime_error("Missing transparency material");
	};
	auto Image = [&](const FFbxMaterialData& M) -> const FImageData&
	{
		for (const auto& A : Data) if (A.Key == M.DiffuseTexture && A.Type == EFbxAssetType::Texture2D) return A.Texture;
		throw std::runtime_error("Missing transparency texture");
	};
	const auto& Opaque = Find("OpaqueShared"); const auto& Constant = Find("Constant");
	Check(Opaque.BlendState == EBlendState::Opaque && Near(Opaque.DiffuseOrFactor.W, 1), "Diffuse alpha alone does not enable AlphaBlend");
	Check(Constant.BlendState == EBlendState::Opaque && Near(Constant.DiffuseOrFactor.W, 1) && Constant.DiffuseTexture == Opaque.DiffuseTexture, "Scalar transparency stays opaque and retains shared source texture");
	Check(Image(Opaque).Pixels[3] == 128 && Image(Opaque).Pixels[7] == 0, "Opacity baking never mutates shared source image");
	const std::pair<const char*, std::array<uint8, 2>> Expected[]{
		{"AlphaShared", {64, 192}}, {"RGBShared", {0, 255}}, {"OpacityOnly", {64, 192}},
		{"TextureWithFactor", {64, 192}}, {"SameTexture", {128, 0}}, {"OpaqueMap", {255, 255}}, {"Resampled", {32, 192}}
	};
	std::set<FString> DerivedKeys;
	for (const auto& [Name, Alphas] : Expected)
	{
		const auto& M = Find(Name); const auto& Pixels = Image(M).Pixels;
		Check(M.BlendState == EBlendState::AlphaBlend && Near(M.DiffuseOrFactor.W, 1), FString(Name) + ": map selects AlphaBlend independently of scalar");
		Check(DerivedKeys.insert(M.DiffuseTexture).second && M.DiffuseTexture != Opaque.DiffuseTexture, "Material-specific opacity textures have distinct stable keys");
		Check(Pixels[3] == Alphas[0] && Pixels[7] == Alphas[1], FString(Name) + ": expected alpha bytes without duplicate multiplication or gamma decoding");
		Check(Pixels[0] == (FString(Name) == "OpacityOnly" ? 255 : 10) && Pixels[1] == (FString(Name) == "OpacityOnly" ? 255 : 20), "Diffuse RGB remains unpremultiplied; absent Diffuse becomes white");
	}
	Check(Find("AlphaShared").DiffuseTexture != Find("RGBShared").DiffuseTexture, "Shared Diffuse supports independent opacity maps");
	FString Error; FFbxAssetData Read;
	for (const auto& A : Data) if (A.Type == EFbxAssetType::Material || A.Type == EFbxAssetType::Texture2D)
	{
		const FString File = FbxPath::ToString(Root / "opacity-roundtrip.fbxbin");
		Check(FFbxAssetBake::WriteBaked(File, A, Error) && FFbxAssetBake::ReadBaked(File, Read, Error), Error);
		if (A.Type == EFbxAssetType::Material) Check(A.Material.BlendState == Read.Material.BlendState, "Blend mode binary roundtrip");
		else Check(A.Texture.Pixels.Num() == Read.Texture.Pixels.Num() && !std::memcmp(A.Texture.Pixels.GetData(), Read.Texture.Pixels.GetData(), A.Texture.Pixels.Num()), "Baked opacity pixels binary roundtrip");
	}
	FFbxAssetData Invalid; Invalid.Type = EFbxAssetType::Material; Invalid.Material.BlendState = EBlendState::Additive;
	Check(!FFbxAssetBake::WriteBaked(FbxPath::ToString(Root / "invalid-opacity-blend.fbxbin"), Invalid, Error), "Unsupported imported blend mode rejected");
	// 투명도 텍스처도 UV 요구 판정에 참여한다.
	FScene UVScene; auto* Node = UVScene.Triangle("OpacityUV"); AddUV(Node->GetMesh(), "UV1", .25, .5);
	auto* UVMaterial = FbxSurfacePhong::Create(UVScene.Scene, "UVPaint"); Node->AddMaterial(UVMaterial);
	auto* D = FbxFileTexture::Create(UVScene.Scene, "D"); D->SetFileName(FbxPath::ToString(DiffusePath).c_str()); D->UVSet.Set("UV1"); D->SetScale(2, 2);
	auto* A = FbxFileTexture::Create(UVScene.Scene, "A"); A->SetFileName(FbxPath::ToString(AlphaPath).c_str()); A->UVSet.Set("UV0"); A->SetScale(2, 2);
	UVMaterial->Diffuse.ConnectSrcObject(D); UVMaterial->TransparentColor.ConnectSrcObject(A);
	Data = Cook(UVScene.Save("opacity-uv-sets.fbx"), {}, &Warnings);
	Check(HasWarning(Warnings, "Different texture UV sets") && Near(Type(Data, EFbxAssetType::StaticMesh)[0]->LODs[0].MeshData.Vertices[0].UV.X, .25f), "Opacity UV set conflict selects Diffuse set and ignores all transforms");
	A->UVSet.Set("UV1"); A->SetScale(3, 2);
	Data = Cook(UVScene.Save("opacity-uv-transform.fbx"), {}, &Warnings);
	Check(HasWarning(Warnings, "Different texture UV transforms"), "Opacity transform conflict warning");
	UVMaterial->Diffuse.DisconnectSrcObject(D); A->SetScale(2, 2);
	Data = Cook(UVScene.Save("opacity-uv-only.fbx"), {}, &Warnings);
	Check(Warnings.IsEmpty() && Near(Type(Data, EFbxAssetType::StaticMesh)[0]->LODs[0].MeshData.Vertices[0].UV.X, .5f), "Opacity-only material selects and bakes its UV transform");
	A->SetFileName(FbxPath::ToString(Root / "missing-opacity.tga").c_str());
	Check(!FFbxImporter::Cook(UVScene.Save("opacity-missing.fbx"), {}, FbxPath::ToString(Root / "extracted"), Data, Warnings, Error, FbxPath::ToString(Root)) && Data.IsEmpty(), "Missing opacity image rejects the whole import");
}

void OpacityBakeFallback()
{
	const auto ColorPath = fs::absolute(Root / "fallback-color.bmp"), HDRPath = fs::absolute(Root / "fallback.hdr");
	WriteImage(ColorPath);
	{
		std::ofstream File(HDRPath, std::ios::binary);
		File << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n";
		const uint8 RGBE[]{128, 128, 128, 129}; File.write(reinterpret_cast<const char*>(RGBE), sizeof(RGBE));
		Check(static_cast<bool>(File), "Create HDR opacity fixture");
	}
	FScene S; auto* Bone = S.Bone("FallbackRig");
	auto Texture = [&](const char* Name, const fs::path& Path, const char* UV)
	{
		auto* T = FbxFileTexture::Create(S.Scene, Name); T->SetFileName(FbxPath::ToString(Path).c_str()); T->UVSet.Set(UV); return T;
	};
	auto* Color = Texture("Color", ColorPath, "UV0"); Color->SetScale(2, 2);
	auto* HDRColor = Texture("HDRColor", HDRPath, "UV0");
	auto* HDROpacity = Texture("HDROpacity", HDRPath, "UnusedUV"); HDROpacity->SetScale(3, 3);
	const double Factors[]{0, .4, .25, .2};
	for (int I = 0; I < 4; ++I)
	{
		const FString Name = "Fallback" + std::to_string(I); auto* M = FbxSurfacePhong::Create(S.Scene, Name.c_str());
		M->TransparencyFactor.Set(Factors[I]);
		if (I != 3) M->Diffuse.ConnectSrcObject(I == 2 ? HDRColor : Color);
		M->TransparentColor.ConnectSrcObject(I == 2 ? Color : HDROpacity);
		auto* Static = S.Triangle(Name.c_str()); Static->AddMaterial(M);
		auto* Skinned = S.Triangle((Name + "Skin").c_str()); Skinned->AddMaterial(M); S.Skin(Skinned, {Bone}, {1});
	}
	TArray<FString> Warnings; auto Data = Cook(S.Save("opacity-fallback.fbx"), {}, &Warnings);
	Check(Warnings.Num() == 4 && HasWarning(Warnings, "Opacity baking skipped"), "Unsupported bake formats warn once per material and import succeeds");
	Check(!HasWarning(Warnings, "UV set not found") && !HasWarning(Warnings, "Different texture UV"), "Skipped opacity maps do not constrain usable UVs");
	FString SharedColor; int Materials = 0;
	for (const auto& Asset : Data)
	{
		Check(!Asset.SourceObject.starts_with("OpacityTexture:"), "Unsupported bake creates no derived opacity asset");
		if (Asset.Type == EFbxAssetType::Material)
		{
			++Materials;
			int I = 0; while (I < 4 && !Asset.SourceObject.starts_with("Material:Fallback" + std::to_string(I) + "[")) ++I;
			Check(I < 4, "Fallback material identity"); const auto& M = Asset.Material;
			Check(Near(M.DiffuseOrFactor.W, 1) && M.BlendState == EBlendState::Opaque, "Skipped opacity map keeps material opaque");
			Check(M.DiffuseTexture.empty() == (I == 3), "Fallback preserves presence of original Diffuse");
			if (I == 0) SharedColor = M.DiffuseTexture;
			if (I == 1) Check(M.DiffuseTexture == SharedColor, "Fallback retains shared Diffuse asset");
		}
		if (Asset.Type == EFbxAssetType::StaticMesh && Asset.SourceObject.find("Fallback0") != FString::npos)
			Check(Near(Asset.LODs[0].MeshData.Vertices[1].UV.X, 2), "Remaining Diffuse UV transform is baked after opacity skip");
	}
	Check(Materials == 4 && Type(Data, EFbxAssetType::StaticMesh).size() == 4 && Type(Data, EFbxAssetType::SkeletalMesh).size() == 1, "Fallback retains all static/skeletal geometry and materials");
}

void ExternalSourceAndPublish(UMaterial* Base)
{
	const fs::path External = "Build/.fbx-external-test-fixtures";
	Check(!fs::exists(External), "External fixture directory already exists; review it before removing");
	const fs::path Original = External / "original", Moved = External / "moved";
	fs::create_directories(Original / FbxPath::FromString("한글"));
	const fs::path ImagePath = Moved / FbxPath::FromString("한글/pixel.bmp");
	const fs::path OriginalImage = Original / FbxPath::FromString("한글/pixel.bmp");
	WriteImage(OriginalImage);
	FScene S;
	auto* Static = S.Triangle("ExternalStatic");
	auto* Bone = S.Bone("ExternalRig");
	auto* Skinned = S.Triangle("ExternalSkin"); S.Skin(Skinned, {Bone}, {1});
	auto* Material = FbxSurfacePhong::Create(S.Scene, "ExternalPaint");
	auto* Texture = FbxFileTexture::Create(S.Scene, "ExternalTexture");
	Texture->SetFileName(FbxPath::ToString(fs::absolute(OriginalImage)).c_str());
	Texture->SetRelativeFileName("한글/pixel.bmp");
	Material->Diffuse.ConnectSrcObject(Texture); Material->NormalMap.ConnectSrcObject(Texture);
	Static->AddMaterial(Material); Skinned->AddMaterial(Material);
	S.Save("external.fbx", false, Original);
	Check(Original.parent_path() == External && Moved.parent_path() == External, "External fixture move boundary");
	fs::rename(Original, Moved);
	const FString Source = FbxPath::ToString(Moved / "external.fbx");
	const FString Destination = FbxPath::ToString(Root / FbxPath::FromString("선택한출력"));
	Check(FbxPath::DefaultOutputDirectory(Source) == "Assets/Imported/external", "Default output is separate from source");
	const FString Denied = FbxPath::ToString(External / "denied-output");
	auto Rejected = FFbxAssetLoader::Import(Source, {}, Denied);
	Check(!Rejected.bSuccess && !fs::exists(FbxPath::FromString(Denied)), "Outside output folder rejected before writing");
	TArray<FFbxAssetData> Data; TArray<FString> Warnings; FString Error;
	Check(!FFbxImporter::Cook(Source, {}, FbxPath::ToString(External / "extracted"), Data, Warnings, Error, Destination),
		"Embedded extraction outside Assets rejected");
	std::set<FString> SourceFiles;
	for (const auto& Entry : fs::recursive_directory_iterator(External)) SourceFiles.insert(FbxPath::ToString(Entry.path()));
	auto First = FFbxAssetLoader::Import(Source, {}, Destination); Check(First.bSuccess, First.Error);
	FString StaticKey, SkeletalKey;
	for (const auto& Key : First.AssetKeys)
	{
		Check(Key.starts_with(".fbx-test-fixtures/선택한출력/"), "Every output is inside the selected destination");
		if (UAssetManager::GetAssetByPath<UStaticMesh>(Key)) StaticKey = Key;
		if (UAssetManager::GetAssetByPath<USkeletalMesh>(Key)) SkeletalKey = Key;
	}
	Check(!StaticKey.empty() && !SkeletalKey.empty(), "External static and skeletal outputs registered");
	std::set<FString> AfterFiles;
	for (const auto& Entry : fs::recursive_directory_iterator(External)) AfterFiles.insert(FbxPath::ToString(Entry.path()));
	Check(SourceFiles == AfterFiles, "Import does not write beside external source");
	auto Second = FFbxAssetLoader::Import(Source, {}, Destination); Check(Second.bSuccess, Second.Error);
	Check(First.AssetKeys.Num() == Second.AssetKeys.Num() && std::equal(First.AssetKeys.begin(), First.AssetKeys.end(), Second.AssetKeys.begin()),
		"External reimport keeps output keys");
	// 동일한 파일명/노드명을 가진 다른 외부 소스가 기존 출력을 덮어쓰지 않는다.
	fs::create_directory(External / "other"); FScene Other; Other.Triangle("ExternalStatic");
	const FString OtherSource = Other.Save("external.fbx", false, External / "other");
	auto Collision = FFbxAssetLoader::Import(OtherSource, {}, Destination); Check(Collision.bSuccess, Collision.Error);
	for (const auto& Key : Collision.AssetKeys)
		Check(std::find(First.AssetKeys.begin(), First.AssetKeys.end(), Key) == First.AssetKeys.end(), "External source name collision gets a distinct key");
	auto CollisionAgain = FFbxAssetLoader::Import(OtherSource, {}, Destination); Check(CollisionAgain.bSuccess, CollisionAgain.Error);
	Check(Collision.AssetKeys.Num() == CollisionAgain.AssetKeys.Num() &&
		std::equal(Collision.AssetKeys.begin(), Collision.AssetKeys.end(), CollisionAgain.AssetKeys.begin()), "Collision suffix is stable on reimport");
	// 외부 FBX의 내장 이미지도 출력 폴더의 임시 디렉터리로 추출한다.
	Texture->SetFileName(FbxPath::ToString(fs::absolute(ImagePath)).c_str());
	const FString EmbeddedSource = S.Save("embedded-external.fbx", true, External);
	auto Embedded = FFbxAssetLoader::Import(EmbeddedSource, {}, FbxPath::ToString(Root / "external-embedded"));
	Check(Embedded.bSuccess, Embedded.Error);
	fs::remove(FbxPath::FromString(Source)); fs::remove(FbxPath::FromString(EmbeddedSource)); fs::remove(ImagePath);
	UAssetManager::Shutdown(); UAssetManager::Get().RegisterAsset("DefaultMaterial", Base);
	for (const auto& Key : {StaticKey, SkeletalKey})
	{
		fs::path File; Check(FbxPath::ResolveKey(Key, File, Error), Error);
		auto* Object = FFbxAssetLoader::LoadAsset(FbxPath::ToString(File), Error); Check(Object != nullptr, Error);
		if (auto* Mesh = Cast<UStaticMesh>(Object))
			Check(Mesh->VertexBuffer && !Mesh->Materials.IsEmpty() && Mesh->Materials[0]->Textures[0]->GetWidth() == 1,
				"External static geometry/material/texture reload without source files");
		if (auto* Mesh = Cast<USkeletalMesh>(Object))
			Check(Mesh->Skeleton && !Mesh->Materials.IsEmpty() && Mesh->Materials[0]->Textures[0]->GetWidth() == 1 &&
				Mesh->SkinWeights.InfluenceBones.Num() == Mesh->MeshData.Vertices.Num(),
				"External skeletal geometry/skeleton/material/texture reload without source files");
	}
	for (const auto& Key : Embedded.AssetKeys)
	{
		fs::path File; Check(FbxPath::ResolveKey(Key, File, Error), Error);
		Check(FFbxAssetLoader::LoadAsset(FbxPath::ToString(File), Error) != nullptr, Error);
	}
	const auto Target = fs::canonical(External), Build = fs::canonical("Build");
	Check(Target.parent_path() == Build && Target.filename() == ".fbx-external-test-fixtures", "External fixture cleanup boundary");
	fs::remove_all(Target);
	std::cout << "PASS external sources, selected outputs, collision handling and binary-only reload\n";
}

void TransparencyPublish(UMaterial* Base)
{
	Base->ParamLayout = EMaterialParamLayout::StaticMesh;
	const FString Source = FbxPath::ToString(Root / "transparency.fbx");
	auto First = FFbxAssetLoader::Import(Source, {}, FbxPath::ToString(Root)); Check(First.bSuccess, First.Error);
	auto Second = FFbxAssetLoader::Import(Source, {}, FbxPath::ToString(Root)); Check(Second.bSuccess, Second.Error);
	Check(First.AssetKeys.Num() == Second.AssetKeys.Num() && std::equal(First.AssetKeys.begin(), First.AssetKeys.end(), Second.AssetKeys.begin()), "Generated opacity texture keys stable on reimport");
	for (const auto* File : {"transparency.fbx", "opacity-color.tga", "opacity-alpha.tga", "opacity-gray.tga", "opacity-white.tga", "opacity-wide.tga"})
		Check(fs::remove(Root / File), "Remove test source before opacity binary-only reload");
	UAssetManager::Shutdown(); UAssetManager::Get().RegisterAsset("DefaultMaterial", Base);
	FString Error; int Materials = 0, Meshes = 0;
	for (const auto& Key : First.AssetKeys)
	{
		fs::path Path; Check(FbxPath::ResolveKey(Key, Path, Error), Error);
		FFbxAssetData Data; Check(FFbxAssetBake::ReadBaked(FbxPath::ToString(Path), Data, Error), Error);
		auto* Object = FFbxAssetLoader::LoadAsset(FbxPath::ToString(Path), Error); Check(Object != nullptr, Error);
		if (auto* Material = Cast<UMaterial>(Object))
		{
			++Materials;
			Check(Material->BlendState == Data.Material.BlendState, "Binary loader uses stored BlendState");
			const bool Opaque = Data.SourceObject.starts_with("Material:OpaqueShared[") || Data.SourceObject.starts_with("Material:Constant[");
			Check(Material->BlendState == (Opaque ? EBlendState::Opaque : EBlendState::AlphaBlend), "Stored mode survives alpha=1 and source deletion");
			Check(Material->DepthStencilState == (Opaque ? EDepthStencilState::Default : EDepthStencilState::ReadOnly), "Imported blend selects the existing depth state");
			if (Data.SourceObject.starts_with("Material:AlphaShared["))
			{
				ComPtr<ID3D11Texture2D> SourceTexture;
				Check(SUCCEEDED(Material->Textures[0]->GetResource()->GetRawPtr()->QueryInterface(IID_PPV_ARGS(SourceTexture.GetAddressOf()))), "Get generated opacity GPU image");
				D3D11_TEXTURE2D_DESC Desc{}; SourceTexture->GetDesc(&Desc);
				Desc.Usage = D3D11_USAGE_STAGING; Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				Desc.BindFlags = 0; Desc.MiscFlags = 0; Desc.MipLevels = 1;
				ComPtr<ID3D11Texture2D> Readback;
				Check(SUCCEEDED(RenderCommand::GetDevice()->CreateTexture2D(&Desc, nullptr, Readback.GetAddressOf())), "Create opacity GPU readback");
				RenderCommand::GetContext()->CopySubresourceRegion(Readback.Get(), 0, 0, 0, 0, SourceTexture.Get(), 0, nullptr);
				D3D11_MAPPED_SUBRESOURCE Mapped{};
				Check(SUCCEEDED(RenderCommand::GetContext()->Map(Readback.Get(), 0, D3D11_MAP_READ, 0, &Mapped)), "Read uploaded alpha pixels");
				const auto* Pixels = static_cast<const uint8*>(Mapped.pData);
				const bool CorrectAlpha = Pixels[3] == 64 && Pixels[7] == 192;
				RenderCommand::GetContext()->Unmap(Readback.Get(), 0);
				Check(CorrectAlpha, "Generated alpha reaches the existing shader texture slot unchanged");
			}
		}
		if (Cast<UStaticMesh>(Object) || Cast<USkeletalMesh>(Object)) ++Meshes;
	}
	Check(Materials == 9 && Meshes == 10, "Static and skeletal opacity materials reload without source files");
}

void MaterialSerialization(UMaterial* Base)
{
	auto* Source = UMaterial::CreateInstance(Base);
	Source->Textures.Reset(); Source->DiffuseOrFactor = FVector4(.2f, .4f, .6f, .75f); Source->Shininess = 68;
	Source->SpecularColor = FVector(.2f, .5f, .8f); Source->SpecularFactor = .25f;
	Source->EmissiveColor = FVector(.1f, .2f, .4f); Source->EmissiveFactor = 3.f;
	auto Verify = [](const UMaterial* M)
	{
		Check(M && Near(M->DiffuseOrFactor.X, .2f) && Near(M->DiffuseOrFactor.Y, .4f) &&
			Near(M->DiffuseOrFactor.Z, .6f) && Near(M->DiffuseOrFactor.W, .75f) && Near(M->Shininess, 68), "Material color/exponent serialization");
		Check(Near(M->SpecularColor.X, .2f) && Near(M->SpecularColor.Y, .5f) && Near(M->SpecularColor.Z, .8f) && Near(M->SpecularFactor, .25f) &&
			Near(M->EmissiveColor.X, .1f) && Near(M->EmissiveColor.Y, .2f) && Near(M->EmissiveColor.Z, .4f) && Near(M->EmissiveFactor, 3), "Specular and emissive serialization");
	};
	auto Inline = UMaterial::SaveMaterial(Source);
	Check(Inline.contains("DiffuseOrFactor") && !Inline.contains("BaseColor"), "Save current material property name");
	Verify(UMaterial::LoadMaterial(Inline));
	{
		auto Legacy = Inline;
		for (const char* Name : {"SpecularColor", "SpecularFactor", "EmissiveColor", "EmissiveFactor"}) Legacy.erase(Name);
		auto* Loaded = UMaterial::LoadMaterial(Legacy);
		Check(Loaded && Near(Loaded->SpecularColor.X, Base->SpecularColor.X) && Near(Loaded->SpecularFactor, Base->SpecularFactor) &&
			Near(Loaded->EmissiveColor.Z, Base->EmissiveColor.Z) && Near(Loaded->EmissiveFactor, Base->EmissiveFactor), "Old inline materials retain parent lighting defaults");
	}
	{
		auto Legacy = Inline; Legacy["BaseColor"] = Legacy["DiffuseOrFactor"]; Legacy.erase("DiffuseOrFactor");
		Verify(UMaterial::LoadMaterial(Legacy));
	}
	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes); FJsonArchiveOutputFormatter Formatter(Writer); FStructuredArchive Archive(Formatter);
		Source->Serialize(Archive.Open().EnterRecord());
	}
	auto Document = json::parse(FString(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num()));
	Check(Document.contains("DiffuseOrFactor") && !Document.contains("BaseColor"), "Structured JSON current property name");
	{
		auto Legacy = Document;
		for (const char* Name : {"SpecularColor", "SpecularFactor", "EmissiveColor", "EmissiveFactor"}) Legacy.erase(Name);
		const auto Text = Legacy.dump(); TArray<uint8> Input;
		for (char C : Text) Input.Add(static_cast<uint8>(C));
		FMemoryReader Reader(Input); FJsonArchiveInputFormatter Formatter(Reader); FStructuredArchive Archive(Formatter);
		auto* Target = UMaterial::CreateInstance(Base); Target->Serialize(Archive.Open().EnterRecord());
		Check(!Reader.IsError() && Near(Target->SpecularColor.X, Base->SpecularColor.X) && Near(Target->SpecularFactor, Base->SpecularFactor) &&
			Near(Target->EmissiveColor.Z, Base->EmissiveColor.Z) && Near(Target->EmissiveFactor, Base->EmissiveFactor), "Old scene materials retain parent lighting defaults");
	}
	for (const char* Name : {"DiffuseOrFactor", "BaseColor"})
	{
		auto Versioned = Document;
		if (FString(Name) != "DiffuseOrFactor") { Versioned[Name] = Versioned["DiffuseOrFactor"]; Versioned.erase("DiffuseOrFactor"); }
		const auto Text = Versioned.dump(); TArray<uint8> Input;
		for (char C : Text) Input.Add(static_cast<uint8>(C));
		FMemoryReader Reader(Input); FJsonArchiveInputFormatter Formatter(Reader); FStructuredArchive Archive(Formatter);
		auto* Target = UMaterial::CreateInstance(Base); Target->Serialize(Archive.Open().EnterRecord());
		Check(!Reader.IsError(), "Structured JSON load"); Verify(Target);
	}
	Bytes.Reset();
	{
		FMemoryWriter Writer(Bytes); FBinaryArchiveFormatter Formatter(Writer); FStructuredArchive Archive(Formatter);
		Source->Serialize(Archive.Open().EnterRecord());
	}
	{
		FMemoryReader Reader(Bytes); FBinaryArchiveFormatter Formatter(Reader); FStructuredArchive Archive(Formatter);
		auto* Target = UMaterial::CreateInstance(Base); Target->Serialize(Archive.Open().EnterRecord());
		Verify(Target); Check(!Reader.IsError() && Reader.Tell() == Bytes.Num(), "Binary layout remains aligned");
	}
}

void QuaternionTransforms()
{
	const FRotator Euler(23.0f, -47.0f, 91.0f);
	FTransform Source(Euler.Quaternion(), FVector(2, -3, 5), FVector(2, 3, 4));
	auto Verify = [&](const FTransform& Value)
	{
		const FMatrix Expected = Source.GetLocalMatrix(), Actual = Value.GetLocalMatrix();
		for (int Row = 0; Row < 4; ++Row)
			for (int Col = 0; Col < 4; ++Col)
				Check(Near(Expected.M[Row][Col], Actual.M[Row][Col]), "Quaternion Transform matrix preserved");
	};
	const FMatrix Rotation = Euler.Quaternion().ToFMatrix();
	const FMatrix Matrix = Source.GetLocalMatrix();
	Check(Near(Matrix.M[0][1], Rotation.M[0][1] * 2) && Near(Matrix.M[3][2], 5), "Scale and translation preserved");
	Check(Near(Source.GetForward().X, Euler.Quaternion().GetForwardVector().X), "Direction uses stored quaternion");
	Check(Near(FTransform::Identity.Rotation.W, 1), "Identity quaternion");
	json Json = Source;
	Check(Json["Rotation"].size() == 4, "JSON writes quaternion components");
	Verify(Json.get<FTransform>());
	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes); FJsonArchiveOutputFormatter Formatter(Writer); FStructuredArchive Archive(Formatter);
		Archive.Open() << Source;
	}
	auto Document = json::parse(FString(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num()));
	Check(Document["Rotation"].contains("W"), "Structured JSON writes quaternion");
	{
		const auto Text = Document.dump(); TArray<uint8> Input;
		for (char C : Text) Input.Add(static_cast<uint8>(C));
		FMemoryReader Reader(Input); FJsonArchiveInputFormatter Formatter(Reader); FStructuredArchive Archive(Formatter);
		FTransform Target; Archive.Open() << Target; Verify(Target);
		Check(!Reader.IsError(), "Structured Transform read");
	}
	Bytes.Reset();
	{
		FMemoryWriter Writer(Bytes); FBinaryArchiveFormatter Formatter(Writer); FStructuredArchive Archive(Formatter);
		Archive.Open() << Source;
	}
	{
		FMemoryReader Reader(Bytes); FBinaryArchiveFormatter Formatter(Reader); FStructuredArchive Archive(Formatter);
		FTransform Target; Archive.Open() << Target; Verify(Target);
		Check(!Reader.IsError() && Reader.Tell() == Bytes.Num(), "Binary quaternion layout aligned");
	}
}

void QuaternionBakeRoundTrip()
{
	const FTransform Expected(FQuat(0.2f, -0.3f, 0.4f, 0.5f).Normalized(), FVector(2, -3, 5), FVector(2, 3, 4));
	FFbxAssetData Source;
	Source.Type = EFbxAssetType::Skeleton;
	Source.SourcePath = "test.fbx";
	Source.SourceObject = "Skeleton";
	Source.RefSkeleton.RefBoneInfo.Add(FMeshBoneInfo{ "Root", -1 });
	Source.RefSkeleton.RefBonePose.Add(Expected);
	const auto Path = Root / "quaternion.fbxbin";
	FString Error;
	Check(FFbxAssetBake::WriteBaked(FbxPath::ToString(Path), Source, Error), Error);
	FFbxAssetData Loaded;
	Check(FFbxAssetBake::ReadBaked(FbxPath::ToString(Path), Loaded, Error), Error);
	const FMatrix A = Expected.GetLocalMatrix(), B = Loaded.RefSkeleton.RefBonePose[0].GetLocalMatrix();
	for (int Row = 0; Row < 4; ++Row)
		for (int Col = 0; Col < 4; ++Col)
			Check(Near(A.M[Row][Col], B.M[Row][Col]), "Quaternion bake preserves transform");
}

void BinaryAndPublish()
{
	FString Error; auto Data = Cook(FbxPath::ToString(Root / "skeletal.fbx"));
	for (const auto& D : Data)
	{
		const auto File = FbxPath::ToString(Root / "roundtrip.fbxbin"); FFbxAssetData Read;
		Check(FFbxAssetBake::WriteBaked(File, D, Error) && FFbxAssetBake::ReadBaked(File, Read, Error), Error);
		Check(Read.Type == D.Type && Read.SourceObject == D.SourceObject, "Binary identity roundtrip");
		fs::resize_file(FbxPath::FromString(File), 13); Check(!FFbxAssetBake::ReadBaked(File, Read, Error), "Truncated binary rejected");
		Check(FFbxAssetBake::WriteBaked(File, D, Error), Error);
		std::fstream F(FbxPath::FromString(File), std::ios::binary | std::ios::in | std::ios::out);
		F.seekp(4); uint32 Bad = 999; F.write(reinterpret_cast<const char*>(&Bad), sizeof(Bad)); F.close();
		Check(!FFbxAssetBake::ReadBaked(File, Read, Error), "Unsupported binary version rejected");
	}
	FRenderDevice Device; RenderCommand::Init(&Device);
	UAssetManager::Get().CreateDefaultTextures();
	auto* Base = NewObject<UMaterial>(); Base->Textures.Add(UAssetManager::GetAssetByPath<UTexture2D>("WhiteTexture"));
	Base->Textures.Add(UAssetManager::GetAssetByPath<UTexture2D>("FlatNormalTexture")); UAssetManager::Get().RegisterAsset("DefaultMaterial", Base);
	MaterialSerialization(Base);
	auto Textured = FFbxAssetLoader::Import(FbxPath::ToString(Root / "embedded.fbx"), {}, FbxPath::ToString(Root)); Check(Textured.bSuccess, Textured.Error);
	bool HasTexture = false, HasMaterial = false;
	for (const auto& Key : Textured.AssetKeys)
	{
		if (auto* T = UAssetManager::GetAssetByPath<UTexture2D>(Key)) { HasTexture = true; Check(T->GetWidth() == 1 && T->GetResource()->GetSRV(), "Baked image GPU upload"); }
		if (auto* M = UAssetManager::GetAssetByPath<UMaterial>(Key))
		{
			HasMaterial = true; Check(M->Textures[0]->GetWidth() == 1 && !M->GetPath().empty(), "Registered material uses imported texture");
			Check(Near(M->SpecularColor.X, .2f) && Near(M->SpecularFactor, .25f) && Near(M->EmissiveColor.Z, .4f) && Near(M->EmissiveFactor, 3), "Cooked material lighting reaches runtime instance");
		}
	}
	Check(HasTexture && HasMaterial, "Material/texture batch publish");
	const auto Path = FbxPath::ToString(Root / "static.fbx");
	auto First = FFbxAssetLoader::Import(Path, {}, FbxPath::ToString(Root)); Check(First.bSuccess, First.Error);
	auto* Old = UAssetManager::GetAssetByPath<UStaticMesh>(First.AssetKeys[0]); Check(Old != nullptr, "Published static mesh");
	auto Second = FFbxAssetLoader::Import(Path, {}, FbxPath::ToString(Root)); Check(Second.bSuccess, Second.Error);
	Check(First.AssetKeys.Num() == Second.AssetKeys.Num() && std::equal(First.AssetKeys.begin(), First.AssetKeys.end(), Second.AssetKeys.begin()), "Stable output names on reimport");
	Check(Old->IsRetired() && Old->VertexBuffer && UAssetManager::GetAssetByPath<UStaticMesh>(First.AssetKeys[0]) != Old, "Retired objects retain resources; registry selects new object");
	// 페이로드 버전이 바뀌어도 공통 출처 정보로 같은 출력 파일을 찾는다.
	fs::path FirstFile; Check(FbxPath::ResolveKey(First.AssetKeys[0], FirstFile, Error), Error);
	std::fstream Versioned(FirstFile, std::ios::binary | std::ios::in | std::ios::out);
	Versioned.seekp(4); uint32 OldVersion = 999; Versioned.write(reinterpret_cast<const char*>(&OldVersion), 4); Versioned.close();
	auto Upgraded = FFbxAssetLoader::Import(Path, {}, FbxPath::ToString(Root)); Check(Upgraded.bSuccess, Upgraded.Error);
	Check(Upgraded.AssetKeys[0] == First.AssetKeys[0], "Reimport replaces unsupported binary version at the same key");
	// 두 번째 파일을 잠가 첫 번째 교체까지 수행한 뒤 실제 디스크 롤백을 검증한다.
	fs::path LockedFile; Check(FbxPath::ResolveKey(First.AssetKeys[1], LockedFile, Error), Error);
	auto ReadBytes = [](const fs::path& P) { std::ifstream In(P, std::ios::binary); return FString(std::istreambuf_iterator<char>(In), {}); };
	const auto BeforeRollback = ReadBytes(FirstFile);
	auto* BeforeObject = UAssetManager::GetAssetByPath<UStaticMesh>(First.AssetKeys[0]);
	HANDLE Lock = CreateFileW(LockedFile.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	Check(Lock != INVALID_HANDLE_VALUE, "Lock output fixture");
	auto RolledBack = FFbxAssetLoader::Import(Path, {}, FbxPath::ToString(Root)); CloseHandle(Lock);
	Check(!RolledBack.bSuccess && ReadBytes(FirstFile) == BeforeRollback && !BeforeObject->IsRetired() &&
		UAssetManager::GetAssetByPath<UStaticMesh>(First.AssetKeys[0]) == BeforeObject, "Failed disk replacement restores old files and registry");
	std::ifstream Before(FbxPath::FromString(Path), std::ios::binary); FString Bytes((std::istreambuf_iterator<char>(Before)), {}); Before.close();
	std::ofstream Broken(FbxPath::FromString(Path), std::ios::binary | std::ios::trunc); Broken << "invalid"; Broken.close();
	auto* Active = UAssetManager::GetAssetByPath<UStaticMesh>(First.AssetKeys[0]);
	auto Failed = FFbxAssetLoader::Import(Path, {}, FbxPath::ToString(Root));
	Check(!Failed.bSuccess && !Active->IsRetired() && UAssetManager::GetAssetByPath<UStaticMesh>(First.AssetKeys[0]) == Active, "Failed import preserves active objects");
	std::ofstream Restore(FbxPath::FromString(Path), std::ios::binary | std::ios::trunc); Restore.write(Bytes.data(), Bytes.size()); Restore.close();
	auto Skinned = FFbxAssetLoader::Import(FbxPath::ToString(Root / "skeletal.fbx"), {}, FbxPath::ToString(Root)); Check(Skinned.bSuccess, Skinned.Error);
	FString SkeletalKey;
	for (const auto& Key : Skinned.AssetKeys) if (auto* Mesh = UAssetManager::GetAssetByPath<USkeletalMesh>(Key))
	{
		SkeletalKey = Key;
		Check(Mesh->Skeleton && Mesh->RefBasesInvMatrix.Num() == Mesh->RefSkeleton.GetNum(), "Skeletal runtime asset/inverse bind cache");
	}
	Check(!SkeletalKey.empty(), "Skeletal asset registered");
	UAssetManager::Shutdown();
	UAssetManager::Get().RegisterAsset("DefaultMaterial", Base);
	fs::path SkeletalFile; Check(FbxPath::ResolveKey(SkeletalKey, SkeletalFile, Error), Error);
	auto* Reloaded = Cast<USkeletalMesh>(FFbxAssetLoader::LoadAsset(FbxPath::ToString(SkeletalFile), Error));
	Check(Reloaded && Reloaded->Skeleton && Reloaded->SkinWeights.InfluenceBones.Num() == Reloaded->MeshData.Vertices.Num(), "Binary-only loading resolves skeleton/material dependencies");
	Base->UVTiling = FVector2(3, 4); Base->UVScrollSpeed = FVector2(1, 2); Base->WorldUVTileSize = FVector2(2, 2);
	auto UVImported = FFbxAssetLoader::Import(FbxPath::ToString(Root / "uv-selected.fbx"), {}, FbxPath::ToString(Root));
	Check(UVImported.bSuccess, UVImported.Error);
	std::vector<FString> UVKeys;
	for (const auto& Key : UVImported.AssetKeys)
		if (UAssetManager::GetAssetByPath<UStaticMesh>(Key) || UAssetManager::GetAssetByPath<USkeletalMesh>(Key)) UVKeys.push_back(Key);
	Check(UVKeys.size() == 2, "UV fixture publishes static and skeletal assets");
	UAssetManager::Shutdown(); UAssetManager::Get().RegisterAsset("DefaultMaterial", Base);
	for (const auto& Key : UVKeys)
	{
		fs::path File; Check(FbxPath::ResolveKey(Key, File, Error), Error);
		auto* Object = FFbxAssetLoader::LoadAsset(FbxPath::ToString(File), Error); Check(Object != nullptr, Error);
		const FStaticMeshData* Geometry = nullptr; UMaterial* Material = nullptr;
		if (auto* M = Cast<UStaticMesh>(Object)) { Geometry = &M->MeshData; Material = M->Materials[0]; }
		if (auto* M = Cast<USkeletalMesh>(Object)) { Geometry = &M->MeshData; Material = M->Materials[0]; }
		Check(Geometry && Material && Near(Geometry->Vertices[0].UV.X, .25f) && Near(Geometry->Vertices[0].Tangent.Y, -1) &&
			Near(Geometry->Vertices[0].Tangent.X, 0), "Binary-only static/skeletal loading preserves chosen UV and authored tangent");
		Check(Material->SamplerState == ESamplerState::LinearWrap && Near(Material->UVTiling.X, 1) && Near(Material->UVTiling.Y, 1) &&
			Near(Material->UVScrollSpeed.X, 0) && Near(Material->WorldUVTileSize.X, 0), "Runtime applies baked Wrap without extra UV transforms");
	}
	TransparencyPublish(Base);
	ExternalSourceAndPublish(Base);
	UAssetManager::Shutdown();
	while (!GUObjectArray.IsEmpty()) delete GUObjectArray.Last();
	RenderCommand::Init(nullptr);
}
}

int main()
{
	try
	{
		Check(!fs::exists(Root), "Test fixture directory already exists; remove it after reviewing its contents");
		fs::create_directories(Root / "extracted");
		QuaternionTransforms(); QuaternionBakeRoundTrip(); std::cout << "PASS quaternion transforms and serialization\n";
		StaticOptions(); std::cout << "PASS static options\n";
		SkeletalOptions(); std::cout << "PASS skeletal options\n";
		LODs(); std::cout << "PASS LOD grouping\n";
		TexturesAndMaterials(); std::cout << "PASS material and embedded/external textures\n";
		BumpPolicy(); std::cout << "PASS Bump-only warning and normal input policy\n";
		MaterialLighting(); std::cout << "PASS specular and emissive material policy\n";
		UVAndTangents(); std::cout << "PASS UV selection/transforms, Wrap and authored tangents\n";
		UVSurfaceBoundaries(); std::cout << "PASS UV surface boundaries, skin weights and partial tangent fallback\n";
		Transparency(); std::cout << "PASS opacity conditions, generated textures, shared images and UV policies\n";
		OpacityBakeFallback(); std::cout << "PASS unsupported opacity formats warn and preserve original material data\n";
		BinaryAndPublish(); std::cout << "PASS binary validation and asset replacement\n";
		FString Key, Error;
		Check(!FbxPath::GetAssetKey("Setup/premake5.lua", Key, Error), "Outside Assets rejected");
		Check(!FbxPath::ResolveKey("../outside.fbxbin", Key, Error), "Escaping asset key rejected");
		fs::path Directory;
		Check(FbxPath::GetOutputDirectory("Assets", Directory, Error), "Assets root is a valid output directory");
		Check(!FbxPath::GetOutputDirectory("Assets/../Build", Directory, Error), "Escaping output directory rejected");
		Check(!FbxPath::GetOutputDirectory("AssetsOther/output", Directory, Error), "Output directory prefix lookalike rejected");
		// 이 실행에서 만든 고정 테스트 폴더만 정리한다.
		const auto Target = fs::canonical(Root), Assets = fs::canonical("Assets");
		Check(Target.parent_path() == Assets && Target.filename() == ".fbx-test-fixtures", "Fixture cleanup boundary");
		fs::remove_all(Target);
		std::cout << "PASS asset path boundaries\n"; return 0;
	}
	catch (const std::exception& E) { std::cerr << "FAIL " << E.what() << '\n'; return 1; }
}
