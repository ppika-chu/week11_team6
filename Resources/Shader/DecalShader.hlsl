#pragma pack_matrix(row_major)

#include "VertexFactory.hlsli"
#include "ColorSpace.hlsli"
// ViewProjection·CameraWorldPos(b0)와 라이트·그림자 리소스는 Base Pass가 묶어 둔 것을 그대로 쓴다.
#include "LightingCommon.hlsli"

// 투영 데칼. 데칼 상자와 겹치는 메시를 Base Pass 뒤에 한 번 더 그려서, 상자 안 픽셀에만 데칼 텍스처를 입힌다.
// DecalRenderer.h의 FDecalConstants와 바이트 단위로 같아야 한다.
cbuffer DecalConstants : register(b3)
{
    matrix DecalVP; // UDecalComponent::GetDecalViewProjection(): 월드 → 데칼
    float4 DecalColor; // 선형 머티리얼 DiffuseOrFactor × 선형 컴포넌트 DecalColor
    float FadeAlpha; // Fade In/Out 결과 (0~1)
    float bAdditive; // 1이면 One/One 블렌드용으로 색에 알파를 미리 곱한다 (Fake Spot Light)
    float Shininess;
    uint DecalPadding;
    float3 SpecularColor;
    float SpecularFactor;
    float3 EmissiveColor;
    float EmissiveFactor;
};

Texture2D DecalTexture : register(t0);
Texture2D DecalNormalTexture : register(t1);
SamplerState DecalSampler : register(s0); // LinearClamp

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 decalPosition : TEXCOORD0; // 데칼 로컬 좌표
    float3 worldPosition : TEXCOORD1;
    float3 normal : NORMAL;
};

PS_INPUT mainVS(FVertexFactoryInput Input)
{
    const FVertexFactoryIntermediates VFIntermediates = GetVertexFactoryIntermediates(Input);
    const float4 WorldPosition = VertexFactoryGetWorldPosition(Input, VFIntermediates);

    PS_INPUT Output;
    Output.position = mul(WorldPosition, ViewProjection);
    Output.decalPosition = mul(WorldPosition, DecalVP).xyz;
    Output.worldPosition = WorldPosition.xyz;
    Output.normal = VertexFactoryGetWorldNormal(Input, VFIntermediates);
    return Output;
}

#if !LIGHTING_MODEL_UNLIT
// 노멀맵을 데칼 UV로 읽으므로 탄젠트 기준도 메시 탄젠트가 아니라 데칼 축으로 잡는다.
float3 GetDecalNormal(float3 VertexNormal, float2 uv)
{
    // DecalVP의 열은 월드 좌표에 대한 데칼 좌표의 기울기다. U는 데칼 로컬 +Y, V는 로컬 -Z로 늘어난다.
    const float3 AxisU = normalize(float3(DecalVP[0][1], DecalVP[1][1], DecalVP[2][1]));
    const float3 AxisV = -normalize(float3(DecalVP[0][2], DecalVP[1][2], DecalVP[2][2]));

    const float3 T = AxisU - VertexNormal * dot(VertexNormal, AxisU);
    const float3 B = AxisV - VertexNormal * dot(VertexNormal, AxisV);
    // 표면이 투영 축과 나란하면 축이 사라지므로 정점 노멀로 대신한다.
    if (dot(T, T) < 1e-4f || dot(B, B) < 1e-4f)
    {
        return VertexNormal;
    }

    // UberLit과 같은 규약이다 (G+가 텍스처 위쪽).
    const float3 TangentNormal = DecalNormalTexture.Sample(DecalSampler, uv).xyz * 2.0f - 1.0f;
    return normalize(normalize(T) * TangentNormal.x + normalize(B) * -TangentNormal.y + VertexNormal * TangentNormal.z);
}
#endif

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    const float3 p = input.decalPosition;
    
    if (any(abs(p) > 1.0f))
    {
        discard;
    }
    
    const float2 uv = float2(p.y, -p.z) * 0.5f + 0.5f;
    const float4 texel = DecalTexture.Sample(DecalSampler, uv);
    
    const float3 albedo = SRGBToLinear(texel.rgb) * DecalColor.rgb;
    const float alpha = texel.a * DecalColor.a * FadeAlpha;
    const float3 emission = EmissiveColor * EmissiveFactor;
    
    // Additive(Fake Spot Light)는 데칼 자체가 빛이므로 조명을 곱하지 않는다.
    if (bAdditive > 0.5f)
    {
        return float4((albedo + emission) * alpha, 1.0f);
    }

#if LIGHTING_MODEL_UNLIT
    return float4(albedo + emission, alpha);
#else
    // UberLit과 같은 공식으로 데칼 색을 조명한다. 그림자와 지오메트리 마스크에는 정점 노멀을 쓴다.
    const float3 VertexNormal = normalize(input.normal);
    const float3 N = GetDecalNormal(VertexNormal, uv);
    const float3 V = normalize(CameraWorldPos - input.worldPosition);
    const FLightContribution Light = AccumulateDirectLighting(N, V, VertexNormal, input.worldPosition, Shininess, input.position, true, ReceiveShadow != 0);

    const float3 lit = albedo * (GetAmbientLighting() + Light.Diffuse) + Light.Specular * SpecularColor * SpecularFactor + emission;
    return float4(lit, alpha);
#endif
}
