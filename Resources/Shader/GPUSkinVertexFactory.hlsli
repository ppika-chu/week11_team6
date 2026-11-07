#include "VertexFactoryCommon.hlsli"

struct FBoneMatrixElement
{
    float4x4 M;
};
StructuredBuffer<FBoneMatrixElement> BoneMatrices : register(t20);

struct FSkinWeight
{
    uint2 PackedBones;
    float4 Weights;
};
StructuredBuffer<FSkinWeight> SkinWeights : register(t21);

struct FVertexFactoryIntermediates
{
    float4x4 BlendMatrix;
    float3 LocalPosition;
    float3x3 TangentToLocal;
    float TangentToWorldSign;
};

float4x4 CalcBoneMatrix(FVertexFactoryInput Input)
{
    const FSkinWeight W = SkinWeights[Input.VertexID];
    const uint4 B = uint4(W.PackedBones.x & 0xFFFF, W.PackedBones.x >> 16, W.PackedBones.y & 0xFFFF, W.PackedBones.y >> 16);
    return BoneMatrices[B.x].M * W.Weights.x +
           BoneMatrices[B.y].M * W.Weights.y +
           BoneMatrices[B.z].M * W.Weights.z +
           BoneMatrices[B.w].M * W.Weights.w;
}

float3 SkinPosition(FVertexFactoryInput Input, FVertexFactoryIntermediates Intermediates)
{
    return mul(float4(Input.Position, 1.0f), Intermediates.BlendMatrix).xyz;
}

float3x3 SkinTangents(FVertexFactoryInput Input, FVertexFactoryIntermediates Intermediates)
{
    const float3 TangentX = mul(float4(Input.Tangent.xyz, 0.0f), Intermediates.BlendMatrix).xyz;
    const float3 TangentZ = mul(float4(Input.Normal, 0.0f), Intermediates.BlendMatrix).xyz;
    const float3 TangentY = cross(TangentZ, TangentX) * Input.Tangent.w;
    return float3x3(TangentX, TangentY, TangentZ);
}

FVertexFactoryIntermediates GetVertexFactoryIntermediates(FVertexFactoryInput Input)
{
    FVertexFactoryIntermediates Intermediates;
    Intermediates.BlendMatrix = CalcBoneMatrix(Input);
    Intermediates.LocalPosition = SkinPosition(Input, Intermediates);
    Intermediates.TangentToLocal = SkinTangents(Input, Intermediates);
    Intermediates.TangentToWorldSign = Input.Tangent.w * GetWorldDeterminantSign();
    return Intermediates;
}

float3 VertexFactoryGetLocalPosition(FVertexFactoryInput Input, FVertexFactoryIntermediates Intermediates)
{
    return Intermediates.LocalPosition;
}

float4 VertexFactoryGetWorldPosition(FVertexFactoryInput Input, FVertexFactoryIntermediates Intermediates)
{
    return mul(float4(Intermediates.LocalPosition, 1.0f), World);
}

float3x3 VertexFactoryGetTangentToLocal(FVertexFactoryInput Input, FVertexFactoryIntermediates Intermediates)
{
    return Intermediates.TangentToLocal;
}

float3 VertexFactoryGetWorldNormal(FVertexFactoryInput Input, FVertexFactoryIntermediates Intermediates)
{
    return mul(float4(Intermediates.TangentToLocal[2], 0.0f), WorldInverseTranspose).xyz;
}
