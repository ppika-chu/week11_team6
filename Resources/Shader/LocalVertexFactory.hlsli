#include "VertexFactoryCommon.hlsli"

struct FVertexFactoryIntermediates
{
    float3 LocalPosition;
    float3x3 TangentToLocal;
    float TangentToWorldSign;
};

float3x3 CalcTangentToLocal(FVertexFactoryInput Input, inout float TangentSign)
{
    const float3 TangentX = Input.Tangent.xyz;
    const float3 TangentZ = Input.Normal;
    TangentSign = Input.Tangent.w;
    const float3 TangentY = cross(TangentZ, TangentX) * TangentSign;
    return float3x3(TangentX, TangentY, TangentZ);
}

FVertexFactoryIntermediates GetVertexFactoryIntermediates(FVertexFactoryInput Input)
{
    FVertexFactoryIntermediates Intermediates;
    float TangentSign = 1.0f;
    Intermediates.LocalPosition = Input.Position;
    Intermediates.TangentToLocal = CalcTangentToLocal(Input, TangentSign);
    Intermediates.TangentToWorldSign = TangentSign * GetWorldDeterminantSign();
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
