#pragma once

#include "Animation/AnimDataModel.h"
#include "Animation/ReferenceSkeleton.h"
#include "Render/SkeletalMesh.h"
#include "Render/ImageLoader.h"
#include "Render/RenderEnums.h"
#include "Render/RenderStates.h"
#include <array>

enum class EFbxAssetType : uint32
{
	StaticMesh = 1,
	SkeletalMesh,
	Skeleton,
	Material,
	Texture2D,
	AnimSequence,
};

enum class EInterchangeAnimationRange : int32
{
	Timeline, // FBX에 기록된 클립 구간
	Animated, // 키가 실제로 있는 구간
	SetRange, // FrameImportRangeMin, FrameImportRangeMax로 지정한 프레임 구간
};

enum class EInterchangeFrameAlignment : int32
{
	None, // 프레임 경계에 맞지 않는 클립은 임포트하지 않는다.
	SnapToFloor,
	SnapToClosest,
	SnapToCeiling,
};

struct FFbxImportOptions
{
	bool bBakeStaticMeshes = true;
	bool bCombineStaticMeshes = false;
	bool bBakeSkeletalMeshes = true;
	bool bCombineSkeletalMeshes = true;
	bool bUseT0AsRefPose = false; // 켜면 바인드 포즈가 유효해도 0초 자세를 기본 자세로 쓴다.
	bool bImportAnimations = true;
	EInterchangeAnimationRange AnimationRange = EInterchangeAnimationRange::Timeline;
	int32 FrameImportRangeMin = 0;
	int32 FrameImportRangeMax = 0;
	EInterchangeFrameAlignment FrameAlignment = EInterchangeFrameAlignment::None;
	bool bKeepFbxNamespace = false; // 켜면 이름의 ':'를 '_'로 바꿔 남기고, 끄면 마지막 ':'까지 지운다. (UE UInterchangeFbxTranslatorSettings)
	// 비어 있으면 새 SKEL_을 만들고, 스켈레톤 에셋 키를 주면 그 스켈레톤을 쓴다. 없는 본은 합친다 (UE 임포트 옵션 Skeleton)
	// 임포트 때만 쓰고 .fbxbin에는 저장하지 않는다
	FString Skeleton;
};

struct FFbxMaterialData
{
	FVector4 DiffuseOrFactor{ 1, 1, 1, 1 }; // 선형 RGB: Diffuse 맵이 없으면 Diffuse × DiffuseFactor, 있으면 DiffuseFactor를 각 채널에 저장한다. W는 불투명도.
	float Shininess = 32.f;
	FVector SpecularColor = FVector(1, 1, 1); // 선형 반사광 색상
	float SpecularFactor = 1.f; // 반사광 강도 계수
	FVector EmissiveColor = FVector(0, 0, 0); // 선형 자체 발광 색상
	float EmissiveFactor = 1.f; // 자체 발광 강도 계수
	ELightingModel ShadingModel = ELightingModel::BlinnPhong;
	ESamplerState SamplerState = ESamplerState::LinearClamp;
	EBlendState BlendState = EBlendState::Opaque;
	FString DiffuseTexture;
	FString NormalTexture;
};

struct FFbxMeshLODData
{
	FStaticMeshData MeshData;
	FSkinWeightData SkinWeights;
};

// FBX 클립 하나를 본별 키로 바꾼 결과다. UAnimDataModel을 만들기 전 Cook 단계에서 쓴다.
struct FFbxAnimationData
{
	FString Name;
	float FrameRate = 30.0f; // 초당 키 수
	int32 NumberOfFrames = 0; // 키 사이 구간 수
	int32 NumberOfKeys = 0;
	TArray<FBoneAnimationTrack> Tracks;
	// Root Motion 설정 (UAnimSequence와 같은 이름). FBX에는 없어 임포트는 기본값이고, 에디터에서 바꿔 저장한다
	bool bEnableRootMotion = false;
	int32 RootMotionRootLock = 0; // ERootMotionRootLock::Type
	bool bForceRootLock = false;
};

// Cook 결과다. SDK 포인터/GPU 리소스는 포함하지 않는다. 한 항목이 한 .fbxbin이다.
struct FFbxAssetData
{
	EFbxAssetType Type = EFbxAssetType::StaticMesh;
	FString Key;
	FString SourcePath; // 출처 식별용: Assets 상대 경로 또는 외부 원본의 절대 경로. 로딩 의존성이 아니다.
	FString SourceObject;
	FFbxImportOptions Options;
	TArray<FFbxMeshLODData> LODs;
	TArray<FString> MaterialKeys;
	std::array<float, 3> ScreenThresholds{ .30f, .10f, .03f };
	FString SkeletonKey;
	FReferenceSkeleton RefSkeleton;
	FFbxMaterialData Material;
	FImageData Texture;
	FFbxAnimationData Animation;

	bool Validate(FString& Error) const;
};

struct FFbxImportResult
{
	bool bSuccess = false;
	FString Error;
	TArray<FString> Warnings;
	TArray<FString> AssetKeys;
};
