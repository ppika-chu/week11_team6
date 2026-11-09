#include "EnginePCH.h"
#include "FbxImporter.h"
#include "FbxAssetBake.h"
#include "FbxPath.h"
#include "Math/MathUtility.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#ifndef WITH_FBX_SDK
#define WITH_FBX_SDK 0
#endif

#if WITH_FBX_SDK
#include <fbxsdk.h>

namespace
{
FString Lower(FString S) { for (char& C : S) C = static_cast<char>(std::tolower(static_cast<unsigned char>(C))); return S; }

FString NodePath(FbxNode* Node)
{
	if (!Node || !Node->GetParent()) return "/";
	FString Name = Node->GetName();
	int Occurrence = 0;
	for (int I = 0; I < Node->GetParent()->GetChildCount(); ++I)
	{
		FbxNode* Sibling = Node->GetParent()->GetChild(I);
		if (Sibling == Node) break;
		if (Name == Sibling->GetName()) ++Occurrence;
	}
	return NodePath(Node->GetParent()) + Name + "[" + std::to_string(Occurrence) + "]/";
}

bool HasAttribute(FbxNode* Node, FbxNodeAttribute::EType Type)
{
	for (int I = 0; Node && I < Node->GetNodeAttributeCount(); ++I)
		if (Node->GetNodeAttributeByIndex(I)->GetAttributeType() == Type) return true;
	return false;
}

bool IsCompensation(FbxCluster* Cluster)
{
	return Cluster && !std::strcmp(Cluster->GetUserDataID(), "Maya_ClusterHint") &&
		!std::strcmp(Cluster->GetUserData(), "CompensationCluster");
}

// UE FFbxScene::Internal_GetRootSkeleton의 상향 경계와 Blender Armature 예외를 따른다.
FbxNode* FindRoot(FbxScene* Scene, FbxNode* Link, bool bBlender)
{
	FbxNode* Root = Link;
	while (Root && Root->GetParent() && Root->GetParent() != Scene->GetRootNode())
	{
		FbxNode* Parent = Root->GetParent();
		FbxNodeAttribute* Attribute = Parent->GetNodeAttribute();
		if (!Attribute) break;
		const auto Type = Attribute->GetAttributeType();
		const bool bArmature = bBlender && Lower(Parent->GetName()) == "armature" &&
			(!Parent->GetParent() || Parent->GetParent() == Scene->GetRootNode());
		if (Type != FbxNodeAttribute::eSkeleton && Type != FbxNodeAttribute::eMesh &&
			(Type != FbxNodeAttribute::eNull || bArmature)) break;
		if (Type == FbxNodeAttribute::eMesh && static_cast<FbxMesh*>(Attribute)->GetDeformerCount(FbxDeformer::eSkin)) break;
		Root = Parent;
	}
	return Root;
}

bool DescendsFrom(FbxNode* Node, FbxNode* Root)
{
	for (; Node; Node = Node->GetParent()) if (Node == Root) return true;
	return false;
}

bool CollisionName(FString Name)
{
	Name = Lower(Name);
	// 후속 충돌 임포트는 이 분류에서 별도 데이터 경로로 연결한다.
	for (const char* Prefix : { "ucx_", "ubx_", "usp_", "ucp_", "mcdcX_" })
		if (Name.starts_with(Lower(Prefix))) return true;
	return false;
}

FbxAMatrix Geometry(FbxNode* Node)
{
	FbxAMatrix Out;
	Out.SetT(Node->GetGeometricTranslation(FbxNode::eSourcePivot));
	Out.SetR(Node->GetGeometricRotation(FbxNode::eSourcePivot));
	Out.SetS(Node->GetGeometricScaling(FbxNode::eSourcePivot));
	return Out;
}

FVector Reflect(const FbxVector4& V) { return FVector(static_cast<float>(V[0]), static_cast<float>(-V[1]), static_cast<float>(V[2])); }

// 부모 기준 FBX 행렬을 엔진 좌표계의 위치, 회전, 크기로 바꾼다.
FTransform EnginePose(const FbxAMatrix& Local, double UnitScale)
{
	const FbxQuaternion Q = Local.GetQ();
	const FbxVector4 S = Local.GetS();

	FTransform Pose;
	Pose.Location = Reflect(Local.GetT()) * static_cast<float>(UnitScale);
	Pose.Rotation = FQuat(static_cast<float>(Q[0]), static_cast<float>(-Q[1]), static_cast<float>(Q[2]), static_cast<float>(-Q[3])).Normalized();
	Pose.Scale = FVector(static_cast<float>(S[0]), static_cast<float>(S[1]), static_cast<float>(S[2]));
	return Pose;
}

FMatrix EngineMatrix(const FbxAMatrix& M, double UnitScale, bool bEngineInput = true)
{
	const FbxVector4 O = M.MultT(FbxVector4(0, 0, 0));
	const FVector X = Reflect(M.MultT(FbxVector4(1, 0, 0)) - O);
	const FVector Y = Reflect(M.MultT(FbxVector4(0, 1, 0)) - O) * (bEngineInput ? -1.f : 1.f);
	const FVector Z = Reflect(M.MultT(FbxVector4(0, 0, 1)) - O);
	const FVector T = Reflect(O) * static_cast<float>(UnitScale);
	return FMatrix(X.X, X.Y, X.Z, 0, Y.X, Y.Y, Y.Z, 0, Z.X, Z.Y, Z.Z, 0, T.X, T.Y, T.Z, 1);
}

bool FiniteMatrix(const FMatrix& M)
{
	for (int R = 0; R < 4; ++R) for (int C = 0; C < 4; ++C) if (!std::isfinite(M.M[R][C])) return false;
	return std::abs(M.Determinant()) > 1.e-12f;
}

bool Near(const FbxAMatrix& A, const FbxAMatrix& B)
{
	for (int R = 0; R < 4; ++R) for (int C = 0; C < 4; ++C)
		if (std::abs(A.Get(R, C) - B.Get(R, C)) > 1.e-4 * (1 + std::abs(A.Get(R, C)))) return false;
	return true;
}

FbxFileTexture* FileTexture(FbxSurfaceMaterial* Material, const char* PropertyName)
{
	const FbxProperty Property = Material ? Material->FindProperty(PropertyName) : FbxProperty();
	return Property.IsValid() ? Property.GetSrcObject<FbxFileTexture>(0) : nullptr;
}

// FBX UV에 적용하는 2D affine 변환. 엔진의 V 반전은 이 변환 이후에 수행한다.
struct FUVTransform
{
	double M[2][3]{ {1, 0, 0}, {0, 1, 0} };
	bool bValid = true;
	FbxVector2 Apply(const FbxVector2& UV) const
	{
		return { M[0][0] * UV[0] + M[0][1] * UV[1] + M[0][2], M[1][0] * UV[0] + M[1][1] * UV[1] + M[1][2] };
	}
};

FUVTransform UVTransform(FbxFileTexture* Texture)
{
	FUVTransform Out;
	if (!Texture) return Out;
	FbxAMatrix Scale, Rotation, Translation, ScalePivot, InvScalePivot, RotationPivot, InvRotationPivot;
	Scale.SetS(FbxVector4(Texture->GetScaleU(), Texture->GetScaleV(), 1));
	Rotation.SetR(FbxVector4(Texture->GetRotationU(), Texture->GetRotationV(), Texture->GetRotationW()));
	Translation.SetT(FbxVector4(Texture->GetTranslationU(), Texture->GetTranslationV(), 0));
	const FbxDouble3 SP = Texture->ScalingPivot.Get(), RP = Texture->RotationPivot.Get();
	ScalePivot.SetT(FbxVector4(SP[0], SP[1], SP[2])); InvScalePivot.SetT(FbxVector4(-SP[0], -SP[1], -SP[2]));
	RotationPivot.SetT(FbxVector4(RP[0], RP[1], RP[2])); InvRotationPivot.SetT(FbxVector4(-RP[0], -RP[1], -RP[2]));
	const FbxAMatrix Matrix = Translation * RotationPivot * Rotation * InvRotationPivot * ScalePivot * Scale * InvScalePivot;
	FbxVector4 O = Matrix.MultT(FbxVector4(0, 0, 0)), U = Matrix.MultT(FbxVector4(1, 0, 0)), V = Matrix.MultT(FbxVector4(0, 1, 0));
	if (Texture->GetSwapUV()) { std::swap(O[0], O[1]); std::swap(U[0], U[1]); std::swap(V[0], V[1]); }
	for (int I = 0; I < 2; ++I)
	{
		Out.M[I][0] = U[I] - O[I]; Out.M[I][1] = V[I] - O[I]; Out.M[I][2] = O[I];
		for (double Value : Out.M[I]) Out.bValid &= std::isfinite(Value);
	}
	Out.bValid &= Texture->GetMappingType() == FbxTexture::eUV;
	return Out;
}

bool Near(const FUVTransform& A, const FUVTransform& B)
{
	for (int R = 0; R < 2; ++R) for (int C = 0; C < 3; ++C)
		if (std::abs(A.M[R][C] - B.M[R][C]) > 1.e-6 * (1 + std::max(std::abs(A.M[R][C]), std::abs(B.M[R][C])))) return false;
	return true;
}

struct FSurfaceUV
{
	FString Set;
	FUVTransform Transform;
	FbxGeometryElementTangent* Tangents = nullptr;
	FbxGeometryElementBinormal* Binormals = nullptr;
};

template<class T> bool CornerVector(T* Element, FbxMesh* Mesh, int Polygon, int Corner, FbxVector4& Out)
{
	if (!Element) return false;
	int Index;
	switch (Element->GetMappingMode())
	{
	case FbxLayerElement::eByControlPoint: Index = Mesh->GetPolygonVertex(Polygon, Corner); break;
	case FbxLayerElement::eByPolygonVertex: Index = Mesh->GetPolygonVertexIndex(Polygon) + Corner; break;
	case FbxLayerElement::eByPolygon: Index = Polygon; break;
	case FbxLayerElement::eAllSame: Index = 0; break;
	default: return false;
	}
	if (Element->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
	{
		if (Index < 0 || Index >= Element->GetIndexArray().GetCount()) return false;
		Index = Element->GetIndexArray().GetAt(Index);
	}
	if (Index < 0 || Index >= Element->GetDirectArray().GetCount()) return false;
	Out = Element->GetDirectArray().GetAt(Index);
	return std::isfinite(Out[0]) && std::isfinite(Out[1]) && std::isfinite(Out[2]);
}

bool ImportTangent(const FSurfaceUV& UV, FbxMesh* Mesh, int Polygon, int Corner, const FMatrix& GeometryTransform, FVertex& Vertex)
{
	FbxVector4 T, B;
	if (!CornerVector(UV.Tangents, Mesh, Polygon, Corner, T)) return false;
	if (!CornerVector(UV.Binormals, Mesh, Polygon, Corner, B))
	{
		FbxVector4 N;
		if (!Mesh->GetPolygonVertexNormal(Polygon, Corner, N) || !std::isfinite(T[3]) || std::abs(std::abs(T[3]) - 1) > 1.e-4) return false;
		B = N.CrossProduct(T) * T[3];
	}
	auto Direction = [&](const FbxVector4& V) { return GeometryTransform.TransformVector(FVector(static_cast<float>(V[0]), static_cast<float>(V[1]), static_cast<float>(V[2]))); };
	const FVector GT = Direction(T), GB = Direction(B);
	// 최종 UV의 Jacobian 역행렬로 원본 T/B를 옮긴다. 두 번째 행의 부호는 1-V 변환이다.
	const auto& M = UV.Transform.M;
	const double J00 = M[0][0], J01 = M[0][1], J10 = -M[1][0], J11 = -M[1][1];
	const double Det = J00 * J11 - J01 * J10;
	if (!std::isfinite(Det) || std::abs(Det) < 1.e-12) return false;
	FVector Tangent = GT * static_cast<float>(J11 / Det) + GB * static_cast<float>(-J10 / Det);
	const FVector Bitangent = GT * static_cast<float>(-J01 / Det) + GB * static_cast<float>(J00 / Det);
	Tangent = (Tangent - Vertex.Normal * Vertex.Normal.Dot(Tangent)).GetSafeNormal();
	const float Orientation = Vertex.Normal.Cross(Tangent).Dot(Bitangent);
	if (!std::isfinite(Orientation) || std::abs(Orientation) < 1.e-8f || Tangent.SizeSquared() < 1.e-8f) return false;
	Vertex.Tangent = FVector4(Tangent, Orientation < 0 ? -1.f : 1.f);
	return true;
}

struct FMeshItem
{
	FbxNode* Node = nullptr;
	FbxMesh* Mesh = nullptr;
	FbxNode* Rig = nullptr;
	FbxNode* LODGroupNode = nullptr;
	FString UnitKey;
	FString Label;
	int LOD = -1;
};

struct FRig
{
	FbxNode* Root = nullptr;
	std::set<FbxNode*> Needed;
	std::vector<FbxNode*> Bones;
	std::unordered_map<FbxNode*, uint16> Indices;
	FReferenceSkeleton Reference;
	FbxAMatrix ParentBind;
	FString Key;
	bool bUseTimeZeroAsBindPose = false;
	TArray<FMatrix> TimeZeroRebindMatrices; // 바인드 포즈의 정점을 0초 자세로 옮기는 본별 행렬
};

struct FOutputGroup
{
	FString Identity;
	FString Label;
	FbxNode* Root = nullptr;
	std::vector<FMeshItem*> Items;
};

struct FCornerKey
{
	int CP;
	int Surface;
	FVertex Vertex;
	bool operator==(const FCornerKey& Other) const { return CP == Other.CP && Surface == Other.Surface && !std::memcmp(&Vertex, &Other.Vertex, sizeof(Vertex)); }
};
struct FCornerHash
{
	size_t operator()(const FCornerKey& Key) const
	{
		uint64 Hash = static_cast<uint32>(Key.CP) ^ (static_cast<uint64>(Key.Surface) << 32);
		for (size_t I = 0; I < sizeof(FVertex); ++I) Hash = (Hash ^ reinterpret_cast<const uint8*>(&Key.Vertex)[I]) * 1099511628211ull;
		return Hash;
	}
};

constexpr double DEFAULT_SAMPLERATE = 30.0;
constexpr double MINIMUM_ANIMATION_LENGTH = 1.0 / DEFAULT_SAMPLERATE;

// FBX 파일 프레임레이트를 초당 키 수로 쓴다. (UE UInterchangeGenericAnimationPipeline)
double GetBoneAnimationSampleRate(double FrameRate)
{
	return FrameRate > 0.0 ? FrameRate : DEFAULT_SAMPLERATE;
}

// 이 클립에서 노드의 위치, 회전, 크기 곡선이 하나라도 있는지 본다. (UE FFbxAnimation GetFbxTransformCurves)
bool IsNodeAnimated(FbxNode* Node, FbxAnimStack* Stack)
{
	FbxPropertyT<FbxDouble3>* Properties[]{ &Node->LclTranslation, &Node->LclRotation, &Node->LclScaling };
	const char* Components[]{ FBXSDK_CURVENODE_COMPONENT_X, FBXSDK_CURVENODE_COMPONENT_Y, FBXSDK_CURVENODE_COMPONENT_Z };
	for (int32 LayerIndex = 0; LayerIndex < Stack->GetMemberCount<FbxAnimLayer>(); ++LayerIndex)
		for (auto* Property : Properties) for (const char* Component : Components)
			if (Property->GetCurve(Stack->GetMember<FbxAnimLayer>(LayerIndex), Component, false)) return true;
	return false;
}

// 회전 곡선을 이어지는 값으로 고친다. 예: 359도 다음 1도 → 359도 다음 361도. (UE ApplyUnroll)
void ApplyUnroll(FbxNode* Node, FbxAnimLayer* Layer, FbxAnimCurveFilterUnroll* UnrollFilter)
{
	if (!Node || !Layer || !UnrollFilter) return;

	if (FbxAnimCurveNode* CurveNode = Node->LclRotation.GetCurveNode(Layer))
	{
		FbxAnimCurve* Curves[3]{ CurveNode->GetCurve(0), CurveNode->GetCurve(1), CurveNode->GetCurve(2) };
		EFbxRotationOrder RotationOrder = eEulerXYZ;
		Node->GetRotationOrder(FbxNode::eSourcePivot, RotationOrder);
		UnrollFilter->SetRotationOrder(static_cast<FbxEuler::EOrder>(RotationOrder));
		UnrollFilter->Apply(Curves, 3);
	}

	for (int32 I = 0; I < Node->GetChildCount(); ++I) ApplyUnroll(Node->GetChild(I), Layer, UnrollFilter);
}

// DeepConvertScene은 0번 층만 변환하므로 변환 전에 층을 하나로 합친다. (UE 이전 임포터 MergeAllLayerAnimation)
void MergeAllLayerAnimation(FbxScene* Scene, FbxAnimStack* Stack, int32 ResampleRate)
{
	FbxTime FramePeriod;
	FramePeriod.SetSecondDouble(1.0 / ResampleRate);

	const FbxTimeSpan Span = Stack->GetLocalTimeSpan();
	Stack->BakeLayers(Scene->GetAnimationEvaluator(), Span.GetStart(), Span.GetStop(), FramePeriod);

	FbxAnimCurveFilterUnroll UnrollFilter;
	UnrollFilter.Reset();
	ApplyUnroll(Scene->GetRootNode(), Stack->GetMember<FbxAnimLayer>(0), &UnrollFilter);
}

// 값이 끝까지 같은 키 배열을 길이 1로 줄인다. (UE CompressRawAnimSequenceTrack)
void CompressRawAnimSequenceTrack(FRawAnimSequenceTrack& Track, float MaxPosDiff = 0.0001f, float MaxAngleDiff = 0.0003f, float MaxScaleDiff = 0.0001f)
{
	const auto IsConstant = [](const TArray<FVector>& Keys, float MaxDiff)
	{
		for (int32 I = 1; I < Keys.Num(); ++I)
			if ((Keys[0] - Keys[I]).SizeSquared() > MaxDiff * MaxDiff) return false;
		return true;
	};
	if (IsConstant(Track.PosKeys, MaxPosDiff)) Track.PosKeys.SetNum(1);
	if (IsConstant(Track.ScaleKeys, MaxScaleDiff)) Track.ScaleKeys.SetNum(1);

	bool bConstantRotation = true;
	for (int32 I = 1; I < Track.RotKeys.Num() && bConstantRotation; ++I)
	{
		const float Cos = std::abs(Track.RotKeys[0] | Track.RotKeys[I]);
		const float Error = Cos < 0.9999999f ? std::acos(Cos) / PI : 0.0f; // UE FQuat::Error
		bConstantRotation = Error <= MaxAngleDiff;
	}
	if (bConstantRotation) Track.RotKeys.SetNum(1);
}

class FImport
{
public:
	FbxScene* Scene;
	FFbxImportOptions Options;
	FString SourceKey;
	fs::path Source;
	fs::path Destination;
	double UnitScale = 1;
	double FrameRate = 0;
	bool bBlender = false;
	TArray<FFbxAssetData>& Output;
	TArray<FString>& Warnings;
	FString& Error;
	std::vector<FMeshItem> Meshes;
	std::map<FString, FRig> Rigs;
	std::unordered_map<FbxNode*, FbxAMatrix> ClusterBones;
	std::unordered_map<FbxMesh*, FbxAMatrix> ClusterMeshes;
	std::unordered_map<FbxNode*, FbxAMatrix> BindCache;
	std::unordered_map<FbxSurfaceMaterial*, FString> Materials;
	std::unordered_set<FbxSurfaceMaterial*> SkippedOpacityTextures;
	std::unordered_map<FString, FString> Textures;
	std::unordered_map<FString, FString> FileOwners;
	std::unordered_set<FString> Warned;
	std::vector<FbxPose*> Poses;
	std::vector<FFbxAssetData> Animations;

	FImport(FbxScene* S, const FFbxImportOptions& O, const FString& K, const fs::path& P, const fs::path& D,
		TArray<FFbxAssetData>& Out, TArray<FString>& W, FString& E)
		: Scene(S), Options(O), SourceKey(K), Source(P), Destination(D), Output(Out), Warnings(W), Error(E) {}

	void Warn(const FString& Message) { if (Warned.insert(Message).second) Warnings.Add(Message); }
	void ManageNamespace(FString& ObjectName) const;
	FString GetFbxObjectName(const FbxObject* Object) const;
	FString MakeKey(const char* Prefix, const FString& Label, const FString& Identity);
	FFbxAssetData Data(EFbxAssetType Type, const FString& Key, const FString& Identity)
	{
		FFbxAssetData Out; Out.Type = Type; Out.Key = Key; Out.SourcePath = SourceKey; Out.SourceObject = Identity; Out.Options = Options; return Out;
	}
	void Gather(FbxNode* Node, bool bParentVisible, FbxNode* LODGroup = nullptr, int LOD = -1);
	void FindRigs();
	void PreparePoses();
	bool PoseMatrix(FbxPose* Pose, FbxNode* Node, FbxAMatrix& Out);
	FbxAMatrix Bind(FbxNode* Node);
	FbxAMatrix MeshBind(const FMeshItem& Item);
	bool IsValidBindPose(FbxNode* RootJoint);
	bool BuildRig(FRig& Rig);
	void RebindToTimeZero(const FRig& Rig, FFbxMeshLODData& LOD);
	FString Material(FbxSurfaceMaterial* SourceMaterial);
	FString Texture(FbxProperty Property);
	FString OpacityTexture(FbxSurfaceMaterial* Material, const FString& DiffuseKey, const FString& OpacityKey, const FString& Identity, ESamplerState Sampler);
	FSurfaceUV SurfaceUV(FbxSurfaceMaterial* SourceMaterial, FbxMesh* Mesh, const FString& Context);
	bool CookMesh(const FOutputGroup& Group);
	bool AppendMesh(const FMeshItem& Item, FFbxAssetData& Asset, int LOD, std::map<uint32, std::vector<uint32>>& Sections);
	void LODThresholds(const FOutputGroup& Group, FFbxAssetData& Asset);
	bool GetAnimationRange(FbxNode* RootNode, FbxAnimStack* Stack, double SampleRate, double& OutStart, double& OutStop);
	void CookAnimations(const FRig& Rig);
	bool Run();
};

// 에셋, 본, 슬롯처럼 사람이 보는 이름에만 쓴다. NodePath 같은 구분용 ID는 원래 이름을 유지한다. (UE FFbxHelper::ManageNamespace)
void FImport::ManageNamespace(FString& ObjectName) const
{
	if (Options.bKeepFbxNamespace)
	{
		std::replace(ObjectName.begin(), ObjectName.end(), ':', '_');
		return;
	}
	const size_t LastNamespaceTokenIndex = ObjectName.rfind(':');
	if (LastNamespaceTokenIndex != FString::npos) ObjectName.erase(0, LastNamespaceTokenIndex + 1);
}

// UE FFbxHelper::GetFbxObjectName
FString FImport::GetFbxObjectName(const FbxObject* Object) const
{
	if (!Object) return {};
	FString ObjectName = Object->GetName();
	ManageNamespace(ObjectName);
	return ObjectName;
}

FString FImport::MakeKey(const char* Prefix, const FString& Label, const FString& Identity)
{
	const FString Base = FString(Prefix) + "_" + FbxPath::FileName(FbxPath::ToString(Source.stem())) + "_" + FbxPath::FileName(Label);
	FString Candidate = FbxPath::ToString(Destination / FbxPath::FromString(Base + ".fbxbin"));
	FString Key;
	if (!FbxPath::GetAssetKey(Candidate, Key, Error, false)) return {};
	bool Conflict = false;
	if (auto It = FileOwners.find(Lower(Key)); It != FileOwners.end()) Conflict = It->second != Identity;
	if (fs::exists(FbxPath::FromString(Candidate)))
	{
		FString PreviousSource, PreviousObject;
		// 읽을 수 없는 파일은 덮어쓰지 않고 별도 안정 이름을 선택한다.
		if (!FFbxAssetBake::ReadSourceIdentity(Candidate, PreviousSource, PreviousObject)) Conflict = true;
		else Conflict |= Lower(PreviousSource) != Lower(SourceKey) || PreviousObject != Identity;
	}
	if (Conflict)
	{
		std::ostringstream Hash; Hash << std::hex << FbxPath::StableHash(SourceKey + "/" + Identity);
		Candidate = FbxPath::ToString(Destination / FbxPath::FromString(Base + "_" + Hash.str() + ".fbxbin"));
		if (!FbxPath::GetAssetKey(Candidate, Key, Error, false)) return {};
		if (fs::exists(FbxPath::FromString(Candidate)))
		{
			FString PreviousSource, PreviousObject;
			if (!FFbxAssetBake::ReadSourceIdentity(Candidate, PreviousSource, PreviousObject) ||
				Lower(PreviousSource) != Lower(SourceKey) || PreviousObject != Identity) { Error = "Output identity collision: " + Key; return {}; }
		}
	}
	FileOwners[Lower(Key)] = Identity;
	return Key;
}

void FImport::Gather(FbxNode* Node, bool ParentVisible, FbxNode* Group, int LOD)
{
	const bool Visible = Node->Visibility.Get() > 0 && (!Node->VisibilityInheritance.Get() || ParentVisible);
	for (int A = 0; A < Node->GetNodeAttributeCount(); ++A)
	{
		auto* Attribute = Node->GetNodeAttributeByIndex(A);
		if (Attribute->GetAttributeType() != FbxNodeAttribute::eMesh || !Visible || CollisionName(Node->GetName())) continue;
		auto* Mesh = static_cast<FbxMesh*>(Attribute);
		FMeshItem Item; Item.Node = Node; Item.Mesh = Mesh; Item.LODGroupNode = Group; Item.LOD = LOD;
		Item.Label = GetFbxObjectName(Group ? Group : Node);
		Item.UnitKey = Group ? NodePath(Group) : NodePath(Node) + "Mesh[" + std::to_string(A) + "]";
		if (!Group)
		{
			std::smatch Match;
			const FString Name = Node->GetName();
			if (std::regex_match(Name, Match, std::regex("(.+)_LOD([0-9]+)", std::regex::icase)))
			{
				// 구분용 ID는 원래 이름, 표시 이름만 네임스페이스를 처리한다
				Item.UnitKey = NodePath(Node->GetParent()) + "NamedLOD/" + Match[1].str();
				Item.Label = Match[1].str();
				ManageNamespace(Item.Label);
				try { Item.LOD = std::stoi(Match[2].str()); } catch (...) { Item.LOD = 4; }
			}
		}
		if (Item.LOD < 4) Meshes.push_back(std::move(Item));
		else Warn("LOD4 and later geometry is omitted: " + Item.UnitKey);
	}
	const bool NewGroup = !Group && HasAttribute(Node, FbxNodeAttribute::eLODGroup);
	for (int I = 0; I < Node->GetChildCount(); ++I)
		Gather(Node->GetChild(I), Visible, NewGroup ? Node : Group, NewGroup ? I : LOD);
}

void FImport::FindRigs()
{
	std::set<FbxNode*> Links;
	for (int N = 0; N < Scene->GetNodeCount(); ++N)
	{
		FbxNode* Node = Scene->GetNode(N);
		if (HasAttribute(Node, FbxNodeAttribute::eSkeleton)) Links.insert(Node);
	}
	for (auto& Item : Meshes)
	{
		for (int D = 0; D < Item.Mesh->GetDeformerCount(FbxDeformer::eSkin); ++D)
		{
			auto* Skin = static_cast<FbxSkin*>(Item.Mesh->GetDeformer(D, FbxDeformer::eSkin));
			if (D) Warn("Additional Skin deformers are ignored: " + NodePath(Item.Node));
			if (Skin->GetSkinningType() != FbxSkin::eLinear && Skin->GetSkinningType() != FbxSkin::eRigid)
				Warn("DQ/Blend skinning is imported as Linear: " + NodePath(Item.Node));
			for (int C = 0; C < Skin->GetClusterCount(); ++C)
			{
				FbxCluster* Cluster = Skin->GetCluster(C);
				if (!Cluster || !Cluster->GetLink() || IsCompensation(Cluster)) continue;
				FbxNode* Link = Cluster->GetLink(); Links.insert(Link);
				FbxAMatrix B, M; Cluster->GetTransformLinkMatrix(B); Cluster->GetTransformMatrix(M);
				if (auto It = ClusterBones.find(Link); It != ClusterBones.end())
				{ if (!Near(It->second, B)) Warn("Conflicting bind matrices; selected bind pose retained: " + NodePath(Link)); }
				else ClusterBones.emplace(Link, B);
				if (auto It = ClusterMeshes.find(Item.Mesh); It != ClusterMeshes.end())
				{ if (!Near(It->second, M)) Warn("Conflicting mesh bind matrices; first Cluster fallback retained: " + NodePath(Item.Node)); }
				else ClusterMeshes.emplace(Item.Mesh, M);
				if (D == 0)
				{
					FbxNode* Root = FindRoot(Scene, Link, bBlender);
					if (Item.Rig && Item.Rig != Root) { Error = "One mesh references incompatible rig roots: " + NodePath(Item.Node); return; }
					Item.Rig = Root;
				}
			}
		}
	}
	for (FbxNode* Link : Links)
	{
		FbxNode* Root = FindRoot(Scene, Link, bBlender);
		if (!Root || Root == Scene->GetRootNode()) continue;
		auto& Rig = Rigs[NodePath(Root)]; Rig.Root = Root;
		for (FbxNode* N = Link; N && DescendsFrom(N, Root); N = N->GetParent()) Rig.Needed.insert(N);
	}
	for (auto& Item : Meshes)
	{
		if (!Item.Rig)
		{
			for (auto& [Path, Rig] : Rigs)
				if (DescendsFrom(Item.Node, Rig.Root) && (!Item.Rig || DescendsFrom(Rig.Root, Item.Rig))) Item.Rig = Rig.Root;
		}
		if (Item.Rig)
		{
			auto& Rig = Rigs[NodePath(Item.Rig)]; Rig.Root = Item.Rig;
			if (!Item.Mesh->GetDeformerCount(FbxDeformer::eSkin))
				for (FbxNode* N = Item.Node; N && DescendsFrom(N, Rig.Root); N = N->GetParent()) Rig.Needed.insert(N);
		}
	}
}

void FImport::PreparePoses()
{
	if (!Scene->GetPoseCount()) Scene->GetFbxManager()->CreateMissingBindPoses(Scene);
	for (int I = 0; I < Scene->GetPoseCount(); ++I)
	{
		FbxPose* Pose = Scene->GetPose(I);
		if (Pose && Pose->IsBindPose()) Poses.push_back(Pose);
	}
	if (Poses.empty() && !Rigs.empty()) Warn("No usable BindPose; Cluster bind matrices and evaluated reference transforms are used");
}

bool FImport::PoseMatrix(FbxPose* Pose, FbxNode* Node, FbxAMatrix& Out)
{
	const int Index = Pose->Find(Node);
	if (Index < 0) return false;
	// BindPose는 노드별로 검증한다.
	FbxArray<FbxNode*> Ancestors, Deformers, DeformerAncestors, Wrong;
	bool Valid = Pose->IsValidBindPoseVerbose(Node, Ancestors, Deformers, DeformerAncestors, Wrong, 1.e-4);
	if (!Valid)
	{
		for (int A = 0; A < Ancestors.GetCount(); ++A)
			Pose->Add(Ancestors[A], Ancestors[A]->EvaluateGlobalTransform(FBXSDK_TIME_ZERO));
		Valid = Pose->IsValidBindPose(Node);
		if (!Valid)
		{
			FbxNode* Parent = Node->GetParent();
			for (; Parent; Parent = Parent->GetParent())
				if (auto* Attribute = Parent->GetNodeAttribute(); Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eNull) break;
			Valid = Parent && Pose->IsValidBindPose(Parent);
		}
	}
	if (!Valid) return false;
	const FbxMatrix& Matrix = Pose->GetMatrix(Index);
	for (int R = 0; R < 4; ++R) Out.SetRow(R, Matrix.GetRow(R));
	if (Pose->IsLocalMatrix(Index)) Out = Bind(Node->GetParent()) * Out;
	return true;
}

FbxAMatrix FImport::Bind(FbxNode* Node)
{
	if (!Node || Node == Scene->GetRootNode()) return FbxAMatrix();
	if (auto It = BindCache.find(Node); It != BindCache.end()) return It->second;
	FbxAMatrix Result;
	bool Found = false;
	for (FbxPose* Pose : Poses)
	{
		FbxAMatrix Candidate;
		if (!PoseMatrix(Pose, Node, Candidate)) continue;
		if (Found) { if (!Near(Result, Candidate)) Warn("Multiple BindPoses differ; first valid pose retained: " + NodePath(Node)); }
		else { Result = Candidate; Found = true; }
	}
	if (auto It = ClusterBones.find(Node); It != ClusterBones.end())
	{
		if (Found && !Near(Result, It->second)) Warn("BindPose and Cluster differ; BindPose retained: " + NodePath(Node));
		if (!Found) { Result = It->second; Found = true; }
	}
	if (!Found) Result = Node->EvaluateGlobalTransform(FBXSDK_TIME_ZERO);
	BindCache.emplace(Node, Result);
	return Result;
}

FbxAMatrix FImport::MeshBind(const FMeshItem& Item)
{
	for (FbxPose* Pose : Poses)
	{
		FbxAMatrix Result;
		if (PoseMatrix(Pose, Item.Node, Result))
		{
			if (auto It = ClusterMeshes.find(Item.Mesh); It != ClusterMeshes.end() && !Near(Result, It->second))
				Warn("Mesh BindPose and Cluster differ; BindPose retained: " + NodePath(Item.Node));
			return Result;
		}
	}
	if (auto It = ClusterMeshes.find(Item.Mesh); It != ClusterMeshes.end()) return It->second;
	return Bind(Item.Node);
}

// FBX SDK 검사를 통과하는 노드가 하나라도 있으면 유효하다. (UE FFbxScene::IsValidBindPose)
bool FImport::IsValidBindPose(FbxNode* RootJoint)
{
	int PoseCount = Scene->GetPoseCount();
	if (PoseCount == 0)
	{
		Scene->GetFbxManager()->CreateMissingBindPoses(Scene);
		PoseCount = Scene->GetPoseCount();
	}

	std::vector<FbxNode*> Nodes;
	std::function<void(FbxNode*)> Collect = [&](FbxNode* Node)
	{
		Nodes.push_back(Node);
		for (int C = 0; C < Node->GetChildCount(); ++C) Collect(Node->GetChild(C));
	};
	Collect(RootJoint);

	for (int PoseIndex = 0; PoseIndex < PoseCount; ++PoseIndex)
	{
		FbxPose* Pose = Scene->GetPose(PoseIndex);
		if (!Pose || !Pose->IsBindPose()) continue;
		for (FbxNode* Current : Nodes)
		{
			FbxArray<FbxNode*> MissingAncestors, MissingDeformers, MissingDeformersAncestors, WrongMatrices;
			if (Pose->IsValidBindPoseVerbose(Current, MissingAncestors, MissingDeformers, MissingDeformersAncestors, WrongMatrices, 1.e-4)) return true;

			for (int A = 0; A < MissingAncestors.GetCount(); ++A)
				Pose->Add(MissingAncestors[A], MissingAncestors[A]->EvaluateGlobalTransform(FBXSDK_TIME_ZERO));
			if (Pose->IsValidBindPose(Current)) return true;

			FbxNode* Parent = Current->GetParent();
			for (; Parent; Parent = Parent->GetParent())
				if (auto* Attribute = Parent->GetNodeAttribute(); Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eNull) break;
			if (Parent && Pose->IsValidBindPose(Parent)) return true;
		}
	}
	return false;
}

bool FImport::BuildRig(FRig& Rig)
{
	Rig.ParentBind = Bind(Rig.Root->GetParent());
	const FbxAMatrix FrameInverse = Options.bBakeSkeletalMeshes ? FbxAMatrix() : Rig.ParentBind.Inverse();
	if (!FiniteMatrix(EngineMatrix(Rig.ParentBind, UnitScale))) { Error = "Singular rig parent bind transform"; return false; }
	const auto BoneGlobal = [&](FbxNode* Node) -> FbxAMatrix
	{
		return FrameInverse * (Rig.bUseTimeZeroAsBindPose ? Node->EvaluateGlobalTransform(FBXSDK_TIME_ZERO) : Bind(Node));
	};
	FReferenceSkeleton BindReference;
	std::function<bool(FbxNode*, int)> Visit = [&](FbxNode* Node, int Parent)
	{
		if (!Rig.Needed.contains(Node)) return true;
		if (Rig.Bones.size() >= 65536) { Error = "More than 65536 bones in a rig"; return false; }
		const uint16 Index = static_cast<uint16>(Rig.Bones.size());
		Rig.Bones.push_back(Node); Rig.Indices[Node] = Index;
		const FbxAMatrix Global = BoneGlobal(Node);
		const FbxAMatrix Local = Parent < 0 ? Global : BoneGlobal(Rig.Bones[Parent]).Inverse() * Global;
		if (Rig.bUseTimeZeroAsBindPose)
		{
			const FbxAMatrix BindGlobal = FrameInverse * Bind(Node);
			const FbxAMatrix BindLocal = Parent < 0 ? BindGlobal : (FrameInverse * Bind(Rig.Bones[Parent])).Inverse() * BindGlobal;
			BindReference.RefBoneInfo.Add({ GetFbxObjectName(Node), Parent });
			BindReference.RefBonePose.Add(EnginePose(BindLocal, UnitScale));
		}
		const FMatrix Converted = EngineMatrix(Local, UnitScale);
		if (!FiniteMatrix(Converted)) { Error = "Invalid bind transform: " + NodePath(Node); return false; }
		const FTransform Transform = EnginePose(Local, UnitScale);
		const FMatrix Rebuilt = Transform.GetLocalMatrix();
		bool Difference = false;
		for (int R = 0; R < 4; ++R) for (int C = 0; C < 4; ++C)
			Difference |= std::abs(Rebuilt.M[R][C] - Converted.M[R][C]) > 1.e-3f * (1 + std::abs(Converted.M[R][C]));
		if (Difference) Warn("Bone transform is approximated as TRS: " + NodePath(Node));
		Rig.Reference.RefBoneInfo.Add({ GetFbxObjectName(Node), Parent });
		Rig.Reference.RefBonePose.Add(Transform);
		for (int C = 0; C < Node->GetChildCount(); ++C) if (!Visit(Node->GetChild(C), Index)) return false;
		return true;
	};
	if (!Visit(Rig.Root, -1)) return false;
	if (!Rig.Reference.Validate(Error))
	{
		// 네임스페이스를 지워 본 이름이 겹친 경우 옵션을 켜면 임포트할 수 있다
		if (!Options.bKeepFbxNamespace) Error += " (enable Keep FBX Namespace if bone names differ only by namespace)";
		return false;
	}
	if (Rig.bUseTimeZeroAsBindPose)
	{
		TArray<FMatrix> BindModel, TimeZeroModel;
		if (!BindReference.GetModelSpaceMatrices(BindModel, Error) || !Rig.Reference.GetModelSpaceMatrices(TimeZeroModel, Error)) return false;
		Rig.TimeZeroRebindMatrices.SetNum(TimeZeroModel.Num());
		for (int32 Bone = 0; Bone < TimeZeroModel.Num(); ++Bone) Rig.TimeZeroRebindMatrices[Bone] = BindModel[Bone].Inverse() * TimeZeroModel[Bone];
	}
	Rig.Key = MakeKey("SKEL", GetFbxObjectName(Rig.Root),"Skeleton:" + NodePath(Rig.Root));
	if (Rig.Key.empty()) return false;
	auto Asset = Data(EFbxAssetType::Skeleton, Rig.Key, "Skeleton:" + NodePath(Rig.Root));
	Asset.RefSkeleton = Rig.Reference;
	Output.Add(std::move(Asset));
	return true;
}

FSurfaceUV FImport::SurfaceUV(FbxSurfaceMaterial* Material, FbxMesh* Mesh, const FString& Context)
{
	FSurfaceUV Out;
	FbxStringList Names; Mesh->GetUVSetNames(Names);
	const FString First = Names.GetCount() ? Names[0].Buffer() : "";
	FbxFileTexture* Inputs[]{ FileTexture(Material, FbxSurfaceMaterial::sDiffuse), FileTexture(Material, FbxSurfaceMaterial::sNormalMap),
		SkippedOpacityTextures.contains(Material) ? nullptr : FileTexture(Material, FbxSurfaceMaterial::sTransparentColor) };
	FString Sets[3]; bool Missing = false;
	for (int I = 0; I < 3; ++I) if (Inputs[I])
	{
		const FString Requested = Inputs[I]->UVSet.Get().Buffer();
		for (int N = 0; N < Names.GetCount(); ++N) if (Requested == Names[N].Buffer()) { Sets[I] = Requested; break; }
		if (Sets[I].empty() && (Requested.empty() || Requested == "default")) Sets[I] = First;
		if (Sets[I].empty())
		{
			Missing = true;
			Warn("UV set not found: " + Context + ", texture=" + Inputs[I]->GetName() + ", requested=" + Requested);
		}
	}
	Out.Set = First;
	for (const auto& Set : Sets) if (!Set.empty()) { Out.Set = Set; break; }
	bool DifferentSets = false;
	for (int I = 0; I < 3; ++I) DifferentSets |= !Missing && Inputs[I] && Sets[I] != Out.Set;
	if (DifferentSets)
		Warn("Different texture UV sets: " + Context + ", diffuse=" + Sets[0] + ", normal=" + Sets[1] + ", opacity=" + Sets[2] + "; selected=" + Out.Set + ", UV transforms ignored");
	else if (Missing)
		Warn("UV fallback: " + Context + "; selected=" + Out.Set + ", UV transforms ignored");
	else
	{
		FUVTransform Selected; bool HasSelected = false, Invalid = false, Different = false;
		for (auto* Input : Inputs) if (Input)
		{
			const FUVTransform Transform = UVTransform(Input); Invalid |= !Transform.bValid;
			if (HasSelected) Different |= !Near(Selected, Transform);
			else { Selected = Transform; HasSelected = true; }
		}
		if (Invalid)
			Warn("Unsupported or invalid texture UV mapping: " + Context + "; UV transforms ignored");
		else if (Different)
			Warn("Different texture UV transforms: " + Context + "; selected=" + Out.Set + ", UV transforms ignored");
		else Out.Transform = Selected;
	}
	// UV 세트가 속한 레이어의 탄젠트를 사용한다. 다른 세트의 기저를 재사용하지 않는다.
	for (int L = 0; L < Mesh->GetLayerCount(); ++L)
	{
		auto* Layer = Mesh->GetLayer(L); const auto UVSets = Layer->GetUVSets();
		for (int U = 0; U < UVSets.GetCount(); ++U)
			if (UVSets[U] && Out.Set == UVSets[U]->GetName()) { Out.Tangents = Layer->GetTangents(); Out.Binormals = Layer->GetBinormals(); return Out; }
	}
	return Out;
}

FString FImport::Texture(FbxProperty Property)
{
	if (!Property.IsValid()) return {};
	if (Property.GetSrcObjectCount<FbxLayeredTexture>()) Warn("Layered textures are not converted: " + FString(Property.GetNameAsCStr()));
	if (!Property.GetSrcObjectCount<FbxFileTexture>()) return {};
	if (Property.GetSrcObjectCount<FbxFileTexture>() > 1) Warn("Only the first texture connection is used: " + FString(Property.GetNameAsCStr()));
	auto* T = Property.GetSrcObject<FbxFileTexture>(0);
	std::vector<fs::path> Candidates;
	if (T->GetFileName()[0]) Candidates.push_back(FbxPath::FromString(T->GetFileName()));
	if (T->GetRelativeFileName()[0]) Candidates.push_back(Source.parent_path() / FbxPath::FromString(T->GetRelativeFileName()));
	if (T->GetFileName()[0]) Candidates.push_back(Source.parent_path() / FbxPath::FromString(T->GetFileName()).filename());
	FString SourceTexture;
	for (auto Candidate : Candidates)
	{
		if (!Candidate.is_absolute()) Candidate = Source.parent_path() / Candidate;
		if (fs::is_regular_file(Candidate)) { SourceTexture = FbxPath::ToString(Candidate); break; }
	}
	if (SourceTexture.empty()) { Error = "Texture file not found: " + FString(T->GetFileName()); return {}; }
	// 입력 텍스처도 원본과 같이 외부에서 읽을 수 있다. 결과에는 픽셀/압축 블록만 남긴다.
	SourceTexture = FbxPath::ToString(fs::canonical(FbxPath::FromString(SourceTexture)));
	const FString TextureKey = SourceTexture;
	if (auto It = Textures.find(Lower(TextureKey)); It != Textures.end()) return It->second;
	FImageData Image = ImageLoader::LoadAuto(SourceTexture);
	if (!Image.IsValid()) { Error = "Texture format is not supported by the existing image loader: " + SourceTexture; return {}; }
	// 임시 내장 경로 대신 소스 FBX 안의 논리 이름으로 출력 이름을 고정한다.
	int TextureIndex = 0;
	for (; TextureIndex < Scene->GetTextureCount() && Scene->GetTexture(TextureIndex) != T; ++TextureIndex) {}
	FString Identity = "Texture:" + FString(T->GetName()) + "[" + std::to_string(TextureIndex) + "]/" + FbxPath::ToString(FbxPath::FromString(T->GetFileName()).filename());
	const FString Key = MakeKey("T", GetFbxObjectName(T), Identity);
	if (Key.empty()) return {};
	auto Asset = Data(EFbxAssetType::Texture2D, Key, Identity); Asset.Texture = std::move(Image);
	Output.Add(std::move(Asset)); Textures[Lower(TextureKey)] = Key;
	return Key;
}

FString FImport::OpacityTexture(FbxSurfaceMaterial* Material, const FString& DiffuseKey, const FString& OpacityKey,
	const FString& Identity, ESamplerState Sampler)
{
	const FImageData* Diffuse = nullptr; const FImageData* Opacity = nullptr;
	for (const auto& Asset : Output) if (Asset.Type == EFbxAssetType::Texture2D)
	{
		if (Asset.Key == DiffuseKey) Diffuse = &Asset.Texture;
		if (Asset.Key == OpacityKey) Opacity = &Asset.Texture;
	}
	if (!Opacity) { Error = "Missing opacity image: " + FString(Material->GetName()); return {}; }
	auto Supported = [](const FImageData& Image)
	{
		switch (Image.Format)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			return Image.BytesPerPixel == 4;
		case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_A8_UNORM: return Image.BytesPerPixel == 1;
		default: return false;
		}
	};
	if (!Supported(*Opacity) || (Diffuse && !Supported(*Diffuse)))
	{
		Warn("Opacity baking skipped: " + FString(Material->GetName()) +
			"; unsupported image format (diffuse=" + (Diffuse ? std::to_string(Diffuse->Format) : "none") +
			", opacity=" + std::to_string(Opacity->Format) + "); original Diffuse used and material stays opaque");
		return {};
	}
	auto Pixel = [](const FImageData& Image, uint32 X, uint32 Y) -> std::array<uint8, 4>
	{
		const uint8* P = Image.Pixels.GetData() + static_cast<size_t>(Y) * Image.GetRowPitch() + X * Image.BytesPerPixel;
		switch (Image.Format)
		{
		case DXGI_FORMAT_R8_UNORM: return { P[0], P[0], P[0], 255 };
		case DXGI_FORMAT_A8_UNORM: return { 255, 255, 255, P[0] };
		case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return { P[2], P[1], P[0], P[3] };
		case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return { P[2], P[1], P[0], 255 };
		default: return { P[0], P[1], P[2], P[3] };
		}
	};
	const auto AlphaSource = FileTexture(Material, FbxSurfaceMaterial::sTransparentColor)->GetAlphaSource();
	bool UseAlpha = AlphaSource == FbxTexture::eBlack;
	if (AlphaSource == FbxTexture::eNone)
		for (uint32 Y = 0; Y < Opacity->Height && !UseAlpha; ++Y)
			for (uint32 X = 0; X < Opacity->Width && !UseAlpha; ++X) UseAlpha = Pixel(*Opacity, X, Y)[3] != 255;
	auto Address = [Sampler](int Value, uint32 Size)
	{
		const int Count = static_cast<int>(Size);
		return static_cast<uint32>(Sampler == ESamplerState::LinearWrap ? ((Value % Count) + Count) % Count : std::clamp(Value, 0, Count - 1));
	};
	auto OpacityAt = [&](int X, int Y)
	{
		const auto P = Pixel(*Opacity, Address(X, Opacity->Width), Address(Y, Opacity->Height));
		// 알파/불투명도 마스크는 수치 데이터다. RGB 밝기를 사용할 때도 sRGB 디코딩하지 않는다.
		return UseAlpha ? static_cast<double>(P[3]) : .2126 * P[0] + .7152 * P[1] + .0722 * P[2];
	};
	FImageData Image;
	Image.Width = Diffuse ? Diffuse->Width : Opacity->Width; Image.Height = Diffuse ? Diffuse->Height : Opacity->Height;
	Image.Format = DXGI_FORMAT_R8G8B8A8_UNORM; Image.BytesPerPixel = 4;
	Image.Pixels.SetNum(static_cast<int32>(Image.Width * Image.Height * 4));
	for (uint32 Y = 0; Y < Image.Height; ++Y) for (uint32 X = 0; X < Image.Width; ++X)
	{
		const auto Color = Diffuse ? Pixel(*Diffuse, X, Y) : std::array<uint8, 4>{ 255, 255, 255, 255 };
		uint8* P = Image.Pixels.GetData() + (static_cast<size_t>(Y) * Image.Width + X) * 4;
		std::copy_n(Color.begin(), 3, P);
		const double SX = (X + .5) * Opacity->Width / Image.Width - .5, SY = (Y + .5) * Opacity->Height / Image.Height - .5;
		const int IX = static_cast<int>(std::floor(SX)), IY = static_cast<int>(std::floor(SY));
		const double FX = SX - IX, FY = SY - IY;
		const double Top = OpacityAt(IX, IY) * (1 - FX) + OpacityAt(IX + 1, IY) * FX;
		const double Bottom = OpacityAt(IX, IY + 1) * (1 - FX) + OpacityAt(IX + 1, IY + 1) * FX;
		P[3] = static_cast<uint8>(std::clamp(std::lround(Top * (1 - FY) + Bottom * FY), 0l, 255l));
	}
	const FString SourceObject = "OpacityTexture:" + Identity;
	const FString Key = MakeKey("T", GetFbxObjectName(Material) + "_Opacity", SourceObject);
	if (Key.empty()) return {};
	auto Asset = Data(EFbxAssetType::Texture2D, Key, SourceObject); Asset.Texture = std::move(Image);
	Output.Add(std::move(Asset));
	return Key;
}

FString FImport::Material(FbxSurfaceMaterial* M)
{
	if (!M) return "DefaultMaterial";
	if (auto It = Materials.find(M); It != Materials.end()) return It->second;
	int SourceIndex = 0;
	for (; SourceIndex < Scene->GetMaterialCount() && Scene->GetMaterial(SourceIndex) != M; ++SourceIndex) {}
	const FString Identity = "Material:" + FString(M->GetName()) + "[" + std::to_string(SourceIndex) + "]";
	const FString Key = MakeKey("M", GetFbxObjectName(M), Identity);
	if (Key.empty()) return {};
	auto Asset = Data(EFbxAssetType::Material, Key, Identity);
	float DiffuseFactor = 1.f;
	if (auto* Lambert = FbxCast<FbxSurfaceLambert>(M))
	{
		const FbxDouble3 Color = Lambert->Diffuse.Get();
		const double Factor = Lambert->DiffuseFactor.Get();
		DiffuseFactor = static_cast<float>(Factor);
		// Diffuse를 선형 색상으로 해석하는 임포트 기본값이다. 강도 계수도 선형 공간에서 곱한다.
		// 언리얼처럼 TransparencyFactor로 불투명도를 정하지 않는다. 내보낸 도구마다 값의 의미가 달라 불투명 메시가 투명해질 수 있다.
		Asset.Material.DiffuseOrFactor = FVector4(static_cast<float>(Color[0] * Factor), static_cast<float>(Color[1] * Factor),
			static_cast<float>(Color[2] * Factor), 1.f);
		Asset.Material.ShadingModel = ELightingModel::Lambert;
		Asset.Material.SpecularFactor = 0.f;
		const FbxDouble3 Emissive = Lambert->Emissive.Get();
		Asset.Material.EmissiveColor = FVector(static_cast<float>(Emissive[0]), static_cast<float>(Emissive[1]), static_cast<float>(Emissive[2]));
		Asset.Material.EmissiveFactor = static_cast<float>(Lambert->EmissiveFactor.Get());
	}
	else Warn("Unsupported material model; engine default used: " + FString(M->GetName()));
	if (auto* Phong = FbxCast<FbxSurfacePhong>(M))
	{
		Asset.Material.ShadingModel = ELightingModel::BlinnPhong;
		const FbxDouble3 Specular = Phong->Specular.Get();
		Asset.Material.SpecularColor = FVector(static_cast<float>(Specular[0]), static_cast<float>(Specular[1]), static_cast<float>(Specular[2]));
		Asset.Material.SpecularFactor = static_cast<float>(Phong->SpecularFactor.Get());
		// Phong의 반사 벡터 지수를 Blinn-Phong의 하프 벡터 지수로 근사한다.
		Asset.Material.Shininess = static_cast<float>(4. * std::max(0., Phong->Shininess.Get()));
	}
	for (const char* PropertyName : { FbxSurfaceMaterial::sSpecular, FbxSurfaceMaterial::sSpecularFactor,
		FbxSurfaceMaterial::sEmissive, FbxSurfaceMaterial::sEmissiveFactor })
	{
		const FbxProperty Property = M->FindProperty(PropertyName);
		if (Property.IsValid() && Property.GetSrcObjectCount<FbxTexture>() > 0)
			Warn("Unsupported material texture: " + FString(M->GetName()) + ", property=" + PropertyName + "; constant color/factor used");
	}
	auto* Diffuse = FileTexture(M, FbxSurfaceMaterial::sDiffuse); auto* Normal = FileTexture(M, FbxSurfaceMaterial::sNormalMap);
	auto* Opacity = FileTexture(M, FbxSurfaceMaterial::sTransparentColor);
	auto* Sampler = Diffuse ? Diffuse : (Normal ? Normal : Opacity);
	const FString SamplerName = Diffuse ? "Diffuse" : (Normal ? "Normal" : "Opacity");
	for (auto* Input : { Diffuse, Normal, Opacity }) if (Input)
	{
		if (Input != Sampler && (Sampler->GetWrapModeU() != Input->GetWrapModeU() || Sampler->GetWrapModeV() != Input->GetWrapModeV()))
			Warn("Different texture Wrap modes: " + FString(M->GetName()) + "; " + SamplerName + " Wrap selected");
		if (Input->GetWrapModeU() != Input->GetWrapModeV())
			Warn("Unsupported mixed U/V Wrap modes: " + FString(M->GetName()) + ", texture=" + Input->GetName() +
				(Input == Sampler ? "; LinearClamp selected" : "; " + SamplerName + " Wrap selected"));
	}
	if (Sampler && Sampler->GetWrapModeU() == Sampler->GetWrapModeV() && Sampler->GetWrapModeU() == FbxTexture::eRepeat)
		Asset.Material.SamplerState = ESamplerState::LinearWrap;
	Asset.Material.DiffuseTexture = Texture(M->FindProperty(FbxSurfaceMaterial::sDiffuse));
	if (!Error.empty()) return {};
	if (!Asset.Material.DiffuseTexture.empty())
	{
		// 색상 맵이 있으면 DiffuseFactor를 RGB 공통 배율로 사용한다. W는 불투명도다.
		Asset.Material.DiffuseOrFactor.X = Asset.Material.DiffuseOrFactor.Y = Asset.Material.DiffuseOrFactor.Z = DiffuseFactor;
	}
	Asset.Material.NormalTexture = Texture(M->FindProperty(FbxSurfaceMaterial::sNormalMap));
	if (!Error.empty()) return {};
	const FbxProperty Bump = M->FindProperty(FbxSurfaceMaterial::sBump);
	if (!Normal && Bump.IsValid() && Bump.GetSrcObjectCount<FbxTexture>() > 0)
		Warn("Unsupported Bump map: " + FString(M->GetName()) + "; normal input left empty");
	// 투명도 텍스처가 연결되어 있을 때만 AlphaBlend를 쓴다. (UE FbxMaterialImport.cpp:827)
	Asset.Material.BlendState = EBlendState::Opaque;
	const FString OpacityKey = Texture(M->FindProperty(FbxSurfaceMaterial::sTransparentColor));
	if (!Error.empty()) return {};
	if (!OpacityKey.empty())
	{
		const FString BakedTexture = OpacityTexture(M, Asset.Material.DiffuseTexture, OpacityKey, Identity, Asset.Material.SamplerState);
		if (!Error.empty()) return {};
		if (!BakedTexture.empty())
		{
			Asset.Material.DiffuseTexture = BakedTexture;
			Asset.Material.BlendState = EBlendState::AlphaBlend;
		}
		else SkippedOpacityTextures.insert(M);
	}
	Output.Add(std::move(Asset)); Materials[M] = Key;
	return Key;
}

bool FImport::AppendMesh(const FMeshItem& Item, FFbxAssetData& Asset, int LODIndex, std::map<uint32, std::vector<uint32>>& Sections)
{
	auto& LOD = Asset.LODs[LODIndex]; auto& Mesh = LOD.MeshData;
	FRig* Rig = Item.Rig ? &Rigs.at(NodePath(Item.Rig)) : nullptr;
	FbxAMatrix Full;
	if (Rig)
	{
		// Cluster.Transform은 이미 글로벌이다. Bake off는 루트 부모만 제거한다.
		Full = (Options.bBakeSkeletalMeshes ? FbxAMatrix() : Rig->ParentBind.Inverse()) * MeshBind(Item) * Geometry(Item.Node);
	}
	else if (Options.bBakeStaticMeshes) Full = Item.Node->EvaluateGlobalTransform(FBXSDK_TIME_ZERO) * Geometry(Item.Node);
	// Static Bake off는 자기 노드 변환도 적용하지 않는다. Combine이 배치를 대신 복원하지 않는다.
	const FMatrix Transform = EngineMatrix(Full, UnitScale, false);
	if (!FiniteMatrix(Transform)) { Error = "Invalid mesh transform: " + NodePath(Item.Node); return false; }
	const FMatrix NormalMatrix = Transform.Inverse().GetTransposed();
	const bool Reverse = Transform.Determinant() < 0;
	std::vector<uint32> MaterialMap;
	std::vector<FSurfaceUV> Surfaces;
	std::vector<int> SurfaceGroups;
	const int SlotCount = std::max(1, Item.Node->GetMaterialCount());
	for (int I = 0; I < SlotCount; ++I)
	{
		FbxSurfaceMaterial* SourceMaterial = I < Item.Node->GetMaterialCount() ? Item.Node->GetMaterial(I) : nullptr;
		const FString Key = Material(SourceMaterial);
		if (!Error.empty()) return false;
		const FSurfaceUV UV = SurfaceUV(SourceMaterial, Item.Mesh, NodePath(Item.Node) + (SourceMaterial ? SourceMaterial->GetName() : "Default"));
		int SurfaceGroup = I;
		for (int Previous = 0; Previous < I; ++Previous)
			if (UV.Set == Surfaces[Previous].Set && Near(UV.Transform, Surfaces[Previous].Transform)) { SurfaceGroup = SurfaceGroups[Previous]; break; }
		Surfaces.push_back(UV); SurfaceGroups.push_back(SurfaceGroup);
		int Index = -1;
		for (int M = 0; M < Asset.MaterialKeys.Num(); ++M) if (Asset.MaterialKeys[M] == Key) { Index = M; break; }
		if (Index < 0)
		{
			Index = Asset.MaterialKeys.Num(); Asset.MaterialKeys.Add(Key);
			// 모든 LOD는 애셋 단위 슬롯 인덱스를 사용한다.
			for (auto& Existing : Asset.LODs)
			{
				FStaticMaterialSlot Slot; Slot.Name = SourceMaterial ? GetFbxObjectName(SourceMaterial) : "Default";
				Existing.MeshData.MaterialSlots.Add(std::move(Slot));
			}
		}
		MaterialMap.push_back(static_cast<uint32>(Index));
	}
	const int CPCount = Item.Mesh->GetControlPointsCount();
	std::vector<std::map<uint16, float>> Influences(Rig ? CPCount : 0);
	if (Rig)
	{
		if (Item.Mesh->GetDeformerCount(FbxDeformer::eSkin))
		{
			auto* Skin = static_cast<FbxSkin*>(Item.Mesh->GetDeformer(0, FbxDeformer::eSkin));
			for (int C = 0; C < Skin->GetClusterCount(); ++C)
			{
				FbxCluster* Cluster = Skin->GetCluster(C);
				if (!Cluster || !Cluster->GetLink() || IsCompensation(Cluster)) continue;
				if (!Rig->Indices.contains(Cluster->GetLink())) { Error = "Bone is not in the mesh reference skeleton"; return false; }
				if (Cluster->GetLinkMode() == FbxCluster::eAdditive) Warn("Additive Link Mode is approximated using normalized Linear weights: " + NodePath(Item.Node));
				const uint16 Bone = Rig->Indices.at(Cluster->GetLink());
				for (int I = 0; I < Cluster->GetControlPointIndicesCount(); ++I)
				{
					const int CP = Cluster->GetControlPointIndices()[I]; const double W = Cluster->GetControlPointWeights()[I];
					if (CP < 0 || CP >= CPCount || !std::isfinite(W)) { Error = "Invalid control point influence"; return false; }
					if (W > 0) Influences[CP][Bone] = std::max(Influences[CP][Bone], static_cast<float>(W));
				}
			}
		}
		else
		{
			const uint16 Bone = Rig->Indices.at(Item.Node);
			for (auto& Vertex : Influences) Vertex[Bone] = 1;
		}
	}
	std::unordered_map<FCornerKey, uint32, FCornerHash> Cache;
	int32 PositionBase = 0;
	for (const auto& SourceVertex : Mesh.LODSourceVertices) PositionBase = std::max(PositionBase, SourceVertex.PositionIndex + 1);
	const int32 AttributeBase = Mesh.Vertices.Num();
	std::map<std::array<float, 2>, int32> UVIndices;
	std::map<std::array<float, 3>, int32> NormalIndices;
	FbxGeometryElementMaterial* MaterialsElement = Item.Mesh->GetElementMaterial();
	for (int P = 0; P < Item.Mesh->GetPolygonCount(); ++P)
	{
		if (Item.Mesh->GetPolygonSize(P) != 3) { Error = "FBX triangulation did not produce triangles"; return false; }
		int Slot = 0;
		if (MaterialsElement && MaterialsElement->GetIndexArray().GetCount())
		{
			const int Index = MaterialsElement->GetMappingMode() == FbxLayerElement::eByPolygon ? P : 0;
			if (Index < MaterialsElement->GetIndexArray().GetCount()) Slot = MaterialsElement->GetIndexArray().GetAt(Index);
		}
		if (Slot < 0 || Slot >= static_cast<int>(MaterialMap.size())) { Warn("Invalid material slot replaced with the first slot: " + NodePath(Item.Node)); Slot = 0; }
		const auto& Surface = Surfaces[Slot];
		FVertex Vertices[3]; int CPs[3]; bool MappedUVs[3]{};
		for (int C = 0; C < 3; ++C)
		{
			const int CP = Item.Mesh->GetPolygonVertex(P, C); CPs[C] = CP;
			if (CP < 0 || CP >= CPCount) { Error = "Invalid polygon control point index"; return false; }
			auto& V = Vertices[C];
			V.Tangent = FVector4(0, 0, 0, 0);
			V.Position = Reflect(Full.MultT(Item.Mesh->GetControlPointAt(CP))) * static_cast<float>(UnitScale);
			FbxVector4 N;
			if (Item.Mesh->GetPolygonVertexNormal(P, C, N)) V.Normal = NormalMatrix.TransformVector(FVector(static_cast<float>(N[0]), static_cast<float>(N[1]), static_cast<float>(N[2]))).GetSafeNormal();
			if (!Surface.Set.empty())
			{
				FbxVector2 UV; bool Unmapped = false;
				if (Item.Mesh->GetPolygonVertexUV(P, C, Surface.Set.c_str(), UV, Unmapped) && !Unmapped)
				{
					UV = Surface.Transform.Apply(UV); MappedUVs[C] = true;
					V.UV = FVector2(static_cast<float>(UV[0]), 1.f - static_cast<float>(UV[1]));
				}
				else Warn("Unmapped UV corner: " + NodePath(Item.Node) + ", selected=" + Surface.Set + "; zero UV used");
			}
			if (auto* Colors = Item.Mesh->GetElementVertexColor())
			{
				int Index = -1;
				switch (Colors->GetMappingMode())
				{
				case FbxLayerElement::eByControlPoint: Index = CP; break;
				case FbxLayerElement::eByPolygonVertex: Index = Item.Mesh->GetPolygonVertexIndex(P) + C; break;
				case FbxLayerElement::eByPolygon: Index = P; break;
				case FbxLayerElement::eAllSame: Index = 0; break;
				default: break;
				}
				if (Colors->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
					Index = Index >= 0 && Index < Colors->GetIndexArray().GetCount() ? Colors->GetIndexArray().GetAt(Index) : -1;
				if (Index >= 0 && Index < Colors->GetDirectArray().GetCount())
				{
					const FbxColor Color = Colors->GetDirectArray().GetAt(Index);
					V.Color = FVector4(static_cast<float>(Color.mRed), static_cast<float>(Color.mGreen), static_cast<float>(Color.mBlue), static_cast<float>(Color.mAlpha));
				}
			}
		}
		FVector FaceNormal = (Vertices[1].Position - Vertices[0].Position).Cross(Vertices[2].Position - Vertices[0].Position).GetSafeNormal();
		if (Reverse) FaceNormal = FaceNormal * -1.f;
		uint32 Corners[3];
		for (int C = 0; C < 3; ++C)
		{
			if (Vertices[C].Normal.SizeSquared() < 1.e-12f) Vertices[C].Normal = FaceNormal;
			if (MappedUVs[C] && !ImportTangent(Surface, Item.Mesh, P, C, Transform, Vertices[C]) && Surface.Tangents)
				Warn("Invalid source tangent basis: " + NodePath(Item.Node) + ", UV set=" + Surface.Set + "; affected tangents recalculated");
			const auto& V = Vertices[C];
			if (!std::isfinite(V.Position.X) || !std::isfinite(V.Position.Y) || !std::isfinite(V.Position.Z) ||
				!std::isfinite(V.Normal.X) || !std::isfinite(V.Normal.Y) || !std::isfinite(V.Normal.Z) ||
				!std::isfinite(V.UV.X) || !std::isfinite(V.UV.Y)) { Error = "Invalid FBX vertex attribute"; return false; }
			const FCornerKey Key{ CPs[C], SurfaceGroups[Slot], Vertices[C] };
			if (auto It = Cache.find(Key); It != Cache.end()) { Corners[C] = It->second; continue; }
			Corners[C] = Mesh.Vertices.Num(); Mesh.Vertices.Add(Vertices[C]); Cache.emplace(Key, Corners[C]);
			// 기존 스태틱 LOD 생성기에 필요한 원본 위치/이음매 식별자다.
			const auto UV = UVIndices.try_emplace({ V.UV.X, V.UV.Y }, AttributeBase + static_cast<int32>(UVIndices.size())).first->second;
			const auto Normal = NormalIndices.try_emplace({ V.Normal.X, V.Normal.Y, V.Normal.Z }, AttributeBase + static_cast<int32>(NormalIndices.size())).first->second;
			Mesh.LODSourceVertices.Add({ PositionBase + CPs[C], UV, Normal });
			if (Rig)
			{
				std::vector<std::pair<uint16, float>> Weights(Influences[CPs[C]].begin(), Influences[CPs[C]].end());
				std::sort(Weights.begin(), Weights.end(), [](auto A, auto B) { return A.second != B.second ? A.second > B.second : A.first < B.first; });
				if (Weights.size() > 4) Warn("Weights reduced to four influences: " + NodePath(Item.Node));
				Weights.resize(std::min<size_t>(Weights.size(), 4));
				std::array<uint16, 4> Bones{}; std::array<float, 4> Values{};
				float Total = 0; for (auto W : Weights) Total += W.second;
				if (Total <= 0) { Values[0] = 1; Warn("Unweighted vertices attached to root: " + NodePath(Item.Node)); }
				else for (size_t I = 0; I < Weights.size(); ++I) { Bones[I] = Weights[I].first; Values[I] = Weights[I].second / Total; }
				LOD.SkinWeights.InfluenceBones.Add(Bones); LOD.SkinWeights.InfluenceWeights.Add(Values);
			}
		}
		auto& Indices = Sections[MaterialMap[Slot]];
		Indices.push_back(Corners[0]); Indices.push_back(Corners[Reverse ? 2 : 1]); Indices.push_back(Corners[Reverse ? 1 : 2]);
	}
	return true;
}

void FImport::LODThresholds(const FOutputGroup& Group, FFbxAssetData& Asset)
{
	const auto& Box = Asset.LODs[0].MeshData.AABB;
	const float Radius = (Box.Max - Box.Min).Size() * .5f;
	std::set<FbxNode*> Seen;
	bool Set[3]{};
	for (auto* Item : Group.Items)
	{
		if (!Item->LODGroupNode || !Seen.insert(Item->LODGroupNode).second) continue;
		FbxLODGroup* LOD = nullptr;
		for (int A = 0; A < Item->LODGroupNode->GetNodeAttributeCount(); ++A)
			if (Item->LODGroupNode->GetNodeAttributeByIndex(A)->GetAttributeType() == FbxNodeAttribute::eLODGroup)
				LOD = static_cast<FbxLODGroup*>(Item->LODGroupNode->GetNodeAttributeByIndex(A));
		if (!LOD) continue;
		if (LOD->MinMaxDistance.Get()) Warn("LOD Min/MaxDistance visibility is not imported: " + NodePath(Item->LODGroupNode));
		for (int I = 0; I < std::min(3, LOD->GetNumThresholds()); ++I)
		{
			float Threshold = 0;
			if (LOD->ThresholdsUsedAsPercentage.Get())
			{
				FbxDouble Percentage = 0;
				if (LOD->GetThreshold(I, Percentage)) Threshold = static_cast<float>(Percentage / 100.);
			}
			else
			{
				FbxDistance Distance;
				if (LOD->GetThreshold(I, Distance))
				{
					// FbxDistance는 자체 단위를 갖는다. 장면 단위를 중복 곱하지 않는다.
					const double D = Distance.valueAs(FbxSystemUnit::m);
					if (D > 0) Threshold = static_cast<float>(Radius * 1.7320508075688772 / D);
				}
			}
			if (Threshold > 0 && std::isfinite(Threshold))
			{
				Asset.ScreenThresholds[I] = Set[I] ? std::min(Asset.ScreenThresholds[I], Threshold) : Threshold; Set[I] = true;
			}
		}
	}
	for (int I = 1; I < 3; ++I)
		if (Asset.ScreenThresholds[I] >= Asset.ScreenThresholds[I - 1]) Asset.ScreenThresholds[I] = Asset.ScreenThresholds[I - 1] * .5f;
}

// 정점을 가중치대로 0초 자세로 옮긴다. 런타임 스키닝과 같은 계산이다. (UE InterchangeSkeletalMeshFactory)
void FImport::RebindToTimeZero(const FRig& Rig, FFbxMeshLODData& LOD)
{
	auto& Vertices = LOD.MeshData.Vertices;
	for (int32 V = 0; V < Vertices.Num(); ++V)
	{
		FVertex& Vertex = Vertices[V];
		const FVector BindPosition = Vertex.Position, BindNormal = Vertex.Normal;
		const FVector BindTangent(Vertex.Tangent.X, Vertex.Tangent.Y, Vertex.Tangent.Z);
		FVector Position = FVector::ZeroVector, Normal = FVector::ZeroVector, Tangent = FVector::ZeroVector;
		for (int K = 0; K < 4; ++K)
		{
			const float Weight = LOD.SkinWeights.InfluenceWeights[V][K];
			if (Weight <= 0.f) continue;
			const FMatrix& Matrix = Rig.TimeZeroRebindMatrices[LOD.SkinWeights.InfluenceBones[V][K]];
			Position += Matrix.TransformPosition(BindPosition) * Weight;
			Normal += Matrix.TransformVector(BindNormal) * Weight;
			Tangent += Matrix.TransformVector(BindTangent) * Weight;
		}
		Vertex.Position = Position;
		Vertex.Normal = Normal.GetSafeNormal();
		const FVector NewTangent = Tangent.GetSafeNormal();
		Vertex.Tangent = FVector4(NewTangent.X, NewTangent.Y, NewTangent.Z, Vertex.Tangent.W);
	}
}

bool FImport::CookMesh(const FOutputGroup& Group)
{
	const bool Skinned = Group.Root != nullptr;
	const auto Type = Skinned ? EFbxAssetType::SkeletalMesh : EFbxAssetType::StaticMesh;
	const FString Key = MakeKey(Skinned ? "SK" : "SM", Group.Label, Group.Identity);
	if (Key.empty()) return false;
	auto Asset = Data(Type, Key, Group.Identity);
	int MaxLOD = 0; for (auto* Item : Group.Items) MaxLOD = std::max(MaxLOD, Item->LOD);
	Asset.LODs.SetNum(std::min(4, MaxLOD + 1));
	if (Skinned) { const auto& Rig = Rigs.at(NodePath(Group.Root)); Asset.RefSkeleton = Rig.Reference; Asset.SkeletonKey = Rig.Key; }
	for (int L = 0; L < Asset.LODs.Num(); ++L)
	{
		std::map<uint32, std::vector<uint32>> Sections;
		// 그룹 밖 조각은 모든 단계에 포함한다. 그룹의 누락 LOD는 마지막 기하를 반복하지 않는다.
		for (auto* Item : Group.Items) if (Item->LOD < 0 || Item->LOD == L)
			if (!AppendMesh(*Item, Asset, L, Sections)) return false;
		auto& Mesh = Asset.LODs[L].MeshData;
		for (const auto& [MaterialIndex, Indices] : Sections)
		{
			Mesh.Sections.Add({ static_cast<uint32>(Mesh.Indices.Num()), static_cast<uint32>(Indices.size()), MaterialIndex });
			for (auto Index : Indices) Mesh.Indices.Add(Index);
		}
		if (!Mesh.Vertices.IsEmpty())
		{
			std::vector<std::pair<int32, FVector4>> SourceTangents;
			for (int32 I = 0; I < Mesh.Vertices.Num(); ++I)
				if (Mesh.Vertices[I].Tangent.W != 0) SourceTangents.emplace_back(I, Mesh.Vertices[I].Tangent);
			if (SourceTangents.size() != static_cast<size_t>(Mesh.Vertices.Num()))
			{
				Mesh.BuildTangentSpace();
				for (const auto& [Index, Tangent] : SourceTangents) Mesh.Vertices[Index].Tangent = Tangent;
			}
			if (Skinned)
				if (const auto& Rig = Rigs.at(NodePath(Group.Root)); Rig.bUseTimeZeroAsBindPose) RebindToTimeZero(Rig, Asset.LODs[L]);
			FVector Min = Mesh.Vertices[0].Position, Max = Min;
			for (const auto& V : Mesh.Vertices)
			{
				Min.X = std::min(Min.X, V.Position.X); Min.Y = std::min(Min.Y, V.Position.Y); Min.Z = std::min(Min.Z, V.Position.Z);
				Max.X = std::max(Max.X, V.Position.X); Max.Y = std::max(Max.Y, V.Position.Y); Max.Z = std::max(Max.Z, V.Position.Z);
			}
			Mesh.AABB = FBox{ Min, Max };
		}
	}
	if (Asset.LODs[0].MeshData.Vertices.IsEmpty()) { Error = "No visible LOD0 geometry: " + Group.Label; return false; }
	LODThresholds(Group, Asset);
	if (!Asset.Validate(Error)) return false;
	Output.Add(std::move(Asset));
	return true;
}

// 옵션에 맞는 클립 구간을 정하고 프레임 경계에 맞춘다. 맞출 수 없으면 false다. (UE UInterchangeGenericAnimationPipeline)
bool FImport::GetAnimationRange(FbxNode* RootNode, FbxAnimStack* Stack, double SampleRate, double& OutStart, double& OutStop)
{
	const FString StackName = Stack->GetName();
	if (Options.AnimationRange == EInterchangeAnimationRange::Timeline)
	{
		const FbxTimeSpan Span = Stack->GetLocalTimeSpan();
		OutStart = Span.GetStart().GetSecondDouble();
		OutStop = Span.GetStop().GetSecondDouble();
	}
	else if (Options.AnimationRange == EInterchangeAnimationRange::SetRange)
	{
		OutStart = Options.FrameImportRangeMin / SampleRate;
		OutStop = Options.FrameImportRangeMax / SampleRate;
	}
	else
	{
		FbxTimeSpan Span(FBXSDK_TIME_INFINITE, FBXSDK_TIME_MINUS_INFINITE);
		RootNode->GetAnimationInterval(Span, Stack);
		OutStart = Span.GetStart().GetSecondDouble();
		OutStop = Span.GetStop().GetSecondDouble();
	}

	const auto SubFrame = [](double Frame) { return Frame - std::floor(Frame); };
	const auto IsFrameBorder = [](double Sub) { return Sub <= KINDA_SMALL_NUMBER || 1.0 - Sub <= KINDA_SMALL_NUMBER; };
	const double SequenceLength = std::max(OutStop - OutStart, MINIMUM_ANIMATION_LENGTH);
	if (!IsFrameBorder(SubFrame(SequenceLength * SampleRate)))
	{
		if (Options.FrameAlignment == EInterchangeFrameAlignment::None)
		{
			Warn("Animation length " + std::to_string(SequenceLength) + " is not compatible with frame rate " + std::to_string(SampleRate) +
				"; enable Frame Alignment to import: " + StackName);
			return false;
		}
		const auto Align = [&](double Time)
		{
			const double Frame = Time * SampleRate;
			if (IsFrameBorder(SubFrame(Frame))) return Time;
			switch (Options.FrameAlignment)
			{
			case EInterchangeFrameAlignment::SnapToFloor: return std::floor(Frame) / SampleRate;
			case EInterchangeFrameAlignment::SnapToClosest: return std::round(Frame) / SampleRate;
			case EInterchangeFrameAlignment::SnapToCeiling: return std::ceil(Frame) / SampleRate;
			default: return Time;
			}
		};
		OutStart = Align(OutStart);
		OutStop = Align(OutStop);
		Warn("Animation length has been adjusted to align with frame borders: " + StackName);
	}
	if (OutStop <= OutStart)
	{
		Warn("Animation range is empty: " + StackName);
		return false;
	}
	return true;
}

// 움직이는 본이 있는 클립마다 일정한 간격으로 본의 값을 물어 키를 만든다. (UE FFbxScene::AddAnimation, FFbxAnimation)
void FImport::CookAnimations(const FRig& Rig)
{
	const double SampleRate = GetBoneAnimationSampleRate(FrameRate);
	FbxTime TimeStep = 0;
	TimeStep.SetSecondDouble(1.0 / SampleRate);

	const int32 BoneCount = static_cast<int32>(Rig.Bones.size());
	// 부모 기준 값 계산은 BuildRig의 기본 자세와 같은 규칙을 따른다.
	const FbxAMatrix FrameInverse = Options.bBakeSkeletalMeshes ? FbxAMatrix() : Rig.ParentBind.Inverse();
	std::vector<FbxAMatrix> Globals(BoneCount);

	for (int32 StackIndex = 0; StackIndex < Scene->GetSrcObjectCount<FbxAnimStack>(); ++StackIndex)
	{
		FbxAnimStack* Stack = Scene->GetSrcObject<FbxAnimStack>(StackIndex);
		const FString StackName = Stack->GetName();

		// 곡선이 있는 본과 그 자식 본만 트랙을 만든다. Rig.Bones는 부모가 항상 자식보다 앞에 있다.
		std::vector<bool> ValidTracks(BoneCount, false);
		bool bHasAnimatedBone = false;
		for (int32 Bone = 0; Bone < BoneCount; ++Bone)
		{
			const int32 Parent = Rig.Reference.RefBoneInfo[Bone].ParentIndex;
			ValidTracks[Bone] = IsNodeAnimated(Rig.Bones[Bone], Stack) || (Parent >= 0 && ValidTracks[Parent]);
			bHasAnimatedBone = bHasAnimatedBone || ValidTracks[Bone];
		}
		if (!bHasAnimatedBone)
		{
			Warn("Animation stack has no animated bones: " + StackName);
			continue;
		}

		double StartSeconds = 0.0, StopSeconds = 0.0;
		if (!GetAnimationRange(Rig.Root, Stack, SampleRate, StartSeconds, StopSeconds)) continue;
		Scene->SetCurrentAnimationStack(Stack);

		const double SequenceLength = std::max(StopSeconds - StartSeconds, 1.0 / SampleRate);
		const int32 NumFrame = static_cast<int32>(std::lround(SequenceLength * SampleRate));
		const int32 BakeKeyCount = NumFrame + 1;

		FFbxAssetData Asset = Data(EFbxAssetType::AnimSequence, {}, "Animation:" + NodePath(Rig.Root) + StackName);
		Asset.SkeletonKey = Rig.Key;
		Asset.Animation.Name = GetFbxObjectName(Stack);
		Asset.Animation.FrameRate = static_cast<float>(SampleRate);
		Asset.Animation.NumberOfFrames = NumFrame;
		Asset.Animation.NumberOfKeys = BakeKeyCount;

		std::vector<FBoneAnimationTrack> Tracks(BoneCount);
		for (int32 Bone = 0; Bone < BoneCount; ++Bone)
		{
			Tracks[Bone].Name = Rig.Reference.RefBoneInfo[Bone].Name;
			Tracks[Bone].BoneTreeIndex = Bone;
			Tracks[Bone].InternalTrackData.PosKeys.Reserve(BakeKeyCount);
			Tracks[Bone].InternalTrackData.RotKeys.Reserve(BakeKeyCount);
			Tracks[Bone].InternalTrackData.ScaleKeys.Reserve(BakeKeyCount);
		}

		FbxTime CurrentTime;
		CurrentTime.SetSecondDouble(StartSeconds);
		for (int32 FrameIndex = 0; FrameIndex < BakeKeyCount; ++FrameIndex, CurrentTime += TimeStep)
		{
			for (int32 Bone = 0; Bone < BoneCount; ++Bone)
			{
				Globals[Bone] = FrameInverse * Rig.Bones[Bone]->EvaluateGlobalTransform(CurrentTime);
				if (!ValidTracks[Bone]) continue;

				const int32 Parent = Rig.Reference.RefBoneInfo[Bone].ParentIndex;
				const FbxAMatrix Local = Parent < 0 ? Globals[Bone] : Globals[Parent].Inverse() * Globals[Bone];
				const FTransform Pose = EnginePose(Local, UnitScale);
				if (Pose.ContainsNaN())
				{
					ValidTracks[Bone] = false;
					Warn("Invalid animation transform: " + StackName + " " + NodePath(Rig.Bones[Bone]));
					continue;
				}
				FRawAnimSequenceTrack& Track = Tracks[Bone].InternalTrackData;
				Track.PosKeys.Add(Pose.Location);
				Track.RotKeys.Add(Pose.Rotation);
				Track.ScaleKeys.Add(Pose.Scale);
			}
		}

		for (int32 Bone = 0; Bone < BoneCount; ++Bone)
		{
			if (!ValidTracks[Bone]) continue;
			CompressRawAnimSequenceTrack(Tracks[Bone].InternalTrackData);
			Asset.Animation.Tracks.Add(std::move(Tracks[Bone]));
		}

		HTR_LOG(Info, "FBX Animation: {} (Skeleton {}) FrameRate={} Frames={} Keys={} Tracks={}/{}",
			StackName, Rig.Key, Asset.Animation.FrameRate, NumFrame, BakeKeyCount, Asset.Animation.Tracks.Num(), BoneCount);
		Animations.push_back(std::move(Asset));
	}
}

bool FImport::Run()
{
	Gather(Scene->GetRootNode(), true);
	if (Meshes.empty()) { Error = "No visible supported mesh geometry in FBX"; return false; }
	FindRigs(); if (!Error.empty()) return false;
	PreparePoses();
	// 메시가 연결되는 리그만 출력한다. 포즈/컨트롤 본은 실제 본 테이블에 유지된다.
	std::set<FbxNode*> Used;
	for (const auto& Item : Meshes) if (Item.Rig) Used.insert(Item.Rig);
	for (auto& [Path, Rig] : Rigs) if (Used.contains(Rig.Root))
	{
		// 바인드 포즈가 유효하지 않으면 옵션과 상관없이 0초 자세를 쓴다. (UE InterchangeGenericCommonSkeletalMeshesAndAnimationsProperties)
		Rig.bUseTimeZeroAsBindPose = Options.bUseT0AsRefPose;
		if (!Rig.bUseTimeZeroAsBindPose && !IsValidBindPose(Rig.Root))
		{
			Rig.bUseTimeZeroAsBindPose = true;
			Warn("Invalid BindPose; mesh is rebound to the time 0 pose: " + NodePath(Rig.Root));
		}
		if (!BuildRig(Rig)) return false;
	}
	std::map<FString, FOutputGroup> Groups;
	for (auto& Item : Meshes)
	{
		const bool Combine = Item.Rig ? Options.bCombineSkeletalMeshes : Options.bCombineStaticMeshes;
		const FString Identity = Item.Rig ? "Skeletal:" + NodePath(Item.Rig) + (Combine ? "Combined" : Item.UnitKey)
			: "Static:" + (Combine ? FString("Combined") : Item.UnitKey);
		auto& Group = Groups[Identity]; Group.Identity = Identity; Group.Root = Item.Rig;
		Group.Label = Combine ? (Item.Rig ? GetFbxObjectName(Item.Rig) : FString("Combined")) : Item.Label;
		Group.Items.push_back(&Item);
	}
	for (const auto& [Key, Group] : Groups) if (!CookMesh(Group)) return false;
	if (Options.bImportAnimations)
		for (const auto& [Path, Rig] : Rigs) if (Used.contains(Rig.Root)) CookAnimations(Rig);
	// 클립이 하나면 "파일이름_Anim", 여러 개면 클립 이름을 쓴다. (UE InterchangeGenericAssetsPipeline)
	for (auto& Asset : Animations)
	{
		Asset.Key = MakeKey("ANIM", Animations.size() == 1 ? FString("Anim") : Asset.Animation.Name, Asset.SourceObject);
		if (Asset.Key.empty()) return false;
		Output.Add(std::move(Asset));
	}
	std::set<FString> ReferencedTextures;
	for (const auto& Asset : Output) if (Asset.Type == EFbxAssetType::Material)
	{
		ReferencedTextures.insert(Asset.Material.DiffuseTexture); ReferencedTextures.insert(Asset.Material.NormalTexture);
	}
	for (int32 I = Output.Num() - 1; I >= 0; --I)
		if (Output[I].Type == EFbxAssetType::Texture2D && !ReferencedTextures.contains(Output[I].Key)) Output.RemoveAt(I);
	for (const auto& Asset : Output) if (!Asset.Validate(Error)) return false;
	return true;
}
}
#endif

bool FFbxImporter::IsAvailable() { return WITH_FBX_SDK != 0; }

bool FFbxImporter::Cook(const FString& SourcePath, const FFbxImportOptions& Options, const FString& ExtractionDirectory,
	TArray<FFbxAssetData>& Out, TArray<FString>& Warnings, FString& Error, const FString& OutputDirectory)
{
	Out.Empty(); Warnings.Empty(); Error.clear();
#if WITH_FBX_SDK
	try
	{
		FString SourceKey;
		fs::path Source, Destination, Extraction;
		if (!FbxPath::GetSourcePath(SourcePath, Source, SourceKey, Error) ||
			!FbxPath::GetOutputDirectory(OutputDirectory.empty() ? FbxPath::DefaultOutputDirectory(SourcePath) : OutputDirectory, Destination, Error) ||
			!FbxPath::GetOutputDirectory(ExtractionDirectory, Extraction, Error)) return false;
		struct FManagerDelete { void operator()(FbxManager* P) const { if (P) P->Destroy(); } };
		std::unique_ptr<FbxManager, FManagerDelete> Manager(FbxManager::Create());
		if (!Manager) { Error = "Cannot create FBX SDK manager"; return false; }
		Manager->SetIOSettings(FbxIOSettings::Create(Manager.get(), IOSROOT));
		FbxImporter* Parser = FbxImporter::Create(Manager.get(), "");
		const FString CanonicalSource = FbxPath::ToString(Source);
		const FString CanonicalExtraction = FbxPath::ToString(Extraction);
		if (!Parser->Initialize(CanonicalSource.c_str(), -1, Manager->GetIOSettings())) { Error = Parser->GetStatus().GetErrorString(); return false; }
		Parser->SetEmbeddingExtractionFolder(CanonicalExtraction.c_str());
		FbxScene* Scene = FbxScene::Create(Manager.get(), "Import");
		if (!Parser->Import(Scene)) { Error = Parser->GetStatus().GetErrorString(); return false; }
		// 후속 애니메이션 임포트: AnimStack/Layer/Curve를 별도 애셋으로 샘플링한다.
		// 이 단계는 바인드 기하/골격만 구성하며 런타임 포즈 평가를 수행하지 않는다.
		bool Blender = false;
		if (auto* Info = Scene->GetSceneInfo()) Blender = Lower(Info->Original_ApplicationName.Get().Buffer()).find("blender") != FString::npos;
		const double UnitScale = Scene->GetGlobalSettings().GetSystemUnit().GetScaleFactor() / 100.;
		if (!std::isfinite(UnitScale) || UnitScale <= 0) { Error = "Invalid FBX scene unit"; return false; }
		const double FbxFrameRate = FbxTime::GetFrameRate(Scene->GetGlobalSettings().GetTimeMode());
		for (int32 StackIndex = 0; StackIndex < Scene->GetSrcObjectCount<FbxAnimStack>(); ++StackIndex)
		{
			FbxAnimStack* Stack = Scene->GetSrcObject<FbxAnimStack>(StackIndex);
			if (Stack->GetMemberCount<FbxAnimLayer>() > 1)
				MergeAllLayerAnimation(Scene, Stack, static_cast<int32>(std::lround(GetBoneAnimationSampleRate(FbxFrameRate))));
		}
		// UE의 Z-up/-ParityOdd 기준을 사용한다. Deep 변환으로 Bake off의 로컬 기하도 같은 축이 된다.
		FbxAxisSystem Target(FbxAxisSystem::eZAxis, static_cast<FbxAxisSystem::EFrontVector>(-FbxAxisSystem::eParityOdd), FbxAxisSystem::eRightHanded);
		Target.DeepConvertScene(Scene);
		FbxGeometryConverter Converter(Manager.get());
		if (!Converter.Triangulate(Scene, true)) { Error = "FBX SDK triangulation failed"; return false; }
		FImport Import(Scene, Options, SourceKey, Source, Destination, Out, Warnings, Error);
		Import.UnitScale = UnitScale; Import.bBlender = Blender;
		Import.FrameRate = FbxFrameRate;
		const bool Result = Import.Run();
		if (!Result) Out.Empty();
		return Result;
	}
	catch (const std::exception& E) { Error = E.what(); Out.Empty(); return false; }
#else
	Error = "FBX SDK is not configured. Set FBX_SDK_ROOT and regenerate the solution; existing .fbxbin assets can still be loaded.";
	return false;
#endif
}
