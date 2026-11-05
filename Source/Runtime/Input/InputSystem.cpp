#include "EnginePCH.h"
#include "InputSystem.h"

bool FInputSystem::mMouseStates[5]{};
bool FInputSystem::mPreviousMouseStates[5]{};
bool FInputSystem::mKeyStates[256]{};
bool FInputSystem::mPreviousKeyStates[256]{};
int32 FInputSystem::mMouseX{};
int32 FInputSystem::mMouseY{};
int32 FInputSystem::mPreviousMouseX{};
int32 FInputSystem::mPreviousMouseY{};
int32 FInputSystem::mDeltaX{};
int32 FInputSystem::mDeltaY{};
int32 FInputSystem::mWheelDelta{};
bool FInputSystem::mRelativeMouseMode{};

bool FInputSystem::IsMouseDown(EMouseButton MouseCode)
{
	return mMouseStates[static_cast<int>(MouseCode)];
}

bool FInputSystem::IsMousePressed(EMouseButton MouseCode)
{
	const unsigned int Index{ static_cast<unsigned int>(MouseCode) };
	return mMouseStates[Index] && !mPreviousMouseStates[Index];
}

bool FInputSystem::IsMouseReleased(EMouseButton MouseCode)
{
	const unsigned int Index{ static_cast<unsigned int>(MouseCode) };
	return !mMouseStates[Index] && mPreviousMouseStates[Index];
}

bool FInputSystem::IsKeyDown(EKeyCode KeyCode)
{
	return mKeyStates[static_cast<int>(KeyCode)];
}

bool FInputSystem::IsKeyPressed(EKeyCode KeyCode)
{
	const unsigned int Index{ static_cast<unsigned int>(KeyCode) };
	return mKeyStates[Index] && !mPreviousKeyStates[Index];
}

bool FInputSystem::IsKeyReleased(EKeyCode KeyCode)
{
	const unsigned int Index{ static_cast<unsigned int>(KeyCode) };
	return !mKeyStates[Index] && mPreviousKeyStates[Index];
}

void FInputSystem::OnKeyUp(int VirtualKey)
{
	if (VirtualKey < 256)
	{
		mKeyStates[VirtualKey] = false;
	}
}

void FInputSystem::OnMouseUp(EMouseButton MouseButton)
{
	const unsigned int Index{ static_cast<unsigned int>(MouseButton) };
	if (Index < 5)
	{
		mMouseStates[Index] = false;
	}
}


void FInputSystem::OnKeyDown(int VirtualKey)
{
	if (VirtualKey < 256)
	{
		mKeyStates[VirtualKey] = true;
	}
}

void FInputSystem::OnMouseDown(EMouseButton MouseButton)
{
	const unsigned int Index{ static_cast<unsigned int>(MouseButton) };
	if (Index < 5)
	{
		mMouseStates[Index] = true;
		if (MouseButton == EMouseButton::Right && !mRelativeMouseMode)
		{
			// 같은 메시지 큐에서 우클릭보다 먼저 들어온 이동을 카메라 캡처에 사용하지 않는다.
			mDeltaX = 0;
			mDeltaY = 0;
			mPreviousMouseX = mMouseX;
			mPreviousMouseY = mMouseY;
		}
	}
}

void FInputSystem::OnMouseMove(int32 X, int32 Y)
{
	// BeginFrame에서 초기화한 뒤 이번 메시지 큐에 들어온 이동량만 누적한다.
	// 여러 WM_MOUSEMOVE가 한 프레임에 처리돼도 첫 위치와 마지막 위치 사이의 실제 델타가 남는다.
	if (!mRelativeMouseMode)
	{
		mDeltaX += X - mMouseX;
		mDeltaY += Y - mMouseY;
	}

	mPreviousMouseX = mMouseX;
	mPreviousMouseY = mMouseY;
	mMouseX = X;
	mMouseY = Y;
}

void FInputSystem::OnMouseWheelDelta(int32 Wheel)
{
	// 터치패드·고해상도 휠은 한 프레임에 메시지를 여러 번 보내므로 덮어쓰지 않고 누적한다. 읽을 때(GetWheelDelta) 비운다.
	mWheelDelta += Wheel;
}

void FInputSystem::OnRawMouseMove(int32 X, int32 Y)
{
	if (mRelativeMouseMode)
	{
		mDeltaX += X;
		mDeltaY += Y;
	}
}

void FInputSystem::SetRelativeMouseMode(bool Enabled)
{
	mRelativeMouseMode = Enabled;
	mDeltaX = 0;
	mDeltaY = 0;
}

void FInputSystem::SyncMousePosition(int32 X, int32 Y)
{
	// 커서가 창 밖에서 움직인 뒤 돌아왔을 때 델타가 한 번 크게 튀는 것을 막는다.
	mMouseX = mPreviousMouseX = X;
	mMouseY = mPreviousMouseY = Y;

	mDeltaX = 0;
	mDeltaY = 0;
}

void FInputSystem::ClearAllStates()
{
	// 포커스를 잃으면 WM_KEYUP / WM_*BUTTONUP 이 이 창으로 오지 않는다.
	// 현재 상태만 비워서 다음 UpdateInputStates 에서 Released 가 한 번 발생하도록 한다.
	std::memset(mKeyStates, 0, sizeof(mKeyStates));
	std::memset(mMouseStates, 0, sizeof(mMouseStates));

	mWheelDelta = 0;
	mDeltaX = 0;
	mDeltaY = 0;
}

void FInputSystem::UpdateInputStates()
{
	std::memcpy(mPreviousKeyStates, mKeyStates, sizeof(mKeyStates));
	std::memcpy(mPreviousMouseStates, mMouseStates, sizeof(mMouseStates));

	// ProcessMessage가 이 호출 뒤에 실행되므로 여기서는 이전 프레임 델타만 비운다.
	// 현재 프레임 델타는 OnMouseMove가 메시지를 처리하면서 누적한다.
	mDeltaX = 0;
	mDeltaY = 0;
	mPreviousMouseX = mMouseX;
	mPreviousMouseY = mMouseY;
}


int32 FInputSystem::GetMouseX()
{
	return mMouseX;
}

int32 FInputSystem::GetMouseY()
{
	return mMouseY;
}

int32 FInputSystem::GetMouseDeltaX()
{
	return mDeltaX;
}

int32 FInputSystem::GetMouseDeltaY()
{
	return mDeltaY;
}

int32 FInputSystem::GetWheelDelta()
{
	const int32 Delta{ mWheelDelta };
	mWheelDelta = 0;
	return Delta;
}
