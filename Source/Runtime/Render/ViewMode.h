#pragma once

#include "Core/Types.h"
#include "Render/RenderEnums.h"
#include "Render/ViewSettingsList.h"

// 뷰포트 조명 표시 모드. UberLit.hlsl을 어떤 매크로로 컴파일한 변형을 쓸지 고른다.
enum class EViewMode : uint8
{
#define MAKE_VIEW_MODE_ENUM(Symbol, Label, Group, MenuLabel) Symbol,
	VIEW_MODE_LIST(MAKE_VIEW_MODE_ENUM)
#undef MAKE_VIEW_MODE_ENUM
	Count
};

// 일반 Lit에서만 머티리얼의 모델을 사용하고, 강제 뷰 모드는 그대로 둔다.
inline EViewMode ResolveMaterialViewMode(EViewMode Mode, ELightingModel Model)
{
	if (Mode != EViewMode::Lit)
	{
		return Mode;
	}
	switch (Model)
	{
	case ELightingModel::Gouraud: return EViewMode::Lit_Gouraud;
	case ELightingModel::Lambert: return EViewMode::Lit_Lambert;
	case ELightingModel::Unlit: return EViewMode::Unlit;
	default: return EViewMode::Lit_BlinnPhong;
	}
}

inline constexpr const char* ViewModeNames[] = {
#define MAKE_VIEW_MODE_NAME(Symbol, Label, Group, MenuLabel) Label,
	VIEW_MODE_LIST(MAKE_VIEW_MODE_NAME)
#undef MAKE_VIEW_MODE_NAME
};

// 뷰 모드 드롭다운의 묶음. 선언 순서대로 메뉴에 그린다 (Lit ▸ / 최상위 항목 / Buffer Visualization ▸).
enum class EViewModeGroup : uint8
{
	Lit,
	None,
	BufferVisualization,
	Count
};

inline constexpr const char* ViewModeGroupNames[] = { "Lit", "", "Buffer Visualization" };

inline constexpr EViewModeGroup ViewModeGroups[] = {
#define MAKE_VIEW_MODE_GROUP(Symbol, Label, Group, MenuLabel) EViewModeGroup::Group,
	VIEW_MODE_LIST(MAKE_VIEW_MODE_GROUP)
#undef MAKE_VIEW_MODE_GROUP
};

inline constexpr const char* ViewModeMenuNames[] = {
#define MAKE_VIEW_MODE_MENU_NAME(Symbol, Label, Group, MenuLabel) MenuLabel,
	VIEW_MODE_LIST(MAKE_VIEW_MODE_MENU_NAME)
#undef MAKE_VIEW_MODE_MENU_NAME
};


inline const char* GetViewModeName(EViewMode Mode)
{
	const uint32 Index = static_cast<uint32>(Mode);
	return Index < static_cast<uint32>(EViewMode::Count) ? ViewModeNames[Index] : "Unknown";
}

inline EViewModeGroup GetViewModeGroup(EViewMode Mode)
{
	const uint32 Index = static_cast<uint32>(Mode);
	return Index < static_cast<uint32>(EViewMode::Count) ? ViewModeGroups[Index] : EViewModeGroup::None;
}

// 서브메뉴 안에서 쓰는 짧은 이름. 예: Lit ▸ "Blinn-Phong"
inline const char* GetViewModeMenuName(EViewMode Mode)
{
	const uint32 Index = static_cast<uint32>(Mode);
	return Index < static_cast<uint32>(EViewMode::Count) ? ViewModeMenuNames[Index] : "Unknown";
}
