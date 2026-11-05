#pragma once

#include "LightweightStats.h"

// "stat decal"로 보는 데칼 패스 통계. 뷰마다 데칼 패스가 돌므로 쿼드 뷰에서는 뷰 수만큼 합쳐진다.
namespace DecalStats
{
// 데칼 패스 전체(정렬 + 후보 검사 + 드로우 제출)에 걸린 CPU 시간이다.
DECLARE_CYCLE_STAT("Decal Pass CPU", STAT_PassCPU);
// Broad Phase(BVH)와 Narrow Phase(SAT)로 수신 메시를 고른 시간이다.
DECLARE_CYCLE_STAT("Decal Receiver Gather CPU", STAT_ReceiverCPU);
// OBBTree로 수신 메시 안의 삼각형을 고른 시간이다.
DECLARE_CYCLE_STAT("Decal Triangle Gather CPU", STAT_TriangleCPU);

DECLARE_COUNTER_STAT("Decals In Scene", STAT_SceneDecals);
// 수신자가 있어 실제로 그린 데칼. 머티리얼이 없거나 페이드로 사라졌거나 화면 밖인 데칼은 빠진다.
DECLARE_COUNTER_STAT("Decals Rendered", STAT_RenderedDecals);
// (데칼, 수신 메시) 쌍의 수다.
DECLARE_COUNTER_STAT("Decal Receivers", STAT_Receivers);
DECLARE_COUNTER_STAT("Decal Draw Calls", STAT_DrawCalls);
// 데칼 셰이더로 다시 그린 삼각형 수다.
DECLARE_COUNTER_STAT("Decal Triangles", STAT_Triangles);
}
