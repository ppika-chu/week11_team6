#pragma once

#include "Core/Types.h"

// PIE를 어디에 띄울지 고른다. UE의 Play 버튼 옆 모드 선택에 해당한다.
enum class EPlayModeType : uint8
{
	InViewport,       // 선택한 패널 View에서 실행
	InEditorFloating, // 별도 OS 창에서 실행
};

struct FRequestPlaySessionParams
{
public:
	// Default Constructor
	FRequestPlaySessionParams()
	{
	}
};
