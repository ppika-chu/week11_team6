#pragma once

#include "Editor/Viewports/EditorViewportTypes.h"

// 에디터 네 View의 화면 배치(Single·Quad)와 Splitter 비율, 좌표 판정을 관리한다.
class FLevelViewportLayout
{
public:
	static constexpr int32 ViewCount = 4;

	// 배치 모드는 다음 Update에서 Rect에 반영한다.
	void SetLayoutMode(ELayoutMode Mode) { LayoutMode = Mode; }
	ELayoutMode GetLayoutMode() const { return LayoutMode; }

	// Single로 확대할 View는 네 View 중에서 선택한다.
	void SetSingleViewIndex(int32 ViewIndex);
	int32 GetSingleViewIndex() const { return SingleViewIndex; }

	// 외부 설정은 허용 범위로 제한한다.
	void SetSplitRatio(const FSplitRatio& Ratio);
	const FSplitRatio& GetSplitRatio() const { return SplitRatio; }

	// 픽셀 Drag를 창 크기로 정규화해 해당 Split 비율에 반영한다. 화면 배치는 Update에서 계산한다.
	void ApplySplitterDrag(EDragAxis Axis, float DeltaPixels, FVector2 WindowSize);

	// Single은 선택한 View만 확대하고, Quad는 Splitter 여백을 적용한다.
	void Update(FVector2 WindowSize, float GutterWidth);

	// Single에서는 선택된 View만, Quad에서는 유효한 Rect를 가진 View를 활성화한다.
	bool IsViewActive(int32 ViewIndex) const;

	// 패널 Rect만 판정하므로 Splitter gutter는 입력 대상에 포함하지 않는다.
	int32 FindViewAt(FVector2 Position) const;

	const FRect& GetViewRect(int32 ViewIndex) const;

private:
	// 중앙 경계에서 양쪽 Rect를 절반씩 줄여 실제 Splitter 여백을 만든다.
	void ApplySplitterGutter(FVector2 WindowSize, float GutterWidth);

	static constexpr float MinimumSplitRatio = 0.1f;
	ELayoutMode LayoutMode = ELayoutMode::Single;
	FSplitRatio SplitRatio{};
	FRect ViewRects[ViewCount]{};
	int32 SingleViewIndex = 0;
};
