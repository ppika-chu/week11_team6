#include "EnginePCH.h"

#include "Editor/Viewports/EditorViewportClient.h"
#include "Editor/Rendering/GridRenderer.h"
#include "Asset/LOD/StaticMeshLODSelector.h"
#include "Math/EngineMath.h"
#include "Math/Vector4.h"
#include "Render/SceneRenderer.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace
{
constexpr float Pi = 3.14159265358979323846f;
constexpr float Epsilon = 1.0e-6f;

// 기존 카메라 이동·회전·직교 투영 계산의 상수다.
constexpr float MaximumCameraPitchDegrees = 89.0f;
constexpr float MaximumRotationDegreesPerFrame = 45.0f;
constexpr float MouseWheelDeltaPerStep = 120.0f;
constexpr float OrthographicZoomFactorPerStep = 0.9f;
constexpr float PerspectiveWheelSpeed = 0.1f;
constexpr float MinimumOrthographicWidth = 0.01f;
// 양방향 깊이 보정 뒤에도 절두체 평면 법선이 epsilon보다 충분히 크게 유지되게 한다.
constexpr float MaximumOrthographicSpan = 80000.0f;
// 프레이밍한 물체가 화면 가장자리에 닿지 않게 두는 여백 배율이다.
constexpr float FrameBoundsPadding = 1.1f;
constexpr float FrameTransitionDuration = 0.3f;

FVector Add(const FVector A, const FVector B) { return { A.X + B.X, A.Y + B.Y, A.Z + B.Z }; }
FVector Subtract(const FVector A, const FVector B) { return { A.X - B.X, A.Y - B.Y, A.Z - B.Z }; }
FVector Scale(const FVector V, const float S) { return { V.X * S, V.Y * S, V.Z * S }; }
float Dot(const FVector A, const FVector B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
// 오른손 좌표계의 벡터 외적을 계산한다.
FVector Cross(const FVector A, const FVector B)
{
	return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X };
}
float LengthSquared(const FVector V) { return Dot(V, V); }
// 0이 아닌 벡터를 길이 1로 정규화한다.
FVector Normalize(const FVector V)
{
	const float Length = sqrtf(LengthSquared(V));
	assert(Length > Epsilon);
	return Scale(V, 1.0f / Length);
}

// 길이가 0이면 영벡터를, 아니면 정규화한 벡터를 반환한다.
FVector NormalizedOrZero(const FVector Value)
{
	const float Length2 = Value.X * Value.X + Value.Y * Value.Y + Value.Z * Value.Z;
	if (Length2 <= 0.0f)
	{
		return {};
	}
	const float InverseLength = 1.0f / std::sqrt(Length2);
	return { Value.X * InverseLength, Value.Y * InverseLength, Value.Z * InverseLength };
}

// 0이 아닌 quaternion을 길이 1로 정규화한다.
FQuat Normalize(const FQuat Q)
{
	const float Length = sqrtf(Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W);
	assert(Length > Epsilon);
	return { Q.X / Length, Q.Y / Length, Q.Z / Length, Q.W / Length };
}

// Hamilton product로 두 quaternion 회전을 합성한다.
FQuat Multiply(const FQuat A, const FQuat B)
{
	return {
		A.W * B.X + A.X * B.W + A.Y * B.Z - A.Z * B.Y,
		A.W * B.Y - A.X * B.Z + A.Y * B.W + A.Z * B.X,
		A.W * B.Z + A.X * B.Y - A.Y * B.X + A.Z * B.W,
		A.W * B.W - A.X * B.X - A.Y * B.Y - A.Z * B.Z
	};
}

// 단위 축과 각도로 회전 quaternion을 만든다.
FQuat AxisAngle(const FVector Axis, const float Radians)
{
	const float Half = Radians * 0.5f;
	const float Sine = sinf(Half);
	const FVector UnitAxis = Normalize(Axis);
	return { UnitAxis.X * Sine, UnitAxis.Y * Sine, UnitAxis.Z * Sine, cosf(Half) };
}

// 정규화 quaternion의 벡터 회전 공식을 사용해 방향을 회전한다.
FVector Rotate(const FQuat Rotation, const FVector V)
{
	const FQuat Q = Normalize(Rotation);
	const FVector U{ Q.X, Q.Y, Q.Z };
	return Add(Add(Scale(U, 2.0f * Dot(U, V)), Scale(V, Q.W * Q.W - Dot(U, U))), Scale(Cross(U, V), 2.0f * Q.W));
}

// 모든 원소가 0인 4x4 행렬을 만든다.
FMatrix ZeroMatrix()
{
	FMatrix Result;
	for (int Row = 0; Row < 4; ++Row)
	{
		for (int Column = 0; Column < 4; ++Column)
		{
			Result.M[Row][Column] = 0.0f;
		}
	}
	return Result;
}

// 회전된 카메라 기저와 위치 내적으로 View 행렬을 구성한다.
FMatrix BuildViewMatrix(const FCameraTransform& Transform)
{
	const FVector Forward = Rotate(Transform.Rotation, { 1.0f, 0.0f, 0.0f });
	const FVector Right = Rotate(Transform.Rotation, { 0.0f, 1.0f, 0.0f });
	const FVector Up = Rotate(Transform.Rotation, { 0.0f, 0.0f, 1.0f });
	FMatrix Result = ZeroMatrix();
	Result.M[0][0] = Forward.X;
	Result.M[1][0] = Forward.Y;
	Result.M[2][0] = Forward.Z;
	Result.M[3][0] = -Dot(Forward, Transform.Location);
	Result.M[0][1] = Right.X;
	Result.M[1][1] = Right.Y;
	Result.M[2][1] = Right.Z;
	Result.M[3][1] = -Dot(Right, Transform.Location);
	Result.M[0][2] = Up.X;
	Result.M[1][2] = Up.Y;
	Result.M[2][2] = Up.Z;
	Result.M[3][2] = -Dot(Up, Transform.Location);
	Result.M[3][3] = 1.0f;
	return Result;
}

// 원근은 FOV, 직교는 전체 폭을 기준으로 Projection 행렬을 구성한다.
FMatrix BuildProjectionMatrix(const FCameraProjection& Projection, const float AspectRatio)
{
	assert(AspectRatio > 0.0f);
	assert(Projection.NearClip > 0.0f && Projection.FarClip > Projection.NearClip);
	FMatrix Result = ZeroMatrix();
	if (Projection.Mode == EProjectionMode::Perspective)
	{
		assert(Projection.FovDegrees > 0.0f && Projection.FovDegrees < 180.0f);
		const float ScaleY = 1.0f / tanf(Projection.FovDegrees * Pi / 360.0f);
		Result.M[1][0] = ScaleY / AspectRatio;
		Result.M[2][1] = ScaleY;
		Result.M[0][2] = Projection.FarClip / (Projection.FarClip - Projection.NearClip);
		Result.M[3][2] = -Projection.NearClip * Projection.FarClip / (Projection.FarClip - Projection.NearClip);
		Result.M[0][3] = 1.0f;
	}
	else
	{
		assert(Projection.OrthoWidth > 0.0f);
		const float Height = Projection.OrthoWidth / AspectRatio;
		Result.M[1][0] = 2.0f / Projection.OrthoWidth;
		Result.M[2][1] = 2.0f / Height;
		Result.M[0][2] = 1.0f / (Projection.FarClip - Projection.NearClip);
		Result.M[3][2] = -Projection.NearClip / (Projection.FarClip - Projection.NearClip);
		Result.M[3][3] = 1.0f;
	}
	return Result;
}

// 화면 좌표를 NDC로 바꾸고 카메라 기저에 결합해 월드 Ray를 만든다.
FRay Deproject(const FViewCamera& Camera, const FVector2 ScreenPos, const FVector2 ViewportSize)
{
	assert(ViewportSize.X > 0.0f && ViewportSize.Y > 0.0f);
	const float NdcX = 2.0f * ScreenPos.X / ViewportSize.X - 1.0f;
	const float NdcY = 1.0f - 2.0f * ScreenPos.Y / ViewportSize.Y;
	const float AspectRatio = ViewportSize.X / ViewportSize.Y;
	const FVector Forward = Rotate(Camera.Transform.Rotation, { 1.0f, 0.0f, 0.0f });
	const FVector Right = Rotate(Camera.Transform.Rotation, { 0.0f, 1.0f, 0.0f });
	const FVector Up = Rotate(Camera.Transform.Rotation, { 0.0f, 0.0f, 1.0f });

	if (Camera.Projection.Mode == EProjectionMode::Perspective)
	{
		const float Tangent = tanf(Camera.Projection.FovDegrees * Pi / 360.0f);
		const FVector Direction = Normalize(Add(Forward, Add(Scale(Right, NdcX * Tangent * AspectRatio), Scale(Up, NdcY * Tangent))));
		return { Camera.Transform.Location, Direction };
	}

	assert(Camera.Projection.OrthoWidth > 0.0f);
	const float Height = Camera.Projection.OrthoWidth / AspectRatio;
	const FVector Origin = Add(Add(Add(Camera.Transform.Location, Scale(Forward, Camera.Projection.NearClip)), Scale(Right, NdcX * Camera.Projection.OrthoWidth * 0.5f)), Scale(Up, NdcY * Height * 0.5f));
	return { Origin, Normalize(Forward) };
}

// 로컬 이동을 월드로 회전하고 Yaw 뒤 현재 Right축 Pitch를 합성한다.
FViewCamera ApplyCameraMovement(const FViewCamera& Current, const FCameraMoveInput& Input, const float DeltaTime)
{
	assert(DeltaTime >= 0.0f);
	FViewCamera Result = Current;
	const FVector LocalTranslation{ Input.MoveAxis.X + Input.ZoomDelta, Input.MoveAxis.Y, Input.MoveAxis.Z };
	Result.Transform.Location = Add(Result.Transform.Location, Scale(Rotate(Current.Transform.Rotation, LocalTranslation), DeltaTime));

	if (Input.MouseDelta.X != 0.0f || Input.MouseDelta.Y != 0.0f)
	{
		const FQuat Yaw = AxisAngle({ 0.0f, 0.0f, 1.0f }, Input.MouseDelta.X * DeltaTime * Pi / 180.0f);
		const FVector CurrentRight = Rotate(Current.Transform.Rotation, { 0.0f, 1.0f, 0.0f });
		const FQuat Pitch = AxisAngle(CurrentRight, -Input.MouseDelta.Y * DeltaTime * Pi / 180.0f);
		Result.Transform.Rotation = Normalize(Multiply(Pitch, Multiply(Yaw, Current.Transform.Rotation)));
	}
	return Result;
}

// 카메라를 향하는 직교기저를 만들고 크기를 반영해 Billboard 행렬을 만든다.
FMatrix ComputeBillboardTransform(const FVector& WorldPosition, const FVector2 Size, const FCameraTransform& ViewCamera)
{
	FVector Forward = Subtract(ViewCamera.Location, WorldPosition);
	if (LengthSquared(Forward) <= Epsilon * Epsilon)
	{
		Forward = Rotate(ViewCamera.Rotation, { -1.0f, 0.0f, 0.0f });
	}
	Forward = Normalize(Forward);
	FVector ReferenceUp{ 0.0f, 0.0f, 1.0f };
	if (fabsf(Dot(Forward, ReferenceUp)) > 0.999f)
	{
		ReferenceUp = { 0.0f, 1.0f, 0.0f };
	}
	const FVector Right = Normalize(Cross(ReferenceUp, Forward));
	const FVector Up = Normalize(Cross(Forward, Right));

	FMatrix Matrix = ZeroMatrix();
	Matrix.M[0][0] = Forward.X;
	Matrix.M[0][1] = Forward.Y;
	Matrix.M[0][2] = Forward.Z;
	Matrix.M[1][0] = Right.X * Size.X;
	Matrix.M[1][1] = Right.Y * Size.X;
	Matrix.M[1][2] = Right.Z * Size.X;
	Matrix.M[2][0] = Up.X * Size.Y;
	Matrix.M[2][1] = Up.Y * Size.Y;
	Matrix.M[2][2] = Up.Z * Size.Y;
	Matrix.M[3][0] = WorldPosition.X;
	Matrix.M[3][1] = WorldPosition.Y;
	Matrix.M[3][2] = WorldPosition.Z;
	Matrix.M[3][3] = 1.0f;
	return Matrix;
}

// quaternion으로 회전된 카메라 Forward의 Z 성분을 계산한다.
float CameraForwardZ(const FQuat& Rotation)
{
	return 2.0f * (Rotation.X * Rotation.Z - Rotation.W * Rotation.Y);
}

// 카메라 Forward의 XY 방향을 atan2로 바꿔 Euler Yaw를 구한다.
float CameraYawDegrees(const FQuat& Rotation)
{
	const FVector Forward = NormalizedOrZero(Rotation.GetForwardVector());
	return std::atan2(Forward.Y, Forward.X) * 180.0f / Pi;
}

// 카메라 Forward의 Z 성분을 asin해 Euler Pitch를 구한다.
float CameraPitchDegrees(const FQuat& Rotation)
{
	return std::asin(FMath::Clamp(CameraForwardZ(Rotation), -1.0f, 1.0f)) * 180.0f / Pi;
}

// Z-up Yaw와 Right축 Pitch를 합성해 Roll 없는 quaternion을 만든다.
FQuat MakeCameraRotation(float YawDegrees, float PitchDegrees)
{
	const float HalfYaw = YawDegrees * Pi / 360.0f;
	const float HalfPitch = PitchDegrees * Pi / 360.0f;
	const float SinYaw = std::sin(HalfYaw);
	const float CosYaw = std::cos(HalfYaw);
	const float SinPitch = std::sin(HalfPitch);
	const float CosPitch = std::cos(HalfPitch);

	return { SinPitch * SinYaw, -SinPitch * CosYaw, CosPitch * SinYaw, CosPitch * CosYaw };
}
}

// 유효한 새 영역에서만 저장 카메라의 직교 폭을 보정한다.
void FEditorViewportClient::SetViewRect(const FRect& Rect)
{
	ViewRect = Rect;
	if (IsViewRectValid(ViewRect))
	{
		ConstrainOrthographicWidth(Camera.Projection);
	}
}

// 외부 설정 적용 시 Roll을 제거하고 일반 View 종류로 갱신한다.
void FEditorViewportClient::SetCamera(const FViewCamera& InCamera)
{
	bFrameTransitionActive = false;
	Camera = InCamera;
	ConstrainOrthographicWidth(Camera.Projection);

	const bool bPerspective = Camera.Projection.Mode == EProjectionMode::Perspective;
	if (bPerspective)
	{
		Camera.Transform.Rotation = MakeCameraRotation(
			CameraYawDegrees(Camera.Transform.Rotation),
			CameraPitchDegrees(Camera.Transform.Rotation));
	}

	ViewportType = bPerspective
		? ELevelViewportType::Perspective
		: ELevelViewportType::OrthographicView;
}

// 위치나 투영 수치 편집으로 Top·Front 등의 View 종류가 풀리지 않게 한다.
void FEditorViewportClient::ApplyCameraProperties(const FViewCamera& InCamera, bool bRotationChanged)
{
	const ELevelViewportType PreviousType = ViewportType;
	const bool bKeepType = !bRotationChanged && Camera.Projection.Mode == InCamera.Projection.Mode;

	SetCamera(InCamera);
	if (bKeepType)
	{
		ViewportType = PreviousType;
	}
}

// 축 정렬 View는 고정 Transform을, 일반 View는 현재 위치를 유지해 적용한다.
void FEditorViewportClient::SetViewportType(ELevelViewportType InType)
{
	bFrameTransitionActive = false;
	ConstrainOrthographicWidth(Camera.Projection);
	ViewportType = InType;
	Camera.Projection.Mode = InType == ELevelViewportType::Perspective
		? EProjectionMode::Perspective
		: EProjectionMode::Orthographic;

	switch (InType)
	{
	case ELevelViewportType::Top:
		Camera.Transform = { { 0.0f, 0.0f, 10.0f }, { 0.0f, 0.70710678f, 0.0f, 0.70710678f } };
		break;
	case ELevelViewportType::Bottom:
		Camera.Transform = { { 0.0f, 0.0f, -10.0f }, { 0.0f, -0.70710678f, 0.0f, 0.70710678f } };
		break;
	case ELevelViewportType::Front:
		Camera.Transform = { { 10.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 0.0f } };
		break;
	case ELevelViewportType::Back:
		Camera.Transform = { { -10.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f } };
		break;
	case ELevelViewportType::Left:
		Camera.Transform = { { 0.0f, -10.0f, 0.0f }, { 0.0f, 0.0f, 0.70710678f, 0.70710678f } };
		break;
	case ELevelViewportType::Right:
		Camera.Transform = { { 0.0f, 10.0f, 0.0f }, { 0.0f, 0.0f, -0.70710678f, 0.70710678f } };
		break;
	case ELevelViewportType::Perspective:
		Camera.Transform.Rotation = MakeCameraRotation(
			CameraYawDegrees(Camera.Transform.Rotation),
			CameraPitchDegrees(Camera.Transform.Rotation));
		break;
	case ELevelViewportType::OrthographicView:
		// 투영 방식만 바꾸는 종류는 현재 Transform을 유지한다.
		break;
	}
}

// 상자의 꼭짓점 8개를 카메라 축에 투영해 상하좌우가 모두 화면 안에 드는 최소 거리(직교는 폭)를 구한다.
void FEditorViewportClient::FrameBounds(const FVector& Center, const FVector& HalfExtent)
{
	if (!HasValidViewRect())
	{
		return;
	}

	FViewCamera NewCamera = Camera;
	const FVector Forward = Rotate(Camera.Transform.Rotation, { 1.0f, 0.0f, 0.0f });
	const FVector Right = Rotate(Camera.Transform.Rotation, { 0.0f, 1.0f, 0.0f });
	const FVector Up = Rotate(Camera.Transform.Rotation, { 0.0f, 0.0f, 1.0f });
	const float AspectRatio = ViewRect.Width / ViewRect.Height;
	const bool bOrthographic = Camera.Projection.Mode == EProjectionMode::Orthographic;

	// 원근 FOV는 세로 기준이므로 가로 범위는 종횡비를 곱해 얻는다.
	const float TanHalfVertical = bOrthographic ? 1.0f : std::tan(Camera.Projection.FovDegrees * Pi / 360.0f);
	const float TanHalfHorizontal = TanHalfVertical * AspectRatio;

	float MaxRight = 0.0f;
	float MaxUp = 0.0f;
	float MaxDepth = 0.0f;
	float Distance = 0.0f;
	for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
	{
		const FVector Corner{
			(CornerIndex & 1) ? HalfExtent.X : -HalfExtent.X,
			(CornerIndex & 2) ? HalfExtent.Y : -HalfExtent.Y,
			(CornerIndex & 4) ? HalfExtent.Z : -HalfExtent.Z
		};
		const float CornerRight = std::fabs(Dot(Corner, Right)) * FrameBoundsPadding;
		const float CornerUp = std::fabs(Dot(Corner, Up)) * FrameBoundsPadding;
		const float CornerDepth = Dot(Corner, Forward);

		MaxRight = std::max(MaxRight, CornerRight);
		MaxUp = std::max(MaxUp, CornerUp);
		MaxDepth = std::max(MaxDepth, std::fabs(CornerDepth));

		// 카메라 깊이는 Distance + CornerDepth이고, 그 깊이에서 화면 절반 폭이 꼭짓점을 덮어야 한다.
		if (!bOrthographic)
		{
			Distance = std::max(Distance, std::max(CornerRight / TanHalfHorizontal, CornerUp / TanHalfVertical) - CornerDepth);
		}
	}

	if (bOrthographic)
	{
		// BuildRenderCamera가 논리 위치 앞뒤로 깊이를 확보하므로 위치는 상자 중심이면 충분하다.
		NewCamera.Transform.Location = Center;
		NewCamera.Projection.OrthoWidth = std::max(MaxRight * 2.0f, MaxUp * 2.0f * AspectRatio);
	}
	else
	{
		// 가장 가까운 면이 Near에 잘리지 않고 가장 먼 면이 Far에 잘리지 않게 한다.
		Distance = std::max(Distance, MaxDepth + Camera.Projection.NearClip);
		NewCamera.Transform.Location = Subtract(Center, Scale(Forward, Distance));
		NewCamera.Projection.FarClip = std::max(Camera.Projection.FarClip, (Distance + MaxDepth) * FrameBoundsPadding);
	}

	ConstrainOrthographicWidth(NewCamera.Projection);

	// 이동 도중에도 물체가 잘리지 않게 FarClip은 바로 넓힌다.
	Camera.Projection.FarClip = NewCamera.Projection.FarClip;
	FrameTransitionStart = Camera;
	FrameTransitionTarget = NewCamera;
	FrameTransitionElapsed = 0.0f;
	bFrameTransitionActive = true;
}

void FEditorViewportClient::TickFrameTransition(const float DeltaTime)
{
	if (!bFrameTransitionActive)
	{
		return;
	}

	FrameTransitionElapsed += DeltaTime;
	const float Alpha = std::min(FrameTransitionElapsed / FrameTransitionDuration, 1.0f);
	const float Smooth = Alpha * Alpha * (3.0f - 2.0f * Alpha);

	const FVector StartLocation = FrameTransitionStart.Transform.Location;
	const FVector TargetLocation = FrameTransitionTarget.Transform.Location;
	Camera.Transform.Location = Add(StartLocation, Scale(Subtract(TargetLocation, StartLocation), Smooth));
	if (Camera.Projection.Mode == EProjectionMode::Orthographic)
	{
		// 폭은 비율로 보간해야 배율이 크게 달라도 줌 속도가 일정해 보인다.
		const float StartWidth = FrameTransitionStart.Projection.OrthoWidth;
		const float TargetWidth = FrameTransitionTarget.Projection.OrthoWidth;
		Camera.Projection.OrthoWidth = StartWidth * std::pow(TargetWidth / StartWidth, Smooth);
	}

	if (Alpha >= 1.0f)
	{
		Camera.Transform.Location = TargetLocation;
		Camera.Projection.OrthoWidth = FrameTransitionTarget.Projection.OrthoWidth;
		bFrameTransitionActive = false;
	}
}

// Forward → Yaw/Pitch 계산과 직교 폭 보정을 Override에만 적용한다.
void FEditorViewportClient::SetCameraOverride(const FVector& Location, const FVector& Forward, const FCameraProjection& Projection, const FQuat* Rotation)
{
	const FVector Direction{ NormalizedOrZero(Forward) };
	CameraOverride.Transform.Location = Location;
	CameraOverride.Transform.Rotation = Rotation ? *Rotation : MakeCameraRotation(std::atan2(Direction.Y, Direction.X) * 180.0f / Pi, std::asin(FMath::Clamp(Direction.Z, -1.0f, 1.0f)) * 180.0f / Pi);
	CameraOverride.Projection = Projection;
	CameraOverride.Projection.mUseCameraClipPlanes = Rotation != nullptr;
	if (!Rotation)
	{
		ConstrainOrthographicWidth(CameraOverride.Projection);
	}
	bHasCameraOverride = true;
}

// 축 정렬 View는 고정 평면을, 일반 직교 View는 실제 카메라 방향을 사용한다.
EGridPlane FEditorViewportClient::GetGridPlane() const
{
	switch (ViewportType)
	{
	case ELevelViewportType::Top:
	case ELevelViewportType::Bottom:
	case ELevelViewportType::Perspective:
		return EGridPlane::XY;
	case ELevelViewportType::Front:
	case ELevelViewportType::Back:
		return EGridPlane::YZ;
	case ELevelViewportType::Left:
	case ELevelViewportType::Right:
		return EGridPlane::XZ;
	case ELevelViewportType::OrthographicView:
		break;
	}

	const FVector Forward = GetEffectiveCamera().Transform.Rotation.GetForwardVector();
	const float AbsX = std::fabs(Forward.X);
	const float AbsY = std::fabs(Forward.Y);
	const float AbsZ = std::fabs(Forward.Z);
	if (AbsZ >= AbsX && AbsZ >= AbsY)
	{
		return EGridPlane::XY;
	}
	if (AbsY >= AbsX)
	{
		return EGridPlane::XZ;
	}
	return EGridPlane::YZ;
}

// 투영 방식에 맞는 이동축을 만들고 카메라별 계산으로 분기한다.
void FEditorViewportClient::UpdateCamera(float DeltaTime, const FCameraMoveInput& RawInput,
	float MoveSpeed, float MouseSensitivity, bool bCameraCaptured)
{
	// 사용자가 카메라를 잡거나 휠을 굴리면 프레이밍 이동을 그 자리에서 멈춘다.
	if (bCameraCaptured || RawInput.ZoomDelta != 0.0f)
	{
		bFrameTransitionActive = false;
	}

	// 게임 카메라 Override 중에는 저장된 편집 카메라를 입력으로 움직이지 않는다.
	if (bHasCameraOverride)
	{
		return;
	}

	const bool bOrthographic = Camera.Projection.Mode == EProjectionMode::Orthographic;
	// 직교 이동은 W/S와 A/D를 화면 평면에 매핑하고 Q/E는 정규화 전에 제외한다.
	const FVector Axis = bOrthographic
		? FVector(0.0f, RawInput.MoveAxis.Y, RawInput.MoveAxis.X)
		: RawInput.MoveAxis;

	// 이동 적분에는 이동 속도만 전달한다. MouseDelta는 0으로 두어 회전 중복을 막는다.
	FCameraMoveInput MoveInput{};
	MoveInput.MoveAxis = NormalizedOrZero(Axis) * MoveSpeed;

	if (bOrthographic)
	{
		UpdateOrthographicCamera(DeltaTime, MoveInput, RawInput.MouseDelta, RawInput.ZoomDelta, bCameraCaptured);
	}
	else
	{
		MoveInput.ZoomDelta = RawInput.ZoomDelta * PerspectiveWheelSpeed;
		UpdatePerspectiveCamera(DeltaTime, MoveInput, RawInput.MouseDelta, MouseSensitivity, bCameraCaptured);
	}
}

// 이전 회전으로 이동한 뒤 마우스 회전을 적용한다.
void FEditorViewportClient::UpdatePerspectiveCamera(
	float DeltaTime, const FCameraMoveInput& MoveInput, FVector2 MouseDelta,
	float MouseSensitivity, bool bCameraCaptured)
{
	Camera = ApplyCameraMovement(Camera, MoveInput, DeltaTime);
	if (!bCameraCaptured)
	{
		return;
	}

	// 마우스 회전은 픽셀 기준이며 DeltaTime을 곱하지 않는다.
	const float YawDelta = FMath::Clamp(
		MouseDelta.X * MouseSensitivity,
		-MaximumRotationDegreesPerFrame, MaximumRotationDegreesPerFrame);
	const float PitchDelta = FMath::Clamp(
		-MouseDelta.Y * MouseSensitivity,
		-MaximumRotationDegreesPerFrame, MaximumRotationDegreesPerFrame);

	const float Yaw = CameraYawDegrees(Camera.Transform.Rotation) + YawDelta;
	const float Pitch = FMath::Clamp(
		CameraPitchDegrees(Camera.Transform.Rotation) + PitchDelta,
		-MaximumCameraPitchDegrees, MaximumCameraPitchDegrees);

	Camera.Transform.Rotation = MakeCameraRotation(Yaw, Pitch);
}

// 직교 카메라는 회전 없이 화면 평면 이동과 폭 줌을 적용한다.
void FEditorViewportClient::UpdateOrthographicCamera(
	float DeltaTime, const FCameraMoveInput& MoveInput, FVector2 MouseDelta,
	float WheelDelta, bool bCameraCaptured)
{
	// 제한된 줌 폭을 이번 프레임의 Pan 거리 계산에도 사용한다.
	const float WheelSteps = WheelDelta / MouseWheelDeltaPerStep;
	Camera.Projection.OrthoWidth = std::max(
		MinimumOrthographicWidth,
		Camera.Projection.OrthoWidth * std::pow(OrthographicZoomFactorPerStep, WheelSteps));
	ConstrainOrthographicWidth(Camera.Projection);

	Camera = ApplyCameraMovement(Camera, MoveInput, DeltaTime);
	if (!bCameraCaptured)
	{
		return;
	}

	// 픽셀 드래그는 시간과 무관하게 현재 화면 배율로 월드 이동량을 계산한다.
	const float WorldUnitsPerPixel = Camera.Projection.OrthoWidth / ViewRect.Width;
	const FVector Right = NormalizedOrZero(Camera.Transform.Rotation.GetRightVector());
	const FVector Up = NormalizedOrZero(Camera.Transform.Rotation.GetUpVector());
	Camera.Transform.Location += Right * (-MouseDelta.X * WorldUnitsPerPixel) + Up * (MouseDelta.Y * WorldUnitsPerPixel);
}

// 직교 화면 구도를 유지하면서 카메라 평면 앞뒤를 렌더링할 깊이를 확보한다.
FViewCamera FEditorViewportClient::BuildRenderCamera() const
{
	FViewCamera RenderCamera = GetEffectiveCamera();
	if (RenderCamera.Projection.mUseCameraClipPlanes || RenderCamera.Projection.Mode != EProjectionMode::Orthographic)
	{
		return RenderCamera;
	}

	const float AspectRatio = IsViewRectValid(ViewRect) ? ViewRect.Width / ViewRect.Height : 1.0f;
	const float HalfWidth = RenderCamera.Projection.OrthoWidth * 0.5f;
	const float HalfHeight = HalfWidth / AspectRatio;
	const FVector Position = RenderCamera.Transform.Location;
	const float Distance = std::sqrt(
		Position.X * Position.X + Position.Y * Position.Y + Position.Z * Position.Z);

	// 기존 FarClip보다 깊이 범위를 줄이지 않는다.
	const float Radius = std::max(RenderCamera.Projection.FarClip,
		2.0f * Distance + 4.0f * std::sqrt(HalfWidth * HalfWidth + HalfHeight * HalfHeight));
	const FVector Forward = NormalizedOrZero(RenderCamera.Transform.Rotation.GetForwardVector());
	const float Retreat = Radius + RenderCamera.Projection.NearClip;

	RenderCamera.Transform.Location = Position - Forward * Retreat;
	RenderCamera.Projection.FarClip = RenderCamera.Projection.NearClip + 2.0f * Radius;
	return RenderCamera;
}

// 렌더 보정 카메라로 View·Projection 행렬을 만든다.
void FEditorViewportClient::CalcViewMatrices(FMatrix& OutView, FMatrix& OutProjection) const
{
	const FViewCamera RenderCamera = BuildRenderCamera();
	OutView = BuildViewMatrix(RenderCamera.Transform);
	OutProjection = BuildProjectionMatrix(RenderCamera.Projection, ViewRect.Width / ViewRect.Height);
}

// row-vector 순서로 VP를 만들고 요청된 경우에만 절두체를 추출한다.
FMatrix FEditorViewportClient::CalcViewProjection(FFrustumPlanes* OutFrustum) const
{
	FMatrix View;
	FMatrix Projection;
	CalcViewMatrices(View, Projection);
	const FMatrix ViewProjection = View * Projection;

	if (OutFrustum)
	{
		*OutFrustum = ExtractFrustumPlanes(ViewProjection);
	}
	return ViewProjection;
}

FLODViewContext FEditorViewportClient::CalcLODViewContext() const
{
	const FViewCamera& ViewCamera = GetEffectiveCamera();
	FMatrix View;
	FMatrix Projection;
	CalcViewMatrices(View, Projection);
	FLODViewContext Context{
		static_cast<uint32>(ViewRect.Width),
		static_cast<uint32>(ViewRect.Height)
	};

	Context.CameraPosition = ViewCamera.Transform.Location;
	Context.CameraForward = NormalizedOrZero(ViewCamera.Transform.Rotation.GetForwardVector());
	Context.ViewProjection = View * Projection;

	const float ScaleX = Projection.M[1][0];
	const float ScaleY = Projection.M[2][1];
	Context.ProjectionScaleSquared = std::max(ScaleX * ScaleX, ScaleY * ScaleY);
	Context.NearZ = ViewCamera.Projection.NearClip;
	Context.bOrthographic = ViewCamera.Projection.Mode == EProjectionMode::Orthographic;
	Context.Prepare();

	return Context;
}

// 패널 PIE와 별도 PIE 모두 Override된 게임 카메라의 클립 범위를 쓴다.
void FEditorViewportClient::CalcSceneView(FSceneView& OutView) const
{
	const FViewCamera& ViewCamera = GetEffectiveCamera();
	CalcViewMatrices(OutView.ViewMatrix, OutView.ProjectionMatrix);
	OutView.ViewProjection = OutView.ViewMatrix * OutView.ProjectionMatrix;
	OutView.Location = GetViewLocation();
	OutView.Forward = GetViewForward();
	OutView.NearZ = ViewCamera.Projection.NearClip;
	OutView.FarZ = ViewCamera.Projection.FarClip;
	OutView.bOrthographic = IsOrthographic();
	OutView.LODView = CalcLODViewContext();
	OutView.AppendFrustumCorners = &AppendSceneViewFrustumCorners;
	OutView.FrustumContext = this;
	OutView.ViewMode = ViewMode;
	OutView.bWireframe = IsWireframe();
	OutView.bVisualizeShadowCascades = ShowFlags.IsEnabled(EEngineShowFlag::ShadowCascades);
}

FVector FEditorViewportClient::GetViewForward() const
{
	return NormalizedOrZero(GetEffectiveCamera().Transform.Rotation.GetForwardVector());
}

FVector FEditorViewportClient::GetViewUp() const
{
	return NormalizedOrZero(GetEffectiveCamera().Transform.Rotation.GetUpVector());
}

bool FEditorViewportClient::AppendSceneViewFrustumCorners(const void* Context, float Near, float Far, TArray<FVector>& OutCorners)
{
	return static_cast<const FEditorViewportClient*>(Context)->AppendShadowFrustumCorners(Near, Far, OutCorners);
}

// 실제 렌더 VP에서 깊이 구간을 정하고 역투영해 View별 Receiver 범위를 구한다.
bool FEditorViewportClient::AppendShadowFrustumCorners(float NearDistance, float FarDistance, TArray<FVector>& OutCorners) const
{
	if (!IsViewRectValid(ViewRect))
	{
		return false;
	}
	const FViewCamera& ViewCamera = GetEffectiveCamera();

	if (IsOrthographic())
	{
		// 직교 View는 논리 원점 앞뒤의 단일 범위를 쓴다.
		if (FarDistance <= 0.0f)
		{
			return false;
		}
		NearDistance = -FarDistance;
	}
	else
	{
		// 원근 구간이 실제 카메라 Frustum을 벗어나지 않도록 제한한다.
		NearDistance = FMath::Max(NearDistance, ViewCamera.Projection.NearClip);
		FarDistance = FMath::Min(FarDistance, ViewCamera.Projection.FarClip);
	}

	if (FarDistance <= NearDistance)
	{
		return false;
	}

	// 직교 렌더 카메라의 후퇴 보정까지 포함해 화면 렌더링과 같은 행렬을 사용한다.
	const FMatrix ViewProjection = CalcViewProjection();

	// 거리의 기준은 후퇴 보정 전의 논리 카메라다.
	const FVector Origin = ViewCamera.Transform.Location;
	const FVector Forward = GetViewForward();

	// 선형 전방 거리를 현재 렌더 VP의 NDC 깊이로 변환한다.
	const auto ProjectDepth = [&ViewProjection](const FVector& WorldPosition)
	{
		const FVector4 ClipPosition = FVector4(WorldPosition, 1.0f) * ViewProjection;
		return FMath::Clamp(ClipPosition.Z / ClipPosition.W, 0.0f, 1.0f);
	};

	const float NearDepth = ProjectDepth(Origin + Forward * NearDistance);
	const float FarDepth = ProjectDepth(Origin + Forward * FarDistance);
	if (FarDepth <= NearDepth)
	{
		return false;
	}

	// 각 깊이 평면의 화면 네 모서리를 월드 공간으로 복원한다.
	const FMatrix InverseViewProjection = ViewProjection.Inverse();
	OutCorners.Reserve(OutCorners.Num() + 8);

	for (int32 DepthIndex = 0; DepthIndex < 2; ++DepthIndex)
	{
		const float Depth = DepthIndex == 0 ? NearDepth : FarDepth;
		for (int32 Y = -1; Y <= 1; Y += 2)
		{
			for (int32 X = -1; X <= 1; X += 2)
			{
				// X·Y는 NDC 화면 좌표이며, 깊이는 투영된 Z를 사용한다.
				FVector4 WorldPosition = FVector4(static_cast<float>(X), static_cast<float>(Y), Depth, 1.0f) * InverseViewProjection;

				// 역투영 결과의 동차 좌표를 월드 위치로 변환한다.
				WorldPosition /= WorldPosition.W;

				OutCorners.Add(FVector(WorldPosition.X, WorldPosition.Y, WorldPosition.Z));
			}
		}
	}

	return true;
}

// 렌더링과 같은 깊이 보정을 적용해 직교 화면 뒤쪽 대상도 피킹한다.
FRay FEditorViewportClient::DeprojectScreen(FVector2 ViewLocalPosition) const
{
	return Deproject(BuildRenderCamera(), ViewLocalPosition, { ViewRect.Width, ViewRect.Height });
}

// 직교는 고정 화면 기저로, 원근은 카메라를 향하는 기저로 Billboard 행렬을 만든다.
FMatrix FEditorViewportClient::BuildBillboardMatrix(const FVector& WorldPosition, float Width, float Height) const
{
	const FViewCamera& ViewCamera = GetEffectiveCamera();
	if (ViewCamera.Projection.Mode == EProjectionMode::Orthographic)
	{
		const FVector Facing = NormalizedOrZero(ViewCamera.Transform.Rotation.GetForwardVector()) * -1.0f;
		const FVector Right = NormalizedOrZero(ViewCamera.Transform.Rotation.GetRightVector());
		const FVector Up = NormalizedOrZero(ViewCamera.Transform.Rotation.GetUpVector());

		FMatrix Matrix;
		Matrix.SetIdentity();
		Matrix.M[0][0] = Facing.X;
		Matrix.M[0][1] = Facing.Y;
		Matrix.M[0][2] = Facing.Z;
		Matrix.M[1][0] = Right.X * Width;
		Matrix.M[1][1] = Right.Y * Width;
		Matrix.M[1][2] = Right.Z * Width;
		Matrix.M[2][0] = Up.X * Height;
		Matrix.M[2][1] = Up.Y * Height;
		Matrix.M[2][2] = Up.Z * Height;
		Matrix.M[3][0] = WorldPosition.X;
		Matrix.M[3][1] = WorldPosition.Y;
		Matrix.M[3][2] = WorldPosition.Z;
		return Matrix;
	}

	FMatrix Matrix = ComputeBillboardTransform(WorldPosition, { Width, Height }, ViewCamera.Transform);
	// ParticleQuad의 기존 와인딩에 맞춰 오른쪽 축만 뒤집는다.
	Matrix.M[1][0] = -Matrix.M[1][0];
	Matrix.M[1][1] = -Matrix.M[1][1];
	Matrix.M[1][2] = -Matrix.M[1][2];
	return Matrix;
}

// 종횡비와 절두체 정밀도에 맞춰 직교 투영 폭의 허용 범위를 유지한다.
void FEditorViewportClient::ConstrainOrthographicWidth(FCameraProjection& Projection) const
{
	const float AspectRatio = IsViewRectValid(ViewRect) ? ViewRect.Width / ViewRect.Height : 1.0f;
	const float MaximumWidth = std::max(MinimumOrthographicWidth,
		MaximumOrthographicSpan * std::min(1.0f, AspectRatio));
	Projection.OrthoWidth = std::isnan(Projection.OrthoWidth)
		? 10.0f
		: FMath::Clamp(Projection.OrthoWidth, MinimumOrthographicWidth, MaximumWidth);
}
