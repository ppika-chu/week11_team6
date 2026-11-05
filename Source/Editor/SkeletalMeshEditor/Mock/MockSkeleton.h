#pragma once

// FBX Importer가 완성되기 전 Viewer / CPU 스키닝 디버깅용 하드코딩 데이터.
// X 앞, Y 오른쪽, Z 위, Row-vector (v * M, 자식 = Local * Parent).
// 단위는 cm다. 엔진은 1.0 = 1m라 그대로 쓰면 100배 크게 보인다. 뷰어는 이제 USkeletalMesh만 연다.
// 모든 뼈의 Bind 회전은 0 (뼈 축 = Component 축). 실제 리그는 회전이 들어 있으니 이 데이터로 회전 처리까지 검증되지는 않는다.

#include <cmath>

#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Math/Matrix.h"
#include "Math/Transform.h"

struct FMockBone
{
	FString Name;
	int32 ParentIndex;      // 루트는 -1. 부모는 항상 자식보다 앞 인덱스
	FTransform LocalBind;   // 부모 기준 Bind Pose
};

struct FMockSkinnedVertex
{
	FVector Position;       // Component 공간, Bind Pose 기준
	FVector Normal;
	int32 BoneIndices[4];
	float BoneWeights[4];   // 합 1
};

struct FMockSkinnedMesh
{
	TArray<FMockBone> Bones;
	TArray<FMockSkinnedVertex> Vertices;
	TArray<uint32> Indices;
};

namespace MockSkeleton
{
	inline FTransform MakeLocal(float X, float Y, float Z)
	{
		return FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(X, Y, Z));
	}

	// 인간형 21개 뼈, T-Pose. 키 약 170cm, 왼쪽 = -Y.
	inline TArray<FMockBone> MakeHumanoid()
	{
		return {
			{ "Root",        -1, MakeLocal(0.0f,   0.0f,   0.0f) },  // 0  발밑
			{ "Pelvis",       0, MakeLocal(0.0f,   0.0f,  95.0f) },  // 1  Component (0,0,95)
			{ "Spine",        1, MakeLocal(0.0f,   0.0f,  10.0f) },  // 2  (0,0,105)
			{ "Spine1",       2, MakeLocal(0.0f,   0.0f,  15.0f) },  // 3  (0,0,120)
			{ "Spine2",       3, MakeLocal(0.0f,   0.0f,  15.0f) },  // 4  (0,0,135)
			{ "Neck",         4, MakeLocal(0.0f,   0.0f,  15.0f) },  // 5  (0,0,150)
			{ "Head",         5, MakeLocal(0.0f,   0.0f,  10.0f) },  // 6  (0,0,160)
			{ "Clavicle_L",   4, MakeLocal(0.0f,  -5.0f,  10.0f) },  // 7  (0,-5,145)
			{ "UpperArm_L",   7, MakeLocal(0.0f, -12.0f,   0.0f) },  // 8  (0,-17,145)
			{ "LowerArm_L",   8, MakeLocal(0.0f, -28.0f,   0.0f) },  // 9  (0,-45,145)
			{ "Hand_L",       9, MakeLocal(0.0f, -25.0f,   0.0f) },  // 10 (0,-70,145)
			{ "Clavicle_R",   4, MakeLocal(0.0f,   5.0f,  10.0f) },  // 11 (0,5,145)
			{ "UpperArm_R",  11, MakeLocal(0.0f,  12.0f,   0.0f) },  // 12 (0,17,145)
			{ "LowerArm_R",  12, MakeLocal(0.0f,  28.0f,   0.0f) },  // 13 (0,45,145)
			{ "Hand_R",      13, MakeLocal(0.0f,  25.0f,   0.0f) },  // 14 (0,70,145)
			{ "Thigh_L",      1, MakeLocal(0.0f, -10.0f,  -5.0f) },  // 15 (0,-10,90)
			{ "Calf_L",      15, MakeLocal(0.0f,   0.0f, -42.0f) },  // 16 (0,-10,48)
			{ "Foot_L",      16, MakeLocal(0.0f,   0.0f, -40.0f) },  // 17 (0,-10,8)
			{ "Thigh_R",      1, MakeLocal(0.0f,  10.0f,  -5.0f) },  // 18 (0,10,90)
			{ "Calf_R",      18, MakeLocal(0.0f,   0.0f, -42.0f) },  // 19 (0,10,48)
			{ "Foot_R",      19, MakeLocal(0.0f,   0.0f, -40.0f) },  // 20 (0,10,8)
		};
	}

	// Local 변환을 루트부터 누적해 Component 공간 행렬을 구한다. 현재 포즈에도 그대로 쓸 수 있다.
	inline TArray<FMatrix> ComputeComponentTransforms(const TArray<FMockBone>& Bones, const TArray<FTransform>& LocalTransforms)
	{
		TArray<FMatrix> Result;
		Result.Reserve(Bones.Num());
		for (int32 i = 0; i < Bones.Num(); ++i)
		{
			const FMatrix Local = LocalTransforms[i].GetLocalMatrix();
			const int32 Parent = Bones[i].ParentIndex;
			Result.Add(Parent < 0 ? Local : Local * Result[Parent]);
		}
		return Result;
	}

	inline TArray<FTransform> GetBindLocals(const TArray<FMockBone>& Bones)
	{
		TArray<FTransform> Locals;
		Locals.Reserve(Bones.Num());
		for (int32 i = 0; i < Bones.Num(); ++i)
			Locals.Add(Bones[i].LocalBind);
		return Locals;
	}

	inline TArray<FMatrix> ComputeInverseBind(const TArray<FMockBone>& Bones)
	{
		TArray<FMatrix> ComponentBind = ComputeComponentTransforms(Bones, GetBindLocals(Bones));
		TArray<FMatrix> Result;
		Result.Reserve(ComponentBind.Num());
		for (int32 i = 0; i < ComponentBind.Num(); ++i)
			Result.Add(ComponentBind[i].Inverse());
		return Result;
	}

	// Row-vector이므로 SkinningMatrix = InverseBind * CurrentComponent (먼저 Bone 공간으로, 그다음 현재 포즈로)
	inline FVector SkinPosition(const FMockSkinnedVertex& Vertex, const TArray<FMatrix>& InverseBind, const TArray<FMatrix>& CurrentComponent)
	{
		FVector Result(0.0f, 0.0f, 0.0f);
		for (int32 i = 0; i < 4; ++i)
		{
			const int32 BoneIndex = Vertex.BoneIndices[i];
			const float Weight = Vertex.BoneWeights[i];
			if (Weight <= 0.0f || BoneIndex < 0 || BoneIndex >= InverseBind.Num())
				continue;

			const FMatrix Skin = InverseBind[BoneIndex] * CurrentComponent[BoneIndex];
			Result += Skin.TransformPosition(Vertex.Position) * Weight;
		}
		return Result;
	}

	// 높이 100cm 원통 + 뼈 3개 (Bone0 바닥, Bone1 중간 관절 z=50, Bone2_End 끝점, 가중치 없음).
	// z 40~60 구간에서 Bone0 -> Bone1로 가중치가 선형으로 넘어간다. Bone1을 돌리면 가운데가 부드럽게 휘어야 정상.
	inline FMockSkinnedMesh MakeCylinder(float Radius = 10.0f, float Height = 100.0f, int32 Segments = 16, int32 Rings = 20)
	{
		FMockSkinnedMesh Mesh;
		Mesh.Bones = {
			{ "Bone0",      -1, MakeLocal(0.0f, 0.0f, 0.0f) },
			{ "Bone1",       0, MakeLocal(0.0f, 0.0f, Height * 0.5f) },
			{ "Bone2_End",   1, MakeLocal(0.0f, 0.0f, Height * 0.5f) },
		};

		const float BlendStart = Height * 0.4f;
		const float BlendEnd = Height * 0.6f;
		const float TwoPi = 6.28318530718f;

		for (int32 Ring = 0; Ring <= Rings; ++Ring)
		{
			const float Z = Height * static_cast<float>(Ring) / static_cast<float>(Rings);
			float W1 = (Z - BlendStart) / (BlendEnd - BlendStart);
			W1 = W1 < 0.0f ? 0.0f : (W1 > 1.0f ? 1.0f : W1);

			for (int32 Seg = 0; Seg < Segments; ++Seg)
			{
				const float Angle = TwoPi * static_cast<float>(Seg) / static_cast<float>(Segments);
				const float C = std::cos(Angle);
				const float S = std::sin(Angle);

				FMockSkinnedVertex V{};
				V.Position = FVector(C * Radius, S * Radius, Z);
				V.Normal = FVector(C, S, 0.0f);
				V.BoneIndices[0] = 0; V.BoneWeights[0] = 1.0f - W1;
				V.BoneIndices[1] = 1; V.BoneWeights[1] = W1;
				V.BoneIndices[2] = 0; V.BoneWeights[2] = 0.0f;
				V.BoneIndices[3] = 0; V.BoneWeights[3] = 0.0f;
				Mesh.Vertices.Add(V);
			}
		}

		// 옆면만 (뚜껑 없음). 감기 방향은 엔진 컬링 설정에 맞지 않으면 두 인덱스를 바꿀 것.
		for (int32 Ring = 0; Ring < Rings; ++Ring)
		{
			for (int32 Seg = 0; Seg < Segments; ++Seg)
			{
				const uint32 A = Ring * Segments + Seg;
				const uint32 B = Ring * Segments + (Seg + 1) % Segments;
				const uint32 C = A + Segments;
				const uint32 D = B + Segments;
				Mesh.Indices.Add(A); Mesh.Indices.Add(C); Mesh.Indices.Add(B);
				Mesh.Indices.Add(B); Mesh.Indices.Add(C); Mesh.Indices.Add(D);
			}
		}
		return Mesh;
	}
}

/* 사용 예

	TArray<FMockBone> Bones = MockSkeleton::MakeHumanoid();
	TArray<FMatrix> InverseBind = MockSkeleton::ComputeInverseBind(Bones);

	// 현재 포즈 = Bind 복사 후 팔꿈치만 굽힘 (Viewer 기즈모가 하는 일)
	TArray<FTransform> Pose = MockSkeleton::GetBindLocals(Bones);
	Pose[9].Rotation = FRotator(0.0f, 0.0f, 90.0f);

	TArray<FMatrix> Current = MockSkeleton::ComputeComponentTransforms(Bones, Pose);
	// 검증: Pose를 바꾸지 않았다면 InverseBind[i] * Current[i]는 모두 단위행렬이어야 한다.
*/
