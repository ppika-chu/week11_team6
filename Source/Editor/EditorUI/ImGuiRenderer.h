#pragma once

#include <imgui.h>
#include <Windows.h>

#include <d3d11.h>

class FImGuiRenderer
{
public:
	FImGuiRenderer() = default;
	~FImGuiRenderer();

	bool Init(HWND WindowHandle, ID3D11Device* Device, ID3D11DeviceContext* DeviceContext);
	void Begin();
	void End();
	void Shutdown();

private:
	// 스타일을 기본값에서 다시 만들어 배율을 곱한다. 반복해도 반올림 오차가 쌓이지 않는다.
	void ApplyDpiScale(float DpiScale);

	HWND MainWindowHandle = nullptr;
	float AppliedDpiScale = 0.0f;
};