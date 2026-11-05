#pragma once

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

struct FFbxImportOptions
{
	bool bBakeStaticMeshes = true;
	bool bCombineStaticMeshes = false;
	bool bBakeSkeletalMeshes = true;
	bool bCombineSkeletalMeshes = true;
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
