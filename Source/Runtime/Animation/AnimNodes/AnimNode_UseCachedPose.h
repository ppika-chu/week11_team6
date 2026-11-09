#pragma once

#include "AnimNode_Base.h"
#include "AnimNode_SaveCachedPose.h"
#include "Animation/AnimPose.h"

// 연결된 SaveCachedPose 노드에 포즈를 요청해 자신의 출력으로 전달하는 노드.
// 여러 UseCachedPose 노드가 하나의 SaveCachedPose를 비소유 참조로 공유할 수 있다.
// 캐시의 갱신과 유효성 판정은 SaveCachedPose에 맡기며, 출력 포즈를 별도로 저장하지 않는다.
struct FAnimNode_UseCachedPose : public FAnimNode_Base
{
public:
	void Initialize()
	{
		if (CachedPoseNode)
		{
			CachedPoseNode->Initialize();
		}
	}
	void Update(const FAnimationUpdateContext& Context)
	{
		if (CachedPoseNode)
		{
			CachedPoseNode->Update(Context);
		}
	}
	void Evaluate(FAnimPose& OutPose)
	{
		if (CachedPoseNode)
		{
			CachedPoseNode->Evaluate(OutPose);
		}
	}

	void SetCachedPoseNode(FAnimNode_SaveCachedPose* InCachedPoseNode) { CachedPoseNode = InCachedPoseNode; }
	FAnimNode_SaveCachedPose* GetCachedPoseNode() const { return CachedPoseNode; }

private:
	FAnimNode_SaveCachedPose* CachedPoseNode = nullptr; // 포즈 캐시를 제공하는 SaveCachedPose 노드
};
