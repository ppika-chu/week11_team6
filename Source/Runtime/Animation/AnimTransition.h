#pragma once

// 두 State 사이의 방향 있는 전이와 선택·혼합 설정을 정의하는 일반 구조체.
// 출발·목적 상태, 조건 결과(TransitionResult), 우선순위, 블렌드 시간 등을 연결한다.
// StateMachine은 이 정의와 조건 결과를 사용해 전이를 선택하고 포즈를 혼합한다.
struct FAnimTransition
{
};
