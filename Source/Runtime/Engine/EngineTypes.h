#pragma once

enum class EWorldType : uint8
{
	None,
	Game,
	Editor,
	PIE,
	EditorPreview // 에셋 에디터의 프리뷰 월드. 컴포넌트 Tick만 돌고 게임 처리는 돌지 않는다.
};
