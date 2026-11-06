#pragma pack_matrix(row_major)

#include "ColorSpace.hlsli"
#include "LightingCommon.hlsli"

cbuffer MaterialParams : register(b1)
{
    float4 DiffuseOrFactor; // 선형 RGB: 색상 맵이 없으면 diffuse, 있으면 맵 RGB에 곱하는 factor. 알파는 불투명도
    float2 UVOffset;
    float bOpaque;
    float Shininess; // Blinn-Phong 광택 지수 (Material.h FStaticMeshMaterialParams::Shininess)
    float2 WorldUVTileSize; // 텍스처 한 장의 월드 크기 (가로, 세로). X가 0이면 꺼짐, Y가 0이면 X와 같게 본다
    float2 UVTiling;        // 메시 UV 반복 횟수 (Material.h FStaticMeshMaterialParams::UVTiling)
    float WorldUVLocalSpace; // 1이면 World Tile을 메시 로컬 축 기준으로 깐다
    float3 MaterialPadding;
    float3 SpecularColor;
    float SpecularFactor;
    float3 EmissiveColor;
    float EmissiveFactor;
};

// Renderer.h의 FPerObjectConstants와 바이트 단위로 같아야 한다.
cbuffer PerObjectConstants : register(b2)
{
    matrix World;
    matrix WorldInverseTranspose; // (World^-1)^T
    uint ReceiveShadow;
    uint3 ObjectPadding;
};

Texture2D DiffuseTexture : register(t0);
Texture2D NormalTexture : register(t1);
SamplerState MaterialSampler : register(s0);

struct VS_INPUT
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float4 Tangent : TANGENT;   // w = 바이탄젠트 부호
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
};

// VS가 출력하고 PS가 입력으로 받는다.
struct VS_OUTPUT
{
    float4 SvPosition : SV_POSITION; // VS: 클립 공간 / PS: 픽셀 좌표(xy), 깊이(z), View 깊이(w)
    float3 WorldPos : TEXCOORD1;
    float3 Normal : NORMAL;
    float4 Tangent : TANGENT;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR;
    // World Tile 로컬 모드용. b2(물체 행렬)는 VS에만 묶이므로 VS에서 계산해 넘긴다.
    float3 ObjectPos : TEXCOORD3;    // 로컬 위치 × 물체 스케일 (월드 단위 크기, 로컬 축 방향)
    float3 ObjectNormal : TEXCOORD4; // 로컬 법선 (어느 로컬 축을 향한 면인지 고를 때)
    nointerpolation float3 ObjectAxisX : TEXCOORD5; // 메시 로컬 X·Y·Z 축의 월드 방향 (노멀맵 TBN용)
    nointerpolation float3 ObjectAxisY : TEXCOORD6;
    nointerpolation float3 ObjectAxisZ : TEXCOORD7;
#if defined(LIGHTING_MODEL_LAMBERT) || defined(LIGHTING_MODEL_BLINN_PHONG)
    // 물체별 정수 설정은 삼각형 내부에서 보간하지 않는다.
    nointerpolation uint ReceivesShadow : TEXCOORD2;
#endif
#if LIGHTING_MODEL_GOURAUD
    float3 VertexDiffuse : COLOR1;   // 정점에서 미리 계산한 Diffuse 조명
#endif
};

#if defined(LIGHTING_MODEL_LAMBERT) || defined(LIGHTING_MODEL_BLINN_PHONG)

// 전환 구간은 두 색을 섞고, 범위 밖이나 사용 불가능한 맵은 회색으로 표시한다.
float3 GetDirectionalCascadeDebugColor(float3 WorldPos)
{
    const float ViewDepth = dot(WorldPos - CameraWorldPos, CameraForward);
    const int Selected = SelectDirectionalCascade(ViewDepth);
    if (Selected < 0)
        return float3(0.25f, 0.25f, 0.25f);

    const uint CascadeIndex = uint(Selected);
    if (!CanEvaluateShadow(DirectionalShadowIndices[CascadeIndex]))
        return float3(0.25f, 0.25f, 0.25f);

    static const float3 Colors[4] =
    {
        float3(1.0f, 0.2f, 0.2f),
        float3(0.2f, 1.0f, 0.2f),
        float3(0.2f, 0.4f, 1.0f),
        float3(1.0f, 1.0f, 0.2f)
    };

    float3 Color = Colors[CascadeIndex];
    const float Blend = GetDirectionalCascadeBlend(CascadeIndex, ViewDepth);
    if (Blend > 0.0f)
        Color = lerp(Color, Colors[CascadeIndex + 1], Blend);
    return Color;
}
#endif

#if defined(GPU_SKINNING)

struct BoneMatrix
{
    matrix Matrix;
};
StructuredBuffer<BoneMatrix> BoneMatrices : register(t20);

struct SkinWeights
{
	uint2 PackedBones; // [0] = b0 | b1 << 16, [1] = b2 | b3 << 16
    float4 Weights;
};
StructuredBuffer<SkinWeights> SkinWeightsBuffer : register(t21);

void ApplySkinning(inout VS_INPUT Input, uint VertexID)
{
	const SkinWeights Weights = SkinWeightsBuffer[VertexID];
    const uint Bones[4] = { Weights.PackedBones.x & 0xFFFF, Weights.PackedBones.x >> 16, Weights.PackedBones.y & 0xFFFF, Weights.PackedBones.y >> 16 };
    float3 Position = float3(0.0f, 0.0f, 0.0f);
    float3 Normal = float3(0.0f, 0.0f, 0.0f);
    float3 Tangent = float3(0.0f, 0.0f, 0.0f);
	[unroll]
    for (int k = 0; k < 4; ++k)
    {
	    const float W = Weights.Weights[k];
        const matrix SkinMatrix = BoneMatrices[Bones[k]].Matrix;
        Position += W * mul(float4(Input.Position, 1.0f), SkinMatrix).xyz;
        Normal += W * mul(float4(Input.Normal, 0.0f), SkinMatrix).xyz;
        Tangent += W * mul(float4(Input.Tangent.xyz, 0.0f), SkinMatrix).xyz;
    }
    Input.Position = Position;
    Input.Normal = normalize(Normal);
    Input.Tangent.xyz = normalize(Tangent);
}

#endif

VS_OUTPUT mainVS(VS_INPUT Input, uint VertexID : SV_VertexID)
{
#if defined(GPU_SKINNING)
    ApplySkinning(Input, VertexID);
#endif

    VS_OUTPUT Output;

    const float4 WorldPos = mul(float4(Input.Position, 1.0f), World);
    Output.SvPosition = mul(WorldPos, ViewProjection);
    Output.WorldPos = WorldPos.xyz;

    // 노멀은 역전치로, 탄젠트는 표면을 따라 놓인 방향이라 World로 변환한다.
    // World가 뒤집혀 있으면(행렬식 < 0) 바이탄젠트 부호도 뒤집는다.
    Output.Normal = mul(float4(Input.Normal, 0.0f), WorldInverseTranspose).xyz;
    Output.Tangent = float4(mul(float4(Input.Tangent.xyz, 0.0f), World).xyz, Input.Tangent.w * (determinant((float3x3) World) < 0.0f ? -1.0f : 1.0f));

    Output.UV = Input.UV;
    Output.Color = Input.Color;

    // World 행렬의 0~2행 = 로컬 X·Y·Z 축 × 스케일. 길이가 스케일, 정규화하면 축의 월드 방향이다.
    const float3 ObjectScale = float3(length(World[0].xyz), length(World[1].xyz), length(World[2].xyz));
    Output.ObjectPos = Input.Position * ObjectScale;
    Output.ObjectNormal = Input.Normal;
    Output.ObjectAxisX = World[0].xyz / max(ObjectScale.x, 1e-6f);
    Output.ObjectAxisY = World[1].xyz / max(ObjectScale.y, 1e-6f);
    Output.ObjectAxisZ = World[2].xyz / max(ObjectScale.z, 1e-6f);
#if defined(LIGHTING_MODEL_LAMBERT) || defined(LIGHTING_MODEL_BLINN_PHONG)
    // 기존 b2의 VS 전용 바인딩을 유지하면서 PS에 전달한다.
    Output.ReceivesShadow = ReceiveShadow;
#endif
#if LIGHTING_MODEL_GOURAUD
    const float3 V = normalize(CameraWorldPos - Output.WorldPos);
    // Gouraud도 공통 조명 함수의 정점 노멀 인자 규약을 맞춘다.
    const float3 VertexNormal = normalize(Output.Normal);
    const FLightContribution Contribution = AccumulateDirectLighting(VertexNormal, V, VertexNormal, Output.WorldPos, 1, Output.SvPosition, false, false);
    Output.VertexDiffuse = Contribution.Diffuse;
#endif
    return Output;
}

float4 mainPS(VS_OUTPUT Input) : SV_TARGET
{
    float2 UV = Input.UV * UVTiling + UVOffset;
    float3 WorldUVTangent = float3(0.0f, 0.0f, 0.0f);
    float3 WorldUVBitangent = float3(0.0f, 0.0f, 0.0f);
    if (WorldUVTileSize.x > 0.0f)
    {
        // 가로(U)는 X 크기로, 세로(V)는 Y 크기로 나눈다. Y가 0이면 정사각형으로 본다.
        const float2 TileSize = float2(WorldUVTileSize.x, WorldUVTileSize.y > 0.0f ? WorldUVTileSize.y : WorldUVTileSize.x);

        // 월드 모드는 월드 위치·월드 축, 로컬 모드는 (스케일된) 로컬 위치·메시 로컬 축으로 같은 투영을 한다.
        const bool bLocal = WorldUVLocalSpace > 0.5f;
        const float3 P = bLocal ? Input.ObjectPos : Input.WorldPos;
        const float3 AxisWeight = abs(normalize(bLocal ? Input.ObjectNormal : Input.Normal));
        const float3 AxisX = bLocal ? normalize(Input.ObjectAxisX) : float3(1.0f, 0.0f, 0.0f);
        const float3 AxisY = bLocal ? normalize(Input.ObjectAxisY) : float3(0.0f, 1.0f, 0.0f);
        const float3 AxisZ = bLocal ? normalize(Input.ObjectAxisZ) : float3(0.0f, 0.0f, 1.0f);

        if (AxisWeight.z >= AxisWeight.x && AxisWeight.z >= AxisWeight.y)
        {
            UV = float2(P.y, P.x) / TileSize;
            WorldUVTangent = AxisY;
            WorldUVBitangent = AxisX;
        }
        else if (AxisWeight.x >= AxisWeight.y)
        {
            UV = float2(P.y, -P.z) / TileSize;
            WorldUVTangent = AxisY;
            WorldUVBitangent = -AxisZ;
        }
        else
        {
            UV = float2(P.x, -P.z) / TileSize;
            WorldUVTangent = AxisX;
            WorldUVBitangent = -AxisZ;
        }
    }
    const float4 TexColor = DiffuseTexture.Sample(MaterialSampler, UV);

#if !defined(EDITOR_ARROW_SHADER)
    const float3 Albedo = SRGBToLinear(TexColor.rgb) * DiffuseOrFactor.rgb;

    if (LightDebugMode == LIGHT_DEBUG_MODE_CELL_HEATMAP && NumTilesX > 0)
    {
        const uint CellLightCount = CellLightIndices[GetCellListOffset(Input.SvPosition)];
        return float4(lerp(Albedo * 0.2f, GetHeatmapColor(CellLightCount), 0.7f), 1.0f);
    }
    // 슬라이스 시각화: 이웃 슬라이스끼리 색이 크게 달라지도록 황금비로 색상환을 돈다.
    if (LightDebugMode == LIGHT_DEBUG_MODE_SLICES && NumTilesX > 0)
    {
        const float Hue = frac(GetSliceIndex(Input.SvPosition) * 0.618034f);
        const float3 SliceColor = saturate(abs(frac(Hue + float3(0.0f, 2.0f, 1.0f) / 3.0f) * 6.0f - 3.0f) - 1.0f);
        return float4(lerp(Albedo * 0.2f, SliceColor, 0.7f), 1.0f);
    }
#endif

    const float3 VertexNormal = { normalize(Input.Normal) };
#if defined(LIGHTING_MODEL_LAMBERT) || defined(LIGHTING_MODEL_BLINN_PHONG) || defined(WORLD_NORMAL)
    // TBN: 노멀맵의 탄젠트 공간 노멀을 월드 공간으로 옮긴다.
    const bool bWorldUV = WorldUVTileSize.x > 0.0f;
    const float3 MeshT = { normalize(Input.Tangent.xyz - VertexNormal * dot(VertexNormal, Input.Tangent.xyz)) };
    const float3 T = { bWorldUV ? WorldUVTangent : MeshT };
    const float3 B = { bWorldUV ? WorldUVBitangent : cross(VertexNormal, MeshT) * (Input.Tangent.w < 0.0f ? -1.0f : 1.0f) };
    const float3 TangentNormal = { NormalTexture.Sample(MaterialSampler, UV).xyz * 2.0f - 1.0f };
    const float3 N = { normalize(T * TangentNormal.x + B * -TangentNormal.y + VertexNormal * TangentNormal.z) };
#endif

    // Opaque는 알파를 1로 고정한다. 뷰포트 RT를 ImGui가 알파 블렌딩으로 그리므로 알파가 남으면 비쳐 보인다
    const float Alpha = { bOpaque > 0.5f ? 1.0f : TexColor.a * DiffuseOrFactor.a };

#if defined(EDITOR_ARROW_SHADER)
    return float4(LinearToSRGB(TexColor.rgb * DiffuseOrFactor.rgb + EmissiveColor * EmissiveFactor), Alpha);
#elif LIGHTING_MODEL_UNLIT
    return float4(Albedo + EmissiveColor * EmissiveFactor, Alpha);
#elif WORLD_NORMAL
    return float4(N * 0.5f + 0.5f, 1.0f);
#else
    float3 DiffuseLighting = { GetAmbientLighting() };
    float3 SpecularLighting = { 0.0f, 0.0f, 0.0f };

#if defined(LIGHTING_MODEL_LAMBERT) || defined(LIGHTING_MODEL_BLINN_PHONG)
    const float3 V = { normalize(CameraWorldPos - Input.WorldPos) };
    const FLightContribution Contribution = { AccumulateDirectLighting(N, V, VertexNormal, Input.WorldPos, Shininess, Input.SvPosition, true, Input.ReceivesShadow != 0) };
    DiffuseLighting += Contribution.Diffuse;
    SpecularLighting = Contribution.Specular * SpecularColor * SpecularFactor;
#elif LIGHTING_MODEL_GOURAUD
    DiffuseLighting += Input.VertexDiffuse;
#endif

    // 먼저 기존 조명과 그림자를 모두 반영한 표면 색을 계산한다.
    float3 FinalColor = Albedo * DiffuseLighting + SpecularLighting + EmissiveColor * EmissiveFactor;

#if defined(LIGHTING_MODEL_LAMBERT) || defined(LIGHTING_MODEL_BLINN_PHONG)
    if (VisualizeShadowCascades != 0 && Input.ReceivesShadow != 0)
    {
        const float3 L = -normalize(Directional.Direction);

        // Directional의 깊이 비교 결과로만 마스크를 만든다.
        const float ShadowMask = saturate(1.0f - GetDirectionalShadowVisibility(Input.WorldPos, VertexNormal, L));
        const float3 CascadeColor = GetDirectionalCascadeDebugColor(Input.WorldPos);

        // 그림자로 가려진 부분에만 색상을 섞고 기존 Alpha는 유지한다.
        FinalColor = lerp(FinalColor, CascadeColor, ShadowMask * 0.8f);
    }
#endif
    return float4(FinalColor, Alpha);
#endif
}
