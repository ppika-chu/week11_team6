#pragma pack_matrix(row_major)

#include "VertexFactory.hlsli"

// View별 행렬과 렌더 타깃 픽셀 크기·Outline 확장량을 전달한다.
cbuffer OutlineConstants : register(b0)
{
    matrix ViewProjection;
    float4 ViewportAndThickness;
};

// 확장된 Clip 위치와 선택 Outline 색상을 전달한다.
struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
};

// 중심에서 바깥으로 향하는 투영 방향을 픽셀 공간에서 정규화해 일정 두께로 확장한다.
PS_INPUT mainVS(FVertexFactoryInput Input)
{
    const FVertexFactoryIntermediates VFIntermediates = GetVertexFactoryIntermediates(Input);
    const float4 WorldPosition = VertexFactoryGetWorldPosition(Input, VFIntermediates);
    const float3 WorldNormal = VertexFactoryGetWorldNormal(Input, VFIntermediates);

    float4 ClipPosition = mul(WorldPosition, ViewProjection);
    const float4 ClipDirection = mul(float4(WorldNormal, 0.0f), ViewProjection);
    const float2 ViewportSize = max(ViewportAndThickness.xy, float2(1.0f, 1.0f));
    
    // 원근 나눗셈의 방향 미분을 사용하고 W 보정으로 원근·직교 모두 픽셀 폭을 유지한다.
    float2 pixelDirection = (ClipDirection.xy * ClipPosition.w - ClipPosition.xy * ClipDirection.w) * ViewportSize;
    
    const float lengthSquared = dot(pixelDirection, pixelDirection);
    
    if (ClipPosition.w > 1.0e-6f && lengthSquared > 1.0e-12f)
    {
        pixelDirection *= rsqrt(lengthSquared);
        ClipPosition.xy += pixelDirection * (2.0f * ViewportAndThickness.z / ViewportSize) * ClipPosition.w;
    }

    PS_INPUT Output;
    Output.Position = ClipPosition;
    Output.Color = float4(1.0f, 1.0f, 0.0f, 1.0f);
    return Output;
}

// 선택 Outline의 노란색을 출력한다.
float4 mainPS(PS_INPUT Input) : SV_TARGET
{
    return Input.Color;
}
