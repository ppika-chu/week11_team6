#pragma once

// ViewMode의 enum과 UI 이름을 생성한다. 기존 enum 순서를 유지한다.
// 뷰 모드는 화면을 "어떻게" 그릴지 하나만 고른다. 무엇을 그릴지(켜고 끄기)는 아래 Show Flag가 맡는다.
// Symbol, 전체 이름(드롭다운 버튼), 메뉴 그룹, 서브메뉴 안의 짧은 이름. 그룹이 None이면 최상위 항목이다.
#define VIEW_MODE_LIST(X)                                               \
	X(Lit_Gouraud, "Lit (Gouraud)", Lit, "Gouraud")                     \
	X(Lit_Lambert, "Lit (Lambert)", Lit, "Lambert")                     \
	X(Lit_BlinnPhong, "Lit (Blinn-Phong)", Lit, "Blinn-Phong")          \
	X(Unlit, "Unlit", None, "Unlit")                                    \
	X(WorldNormal, "World Normal", BufferVisualization, "World Normal") \
	X(Wireframe, "Wireframe", None, "Wireframe")                        \
	X(SceneDepth, "Scene Depth", BufferVisualization, "Scene Depth")    \
	X(Lit, "Lit", Lit, "Material")


// Symbol, 비트, UI 이름, 기본값, 적용 범위, UI 그룹.
// UE처럼 Show Flag는 뷰포트마다 따로 가진다. 뷰포트 툴바의 "Show Flags" 드롭다운에 나온다.
// 표시 항목을 추가할 때 수정하는 공통 목록이다.
#define SHOW_FLAG_LIST(X)                                                   \
	X(Primitives, 1u << 0, "Primitives", true, View, Viewport)              \
	X(Bounds, 1u << 1, "Bounding Box", false, View, Viewport)               \
	X(UUID, 1u << 2, "Object UUID", false, View, Viewport)                  \
	X(FXAA, 1u << 3, "FXAA", true, View, Viewport)                          \
	/* 1u << 4: 예전 Depth View. 뷰 모드 Scene Depth로 옮겼다. */          \
	X(Fog, 1u << 5, "Exponential Height Fog", true, View, Viewport)         \
	X(Grid, 1u << 6, "Grid", true, View, Viewport)                          \
	/* 1u << 7: 예전 PSGrid 토글. 그리드 방식은 투영에 따라 고정된다. */ \
	/* 1u << 8: 예전 Wireframe. 뷰 모드 Wireframe으로 옮겼다. */          \
	X(DynamicShadows, 1u << 9, "Dynamic Shadows", true, View, Viewport)     \
	X(ShadowCascades, 1u << 10, "Shadow Cascades", false, View, Viewport)   \
	X(Decals, 1u << 11, "Decals", true, View, Viewport)						\
	X(SkeletalMesh, 1u << 12, "Skeletal Mesh", true, View, Viewport)
