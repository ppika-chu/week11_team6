#include "EnginePCH.h"
#include "SkeletalMeshGenerator.h"
#include "Render/SkeletalMesh.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace
{
	struct FTestWeight
	{
		std::array<uint16, 4> Bones{};
		std::array<float, 4> Weights{};
	};

	// 높이 Z의 스킨 가중치. 관절 근처(BlendWidth 안)에서만 두 본을 섞는다.
	FTestWeight ComputeWeight(float Z, float BoneLength, int32 BoneCount, float BlendWidth)
	{
		FTestWeight Weight{};

		// 기본: 이 높이를 담당하는 본 하나에 100%
		const int32 Bone = std::clamp(static_cast<int32>(Z / BoneLength), 0, BoneCount - 1);
		Weight.Bones[0] = static_cast<uint16>(Bone);
		Weight.Weights[0] = 1.0f;

		const float Half = BlendWidth * 0.5f;
		for (int32 Joint = 1; Joint < BoneCount; ++Joint)
		{
			const float JointZ = Joint * BoneLength;
			if (Z > JointZ - Half && Z < JointZ + Half)
			{
				float T = (Z - (JointZ - Half)) / BlendWidth; // 0 → 1
				T = T * T * (3.0f - 2.0f * T);                // smoothstep: 경계에서 부드럽게

				Weight = FTestWeight{};
				Weight.Bones[0] = static_cast<uint16>(Joint - 1);
				Weight.Weights[0] = 1.0f - T;
				Weight.Bones[1] = static_cast<uint16>(Joint);
				Weight.Weights[1] = T;
				break;
			}
		}
		return Weight;
	}
}

bool FSkeletalMeshGenerator::BuildTestArm(USkeletalMesh& OutMesh, FString& OutError,
	float Radius, float Length, int32 BoneCount, float BlendRatio)
{
	FStaticMeshData& Data = OutMesh.MeshData;
	FSkinWeightData& Skin = OutMesh.SkinWeights;
	FReferenceSkeleton& Ref = OutMesh.RefSkeleton;

	Data = FStaticMeshData{};
	Skin = FSkinWeightData{};
	Ref = FReferenceSkeleton{};

	const int32 Radial = TestRadialSegments;
	const int32 Rings = TestHeightSegments;
	const float BoneLength = Length / BoneCount;
	const float BlendWidth = BoneLength * BlendRatio;
	const FVector4 Color(1.0f, 1.0f, 1.0f, 1.0f);

	// 1) 본: 0 → 1 → 2 체인. 각 본은 부모 기준 +Z로 BoneLength 떨어져 있다. (FBX 임포터처럼 FTransform으로 저장)
	static const char* DefaultNames[] = { "Shoulder", "Elbow", "Wrist" };
	for (int32 i = 0; i < BoneCount; ++i)
	{
		const FString Name = (BoneCount == 3) ? FString(DefaultNames[i]) : std::format("Bone{}", i);
		const FVector Offset = (i == 0) ? FVector(0.0f, 0.0f, 0.0f) : FVector(0.0f, 0.0f, BoneLength);
		Ref.RefBoneInfo.Add({ Name, i - 1 });
		Ref.RefBonePose.Add(FTransform(FRotator(0.0f, 0.0f, 0.0f), Offset));
	}

	// 정점과 가중치를 항상 같이 추가해서 개수가 어긋나지 않게 한다.
	auto AddVertex = [&](const FVector& Pos, const FVector& Normal, const FVector2& UV) -> uint32
	{
		const uint32 Index = static_cast<uint32>(Data.Vertices.Num());
		Data.Vertices.Add({ Pos, Normal, FVector4(1.0f, 0.0f, 0.0f, 1.0f), UV, Color });

		const FTestWeight Weight = ComputeWeight(Pos.Z, BoneLength, BoneCount, BlendWidth);
		Skin.InfluenceBones.Add(Weight.Bones);
		Skin.InfluenceWeights.Add(Weight.Weights);
		return Index;
	};

	// 2) 옆면: 높이 Rings+1줄 × 둘레 Radial+1칸 (UV 이음새 때문에 마지막 칸 중복)
	const int32 Stride = Radial + 1;
	for (int32 h = 0; h <= Rings; ++h)
	{
		const float Z = Length * h / Rings;
		for (int32 r = 0; r <= Radial; ++r)
		{
			const float Angle = 2.0f * PI * r / Radial;
			const float C = cosf(Angle);
			const float S = sinf(Angle);
			AddVertex(FVector(Radius * C, Radius * S, Z), FVector(C, S, 0.0f),
				FVector2(1.0f - static_cast<float>(r) / Radial, 1.0f - Z / Length));
		}
	}
	for (int32 h = 0; h < Rings; ++h)
	{
		for (int32 r = 0; r < Radial; ++r)
		{
			const uint32 BottomA = h * Stride + r;
			const uint32 BottomB = h * Stride + r + 1;
			const uint32 TopA = (h + 1) * Stride + r;
			const uint32 TopB = (h + 1) * Stride + r + 1;

			// GeometryGenerator::CreateCylinder와 같은 감기 순서
			Data.Indices.Add(BottomB);
			Data.Indices.Add(TopB);
			Data.Indices.Add(TopA);
			Data.Indices.Add(BottomB);
			Data.Indices.Add(TopA);
			Data.Indices.Add(BottomA);
		}
	}

	// 3) 뚜껑: 노멀이 옆면과 달라서 정점을 따로 만든다
	auto AddCap = [&](float Z, bool bTop)
	{
		const FVector Normal(0.0f, 0.0f, bTop ? 1.0f : -1.0f);
		const uint32 Center = AddVertex(FVector(0.0f, 0.0f, Z), Normal, FVector2(0.5f, 0.5f));
		const uint32 RingStart = static_cast<uint32>(Data.Vertices.Num());
		for (int32 r = 0; r <= Radial; ++r)
		{
			const float Angle = 2.0f * PI * r / Radial;
			const float C = cosf(Angle);
			const float S = sinf(Angle);
			AddVertex(FVector(Radius * C, Radius * S, Z), Normal, FVector2(0.5f + 0.5f * C, 0.5f + 0.5f * S));
		}
		for (int32 r = 0; r < Radial; ++r)
		{
			Data.Indices.Add(Center);
			Data.Indices.Add(bTop ? RingStart + r : RingStart + r + 1);
			Data.Indices.Add(bTop ? RingStart + r + 1 : RingStart + r);
		}
	};
	AddCap(0.0f, false);
	AddCap(Length, true);

	Data.AABB = FBox{ FVector(-Radius, -Radius, 0.0f), FVector(Radius, Radius, Length) };

	// 슬롯·섹션 하나. Materials는 비워 두면 컴포넌트가 DefaultMaterial을 쓴다.
	FStaticMaterialSlot Slot;
	Slot.Name = "Default";
	Data.MaterialSlots.Add(Slot);

	FStaticMeshSection Section;
	Section.StartIndex = 0;
	Section.IndexCount = static_cast<uint32>(Data.Indices.Num());
	Section.MaterialSlotIndex = 0;
	Data.Sections.Add(Section);

	// FBX 로더와 같은 후처리. 정점 순서는 바뀌지 않으므로 영향 배열과의 대응이 유지된다.
	Data.BuildTangentSpace();
	Data.OptimizeTriangleOrderForVertexCache();
	if (!Data.Validate(OutError) || !Skin.Validate(Data.Vertices.Num(), Ref.GetNum(), OutError))
	{
		return false;
	}
	Data.BuildTriangleBVH(); // 피킹·데칼용 (바인드 포즈 기준)

	return OutMesh.CalculateInvRefMatrices(OutError);
}

void FSkeletalMeshGenerator::LogSkinData(const USkeletalMesh& Mesh)
{
	const FReferenceSkeleton& Ref = Mesh.RefSkeleton;
	const FStaticMeshData& Data = Mesh.MeshData;
	const FSkinWeightData& Skin = Mesh.SkinWeights;

	HTR_LOG(Info, "[SkelTest] Bones={} Vertices={} SkinWeights={} Indices={}",
		Ref.GetNum(), Data.Vertices.Num(), Skin.InfluenceWeights.Num(), Data.Indices.Num());

	// 본 정보 + "바인드 × 역바인드 = 단위행렬" 검증
	TArray<FMatrix> Bind;
	FString Error;
	if (!Ref.GetModelSpaceMatrices(Bind, Error))
	{
		HTR_LOG(Error, "[SkelTest] {}", Error);
		return;
	}
	for (int32 i = 0; i < Ref.GetNum(); ++i)
	{
		float MaxError = -1.0f; // -1이면 역바인드 행렬이 아직 없음
		if (i < Mesh.RefBasesInvMatrix.Num())
		{
			const FMatrix Check = Bind[i] * Mesh.RefBasesInvMatrix[i];
			MaxError = 0.0f;
			for (int32 r = 0; r < 4; ++r)
			{
				for (int32 c = 0; c < 4; ++c)
				{
					MaxError = std::max(MaxError, std::fabs(Check.M[r][c] - (r == c ? 1.0f : 0.0f)));
				}
			}
		}

		HTR_LOG(Info, "[SkelTest] Bone{} '{}' parent={} bindPos=({:.2f}, {:.2f}, {:.2f}) identityError={:.6f}",
			i, Ref.RefBoneInfo[i].Name, Ref.RefBoneInfo[i].ParentIndex,
			Bind[i].M[3][0], Bind[i].M[3][1], Bind[i].M[3][2], MaxError);
	}

	// 옆면 각 줄의 첫 정점(r = 0)만 찍는다
	const int32 Stride = TestRadialSegments + 1;
	const int32 SideCount = (TestHeightSegments + 1) * Stride;
	for (int32 v = 0; v < SideCount && v < Data.Vertices.Num() && v < Skin.InfluenceWeights.Num(); v += Stride)
	{
		const auto& B = Skin.InfluenceBones[v];
		const auto& W = Skin.InfluenceWeights[v];
		HTR_LOG(Info, "[SkelTest] v{:4} z={:.3f} | B{}:{:.2f}  B{}:{:.2f} | sum={:.3f}",
			v, Data.Vertices[v].Position.Z, B[0], W[0], B[1], W[1], W[0] + W[1] + W[2] + W[3]);
	}
}
