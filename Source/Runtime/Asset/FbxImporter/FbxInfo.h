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
};

enum EFBXAnimationLengthImportType : int
{
	FBXALIT_ExportedTime, // FBX에 기록된 클립 길이
	FBXALIT_AnimatedKey, // 키가 실제로 있는 구간
	FBXALIT_SetRange, // AnimationRangeStart, AnimationRangeEnd로 지정한 프레임 구간
};

struct FFbxImportOptions
{
	bool bBakeStaticMeshes = true;
	bool bCombineStaticMeshes = false;
	bool bBakeSkeletalMeshes = true;
	bool bCombineSkeletalMeshes = true;
	bool bImportAnimations = true;
	bool bResample = true; // 꺼지면 초당 30키로 고정한다.
	int32 ResampleRate = 0; // 0이면 곡선에서 자동으로 구한다.
	EFBXAnimationLengthImportType AnimationLengthImportType = FBXALIT_ExportedTime;
	int32 AnimationRangeStart = 0;
	int32 AnimationRangeEnd = 0;
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
	FString SkeletonKey;
	float FrameRate = 30.0f; // 초당 키 수
	int32 NumberOfFrames = 0; // 키 사이 구간 수
	int32 NumberOfKeys = 0;
	TArray<FBoneAnimationTrack> Tracks;
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

	bool Validate(FString& Error) const;
};

struct FFbxImportResult
{
	bool bSuccess = false;
	FString Error;
	TArray<FString> Warnings;
	TArray<FString> AssetKeys;
};
