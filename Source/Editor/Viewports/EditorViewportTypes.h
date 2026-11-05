// 에디터 뷰포트의 화면 영역·카메라·레이아웃 값 타입을 정의한다.
#pragma once

#include "Core/Types.h"
#include "Math/Vector.h"
#include "Math/Vector2.h"
#include "Math/Quat.h"

// 좌표 규약: +X Forward, +Y Right, +Z Up.
// 행렬은 엔진 FMatrix의 row-vector 규약을 사용하며 이동은 M[3][0..2]에 둔다.
inline constexpr int32 InvalidViewIndex = -1;

// Quad View 사이에서 렌더링과 입력이 비워지는 Splitter gutter의 화면 픽셀 폭이다.
inline constexpr float SplitterThickness = 6.0f;

// 화면상의 좌상단 위치와 너비·높이를 담는다.
struct FRect
{
	float X, Y, Width, Height;
};

// Rect의 너비와 높이가 모두 양수인지 검사한다.
inline bool IsViewRectValid(const FRect& Rect) { return Rect.Width > 0.0f && Rect.Height > 0.0f; }

enum class EProjectionMode
{
	Perspective,
	Orthographic
};

enum class ELayoutMode
{
	Single,
	QuadSplit
};

// 카메라의 월드 위치와 회전을 담는다.
struct FCameraTransform
{
	FVector Location;
	FQuat Rotation{ 0.0f, 0.0f, 0.0f, 0.0f };
};

// 원근·직교 투영에 필요한 모드와 절두체 값을 담는다.
struct FCameraProjection
{
	EProjectionMode Mode;
	float FovDegrees;
	float OrthoWidth;
	float NearClip;
	float FarClip;
	bool mUseCameraClipPlanes{};
};

// 한 View에서 사용하는 카메라 Transform과 투영 설정을 담는다.
struct FViewCamera
{
	FCameraTransform Transform;
	FCameraProjection Projection;
};

// 한 프레임의 카메라 회전·이동·줌 입력을 담는다.
struct FCameraMoveInput
{
	FVector2 MouseDelta;
	FVector MoveAxis;
	float ZoomDelta;
};

// 가로·세로 Splitter가 차지하는 창의 비율을 담는다.
struct FSplitRatio
{
	float Horizontal = 0.5f;
	float Vertical = 0.5f;
};

enum class EDragAxis
{
	Horizontal,
	Vertical
};

// View의 투영·축 정렬 종류이며 툴바 콤보박스와 editor.ini의 저장 순서를 유지한다.
enum class ELevelViewportType
{
	Perspective,
	OrthographicView,
	Top,
	Bottom,
	Front,
	Back,
	Left,
	Right
};

// GridRenderer가 소유하는 기존 평면 enum을 재정의 없이 참조한다.
enum class EGridPlane : int32;
