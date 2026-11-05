#pragma once

#include "Collision/Ray.h"
#include "Math/Transform.h"

enum class EGizmoMode
{
	Location,
	Rotation,
	Scale,
};

enum class EGizmoSpace
{
	Local,
	World,
};

// 드래그 결과. 모두 드래그 시작 값 기준이다
struct FGizmoDragResult
{
	FVector Location;		// 새 위치 (Location 모드)
	FRotator Rotation;		// 새 회전 (Rotation 모드)
	FVector Scale;			// 새 스케일 (Scale 모드)
	float ScaleFactor = 1.0f;	// 시작 대비 배율. 라이트 반지름처럼 스케일이 아닌 값에 쓴다
};

// 상태 + 로직. 대상을 모르고, 위치를 받아 드래그 결과만 내보낸다 (언리얼 FWidget처럼)
// 결과 적용은 기즈모를 쓰는 에디터가 한다 (레벨: 컴포넌트, 스켈레탈 메시 뷰어: 본)
class FGizmo
{
public:
	// 기즈모를 놓을 위치, 축 방향, 스케일. 에디터가 매 프레임 넣어준다
	void SetPivot(const FVector& InLocation, const FRotator& InRotation, const FVector& InScale)
	{
		PivotLocation = InLocation;
		PivotRotation = InRotation;
		PivotScale = InScale;
		bHasPivot = true;
	}
	// 대상이 없을 때. 그리지 않고 입력도 받지 않는다
	void ClearPivot() { bHasPivot = false; }
	bool HasPivot() const { return bHasPivot; }

	// 이번 Update에서 드래그로 값이 바뀌었으면 true. 에디터가 GetDragResult를 대상에 적용한다
	bool HasDragResult() const { return bHasDragResult; }
	const FGizmoDragResult& GetDragResult() const { return DragResult; }
	// 드래그 시작 때 받은 스케일. ScaleFactor와 곱해 스케일이 아닌 값(라이트 반지름)을 구할 때 쓴다
	const FVector& GetDragStartScale() const { return DragStartScale; }

	// Gizmo 모드(이동, 회전, 크기)
	void SetMode(EGizmoMode InMode) { Mode = InMode; }
	EGizmoMode GetMode() const { return Mode; }

	void SetSpace(EGizmoSpace InSpace) { Space = InSpace; }
	EGizmoSpace GetSpace() const { return Space; }

	// Active View의 Ray와 행렬만 사용해 Hover·Drag 상태를 갱신한다.
	void Update(const FRay& MouseRay, const FVector2& MousePos,
		const FMatrix& ViewProj, int ScreenW, int ScreenH,
		bool bMouseDown, const FVector& CameraLocation, bool bCameraOrthographic);

	bool IsUsing() const { return DraggingAxis >= 0; }
	int GetHoveredAxis() const { return HoveredAxis; }

	int PickAxis(const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH);

	int PickLinearAxis(const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH);
	int PickRotationAxis(const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH);


	void BeginDrag(int Axis, const FRay& MouseRay, const FVector2& MousePos);
	void UpdateDrag(const FRay& MouseRay, const FVector2& MousePos);
	void EndDrag();

	float ComputeAngleOnPlane(const FVector& Point, int Axis) const;

	inline FVector GetLocation() const { return PivotLocation; }
	inline FRotator GetRotation() const { return PivotRotation; }
	inline FVector GetScale() const { return PivotScale; }

	FVector GetRenderLocation() const;
	// 원근 View에서만 카메라 거리에 맞춰 크기 보정 위치를 구하고 직교 View는 월드 위치를 유지한다.
	FVector GetRenderLocationForView(const FVector& CameraLocation, bool bCameraOrthographic) const;
	// 직교 View는 줌과 상관없이 화면 크기가 같도록 View 높이에 비례한 배율을 구한다. 원근은 위치로 보정하므로 1이다.
	float GetRenderScaleForView(const FMatrix& ViewProj, bool bCameraOrthographic) const;
	FVector GetCameraLocation() const;
	FVector GetAxisDirection(int Axis) const;

private:
	EGizmoMode Mode = EGizmoMode::Location;
	EGizmoSpace Space = EGizmoSpace::Local;
	FVector PivotLocation{};
	FRotator PivotRotation{};
	FVector PivotScale{ 1.0f, 1.0f, 1.0f };
	bool bHasPivot = false;

	FGizmoDragResult DragResult;
	bool bHasDragResult = false;

	int HoveredAxis = -1;
	int DraggingAxis = -1;

	FVector2 DragStartMousePos;
	FVector DragStartPoint;
	FVector DragStartLocation;
	FRotator DragStartRotation;
	FVector DragStartScale;
	FVector DragAxisDirection;
	FVector DragStartRenderLocation;

	FVector DragPlaneNormal;

	FVector ViewCameraLocation{};
	bool bViewCameraOrthographic = false;
	float ViewRenderScale = 1.0f;

	float DragStartAngle;
	float RingRadius = 1.0f;
};
