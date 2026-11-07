#ifndef VERTEX_FACTORY_COMMON_HLSLI
#define VERTEX_FACTORY_COMMON_HLSLI

#pragma pack_matrix(row_major)

// Renderer.h의 FPerObjectConstants와 동일 레이아웃
cbuffer PerObjectConstants : register(b2)
{
    matrix World;
    matrix WorldInverseTranspose;
    uint ReceiveShadow;
    uint3 ObjectPadding;
};

// 모든 VF 파일이 구현하는 인터페이스:
//   FVertexFactoryIntermediates GetVertexFactoryIntermediates(FVertexFactoryInput Input);
//   float3 VertexFactoryGetLocalPosition(FVertexFactoryInput Input, FVertexFactoryIntermediates Intermediates);
//   float4 VertexFactoryGetWorldPosition(FVertexFactoryInput Input, FVertexFactoryIntermediates Intermediates);
//   float3x3 VertexFactoryGetTangentToLocal(FVertexFactoryInput Input, FVertexFactoryIntermediates Intermediates);
//   float3 VertexFactoryGetWorldNormal(FVertexFactoryInput Input, FVertexFactoryIntermediates Intermediates);

struct FVertexFactoryInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float4 Tangent : TANGENT; // w = 바이탄젠트 부호
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
    uint VertexID : SV_VertexID;
};

float GetWorldDeterminantSign()
{
    return determinant((float3x3) World) < 0.0f ? -1.0f : 1.0f;
}

#endif
