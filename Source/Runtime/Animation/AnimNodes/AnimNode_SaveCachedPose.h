#pragma once

#include "AnimNode_Base.h"
#include "Animation/AnimPose.h"

// 입력 그래프의 로컬 포즈를 저장해 여러 UseCachedPose 노드가 공유하도록 하는 노드.
// 같은 실행 회차의 중복 갱신과 평가를 막고, 평가한 포즈는 CachedPose에 보관한다.
// 입력 노드는 비소유 참조이며, 캐시의 수명과 실행 회차 판정은 이 노드에서 관리한다.
struct FAnimNode_SaveCachedPose : public FAnimNode_Base
{
public:
	void Initialize() override;
	void Update(float DeltaTime) override;
	void Evaluate(FAnimPose& OutPose) override;

	void SetInputNode(FAnimNode_Base* InInputNode) { InputNode = InInputNode; }
	FAnimNode_Base* GetInputNode() const { return InputNode; }

private:
	FAnimNode_Base* InputNode = nullptr; // 캐시할 포즈를 제공하는 입력 노드
	FAnimPose CachedPose;               // 여러 UseCachedPose 노드가 재사용할 로컬 포즈
	// TODO(AnimExecution): 실행 번호 전달 방식 확정 후 중복 갱신·평가와 이전 회차 캐시 재사용을 막는 상태를 추가한다.
};
