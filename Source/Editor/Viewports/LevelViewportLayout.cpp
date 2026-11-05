#include "EnginePCH.h"
#include "Editor/Viewports/LevelViewportLayout.h"

#include "Math/EngineMath.h"

#include <algorithm>
#include <cassert>

namespace
{
// Splitter가 창 가장자리에 붙지 않도록 두 비율을 대칭 범위로 제한한다.
FSplitRatio ClampSplitRatio(const FSplitRatio& Raw, const float MinRatio)
{
	assert(MinRatio >= 0.0f && MinRatio <= 0.5f);
	return {
		FMath::Clamp(Raw.Horizontal, MinRatio, 1.0f - MinRatio),
		FMath::Clamp(Raw.Vertical, MinRatio, 1.0f - MinRatio)
	};
}
}

void FLevelViewportLayout::SetSingleViewIndex(int32 ViewIndex)
{
	assert(ViewIndex >= 0 && ViewIndex < ViewCount);
	SingleViewIndex = ViewIndex;
}

void FLevelViewportLayout::SetSplitRatio(const FSplitRatio& Ratio)
{
	SplitRatio = ClampSplitRatio(Ratio, MinimumSplitRatio);
}

// Drag 픽셀을 창 축 길이로 나눠 Split 비율에 누적하고 제한한다.
void FLevelViewportLayout::ApplySplitterDrag(const EDragAxis Axis, const float DeltaPixels, const FVector2 WindowSize)
{
	FSplitRatio Result = SplitRatio;
	if (Axis == EDragAxis::Horizontal)
	{
		assert(WindowSize.X > 0.0f);
		Result.Horizontal += DeltaPixels / WindowSize.X;
	}
	else
	{
		assert(WindowSize.Y > 0.0f);
		Result.Vertical += DeltaPixels / WindowSize.Y;
	}
	SplitRatio = ClampSplitRatio(Result, MinimumSplitRatio);
}

// Split 위치로 창을 빈틈없는 네 Rect로 나눈 뒤 레이아웃 모드를 적용한다.
void FLevelViewportLayout::Update(const FVector2 WindowSize, const float GutterWidth)
{
	assert(WindowSize.X >= 0.0f && WindowSize.Y >= 0.0f);
	const float LeftWidth = WindowSize.X * SplitRatio.Horizontal;
	const float TopHeight = WindowSize.Y * SplitRatio.Vertical;
	const float RightWidth = WindowSize.X - LeftWidth;
	const float BottomHeight = WindowSize.Y - TopHeight;
	ViewRects[0] = { 0.0f, 0.0f, LeftWidth, TopHeight };
	ViewRects[1] = { LeftWidth, 0.0f, RightWidth, TopHeight };
	ViewRects[2] = { 0.0f, TopHeight, LeftWidth, BottomHeight };
	ViewRects[3] = { LeftWidth, TopHeight, RightWidth, BottomHeight };

	if (LayoutMode == ELayoutMode::Single)
	{
		for (int32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex)
		{
			ViewRects[ViewIndex] = ViewIndex == SingleViewIndex
				? FRect{ 0.0f, 0.0f, WindowSize.X, WindowSize.Y }
				: FRect{};
		}
	}
	else
	{
		ApplySplitterGutter(WindowSize, GutterWidth);
	}
}

bool FLevelViewportLayout::IsViewActive(const int32 ViewIndex) const
{
	return ViewIndex >= 0 && ViewIndex < ViewCount && IsViewRectValid(ViewRects[ViewIndex]) && (LayoutMode == ELayoutMode::QuadSplit || ViewIndex == SingleViewIndex);
}

// 네 Rect를 순서대로 검사해 좌표를 포함하는 View를 찾는다.
int32 FLevelViewportLayout::FindViewAt(const FVector2 Position) const
{
	for (int32 Index = 0; Index < ViewCount; ++Index)
	{
		const FRect& Rect = ViewRects[Index];
		if (IsViewRectValid(Rect) && Position.X >= Rect.X && Position.X < Rect.X + Rect.Width && Position.Y >= Rect.Y && Position.Y < Rect.Y + Rect.Height)
		{
			return Index;
		}
	}
	return InvalidViewIndex;
}

const FRect& FLevelViewportLayout::GetViewRect(const int32 ViewIndex) const
{
	assert(ViewIndex >= 0 && ViewIndex < ViewCount);
	return ViewRects[ViewIndex];
}

void FLevelViewportLayout::ApplySplitterGutter(const FVector2 WindowSize, const float GutterWidth)
{
	const float SplitX = ViewRects[0].Width;
	const float SplitY = ViewRects[0].Height;
	const float HalfThickness = GutterWidth * 0.5f;
	const float HalfGapX = std::min({ HalfThickness, SplitX, WindowSize.X - SplitX });
	const float HalfGapY = std::min({ HalfThickness, SplitY, WindowSize.Y - SplitY });
	const float RightX = SplitX + HalfGapX;
	const float BottomY = SplitY + HalfGapY;
	const float LeftWidth = SplitX - HalfGapX;
	const float TopHeight = SplitY - HalfGapY;
	const float RightWidth = WindowSize.X - RightX;
	const float BottomHeight = WindowSize.Y - BottomY;

	ViewRects[0] = { 0.0f, 0.0f, LeftWidth, TopHeight };
	ViewRects[1] = { RightX, 0.0f, RightWidth, TopHeight };
	ViewRects[2] = { 0.0f, BottomY, LeftWidth, BottomHeight };
	ViewRects[3] = { RightX, BottomY, RightWidth, BottomHeight };
}
