#pragma once

#include "Editor/Viewports/EditorViewportTypes.h"
#include "Collision/Ray.h"
#include "Containers/Array.h"
#include "Math/Frustum.h"
#include "Math/Matrix.h"
#include "Render/ViewMode.h"
#include "Render/EngineShowFlags.h"

struct FLODViewContext;
struct FSceneView;

// 한 View의 화면 영역·카메라·표시 상태를 소유하고 카메라 조작과 렌더용 View 계산을 맡는다.
// 패널 View는 FViewportsPanel이, 별도 PIE 창 View는 UEditorEngine이 소유한다.
class FEditorViewportClient
{
public:
	// 레이아웃이나 창 크기가 정한 렌더 영역이다. 직교 폭은 새 종횡비에 맞춰 보정한다.
	void SetViewRect(const FRect& Rect);
	const FRect& GetViewRect() const { return ViewRect; }
	bool HasValidViewRect() const { return IsViewRectValid(ViewRect); }

	// 외부 카메라를 적용하고 투영 방식에 맞는 일반 View 종류를 선택한다.
	void SetCamera(const FViewCamera& InCamera);
	const FViewCamera& GetCamera() const { return Camera; }

	// 위치·투영 수치만 변경하면 기존 축 정렬 View 종류를 유지한다.
	void ApplyCameraProperties(const FViewCamera& InCamera, bool bRotationChanged);

	// 선택한 View 종류의 방향과 투영 방식을 적용한다.
	void SetViewportType(ELevelViewportType InType);
	ELevelViewportType GetViewportType() const { return ViewportType; }

	// 현재 방향과 View 종류를 유지한 채 월드 축 정렬 상자 전체가 화면에 들어오는 곳으로 카메라 이동을 시작한다.
	void FrameBounds(const FVector& Center, const FVector& HalfExtent);
	// FrameBounds가 시작한 이동을 진행한다. 프레임마다 한 번 부른다.
	void TickFrameTransition(float DeltaTime);

	// 저장 카메라와 View 종류를 보존하고 실제 View에 적용할 카메라만 교체한다.
	void SetCameraOverride(const FVector& Location, const FVector& Forward, const FCameraProjection& Projection, const FQuat* Rotation = nullptr);
	void ClearCameraOverride() { bHasCameraOverride = false; }
	bool HasCameraOverride() const { return bHasCameraOverride; }

	// 렌더·컬링·피킹은 Override가 반영된 논리 카메라를 사용한다.
	const FViewCamera& GetEffectiveCamera() const
	{
		return bHasCameraOverride ? CameraOverride : Camera;
	}

	bool IsWireframe() const { return ViewMode == EViewMode::Wireframe; }
	const FEngineShowFlags& GetShowFlags() const { return ShowFlags; }
	void SetShowFlags(const FEngineShowFlags& InFlags) { ShowFlags = InFlags; }
	void SetViewMode(EViewMode Value) { ViewMode = Value; }
	EViewMode GetViewMode() const { return ViewMode; }

	// 축 정렬 View 종류와 실제 카메라 방향으로 Grid 평면을 선택한다.
	EGridPlane GetGridPlane() const;

	// RawInput은 키 축·마우스 픽셀·원시 휠 값이며 기존 View 종류를 유지해 카메라를 갱신한다.
	void UpdateCamera(float DeltaTime, const FCameraMoveInput& RawInput,
		float MoveSpeed, float MouseSensitivity, bool bCameraCaptured);

	// 아래 계산은 Override가 반영된 카메라와 현재 렌더 영역을 사용한다. 렌더 영역이 유효해야 한다.
	void CalcViewMatrices(FMatrix& OutView, FMatrix& OutProjection) const;
	// 같은 VP로 Frustum도 필요할 때 함께 계산한다.
	FMatrix CalcViewProjection(FFrustumPlanes* OutFrustum = nullptr) const;
	// 공통 LOD·컬링 입력을 구성한다.
	FLODViewContext CalcLODViewContext() const;
	// 카메라 행렬·LOD·그림자 수신 범위·표시 설정을 FSceneView에 채운다. 렌더 대상과 공통 표시 설정은 호출자가 채운다.
	void CalcSceneView(FSceneView& OutView) const;

	FVector GetViewLocation() const { return GetEffectiveCamera().Transform.Location; }
	FVector GetViewForward() const;
	// 화면 위쪽 방향이다.
	FVector GetViewUp() const;
	bool IsOrthographic() const { return GetEffectiveCamera().Projection.Mode == EProjectionMode::Orthographic; }

	// 원근은 [Near, Far] 구간, 직교는 논리 원점 앞뒤 [-Far, Far]의 Receiver 꼭짓점 8개를 기존 목록에 추가한다.
	// 유효한 깊이 구간이 없으면 목록을 변경하지 않는다.
	bool AppendShadowFrustumCorners(float NearDistance, float FarDistance, TArray<FVector>& OutCorners) const;

	// View 원점 기준 화면 좌표를 렌더 카메라 기준으로 역투영한다.
	FRay DeprojectScreen(FVector2 ViewLocalPosition) const;

	// 렌더·피킹에 공통인 Billboard 행렬을 계산한다.
	FMatrix BuildBillboardMatrix(const FVector& WorldPosition, float Width, float Height) const;

private:
	// FSceneView::AppendFrustumCorners에 넘긴다. Context는 이 Client다.
	static bool AppendSceneViewFrustumCorners(const void* Context, float Near, float Far, TArray<FVector>& OutCorners);

	// 논리 카메라는 보존하고 직교 렌더링에 필요한 깊이만 보정한다.
	FViewCamera BuildRenderCamera() const;

	// 투영 방식에 따라 원근 이동·회전 또는 직교 이동·줌을 계산한다.
	void UpdatePerspectiveCamera(float DeltaTime, const FCameraMoveInput& MoveInput,
		FVector2 MouseDelta, float MouseSensitivity, bool bCameraCaptured);
	void UpdateOrthographicCamera(float DeltaTime, const FCameraMoveInput& MoveInput,
		FVector2 MouseDelta, float WheelDelta, bool bCameraCaptured);

	void ConstrainOrthographicWidth(FCameraProjection& Projection) const;

	FRect ViewRect{};
	FViewCamera Camera{};
	ELevelViewportType ViewportType = ELevelViewportType::Perspective;

	// Override 설정·해제는 저장 Camera와 ViewportType을 변경하지 않는다.
	FViewCamera CameraOverride{};
	bool bHasCameraOverride = false;

	// 위치와 직교 폭만 보간한다. 카메라를 외부에서 바꾸거나 사용자가 조작하면 멈춘다.
	FViewCamera FrameTransitionStart{};
	FViewCamera FrameTransitionTarget{};
	float FrameTransitionElapsed = 0.0f;
	bool bFrameTransitionActive = false;

	// 모든 Client가 같은 표시 기본값으로 시작한다.
	FEngineShowFlags ShowFlags{ EShowFlagScope::View };
	EViewMode ViewMode = EViewMode::Lit;
};
