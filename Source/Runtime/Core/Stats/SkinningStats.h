#pragma once

#include "LightweightStats.h"

namespace SkinningStats
{
// CPU 스키닝은 스킨 메시 블렌딩 및 정점 업로드 시간을, GPU 스키닝은 팔레트 계산 및 업로드 시간을 측정
DECLARE_CYCLE_STAT("Skinning CPU", STAT_CPU);

// 스킨 정점 개수
DECLARE_COUNTER_STAT("Skinned Vertices", STAT_SkinnedVertices);
// 매 프레임 스킨 메시 업로드 바이트 수. CPU 스키닝은 전체 정점을 업로드하고 GPU 스키닝은 팔레트만 업로드함
DECLARE_COUNTER_STAT("Skin Upload Bytes", STAT_SkinUploadBytes);
}
