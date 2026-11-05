#pragma once

#include "KeyCode.h"
#include "MouseButton.h"
#include "Core/Types.h"

class FInputSystem
{
private:
	FInputSystem() = delete;

public:
	static bool IsKeyPressed(EKeyCode KeyCode);
	static bool IsKeyReleased(EKeyCode KeyCode);
	static bool IsKeyDown(EKeyCode KeyCode);

	static bool IsMouseDown(EMouseButton MouseButton);
	static bool IsMousePressed(EMouseButton MouseButton);
	static bool IsMouseReleased(EMouseButton MouseButton);

	static void UpdateInputStates();
	static void ClearAllStates();
	static void SyncMousePosition(int32 X, int32 Y);
	static void OnKeyUp(int VirtualKey);
	static void OnMouseUp(EMouseButton MouseButton);
	static void OnKeyDown(int VirtualKey);
	static void OnMouseDown(EMouseButton MouseButton);
	static void OnMouseMove(int32 X, int32 Y);
	static void OnRawMouseMove(int32 X, int32 Y);
	static void SetRelativeMouseMode(bool Enabled);
	static void OnMouseWheelDelta(int32 Wheel);

	static int32 GetMouseX();
	static int32 GetMouseY();

	static int32 GetMouseDeltaX();
	static int32 GetMouseDeltaY();

	static int32 GetWheelDelta();

private:
	static bool mMouseStates[5];
	static bool mPreviousMouseStates[5];
	static bool mKeyStates[256];
	static bool mPreviousKeyStates[256];
	static int32 mMouseX;
	static int32 mMouseY;
	static int32 mPreviousMouseX;
	static int32 mPreviousMouseY;
	static int32 mDeltaX;
	static int32 mDeltaY;
	static int32 mWheelDelta;
	static bool mRelativeMouseMode;
};
