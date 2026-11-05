#pragma once

#include "Core/EngineString.h"

class USkeletalMesh;

// 스키닝 테스트용 메시를 코드로 만든다. (FBX 없이 검증하기 위함)
// FBX 로더와 같은 형태(USkeletalMesh의 MeshData·SkinWeights·RefSkeleton)로 채운다.
struct FSkeletalMeshGenerator
{
	static constexpr int32 TestRadialSegments = 16; // 둘레 칸 수
	static constexpr int32 TestHeightSegments = 24; // 높이 줄 수

	// +Z 방향으로 뻗은 팔 모양 튜브. 본 BoneCount개가 길이를 균등하게 나눠 가진다.
	// BlendRatio: 관절에서 두 본을 섞는 구간 폭 (본 길이 대비 비율)
	static bool BuildTestArm(USkeletalMesh& OutMesh, FString& OutError,
		float Radius = 0.25f, float Length = 3.0f, int32 BoneCount = 3, float BlendRatio = 0.4f);

	// 본 정보, 역바인드 검증, 높이별 가중치를 로그로 찍는다.
	static void LogSkinData(const USkeletalMesh& Mesh);
};
