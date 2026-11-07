#pragma once

#include "AnimSequenceBase.h"
#include "AnimPose.h"

class FAnimationRuntime
{
public:
	// 시간 단위는 초. MoveDelta는 재생 속도와 방향이 반영된 이동량이다.
	// EndTime은 0 이상이며, 정확히 양 끝에 도착한 경우는 ETAA_Default다.
	static ETypeAdvanceAnim AdvanceTime(bool bAllowLooping, float MoveDelta, float& InOutTime, float EndTime);

	// 두 Pose를 혼합해 첫 입력 InOutPoseOne에 덮어쓴다(InPlace). 첫 Pose의 원본은 보존되지 않는다.
	// UE의 BlendTwoPosesTogether는 별도 출력 인자를 받는 버전이며, 현재 여기서는 InPlace 버전만 제공한다.
	// 두 Pose는 같은 Skeleton의 전체 Bone 순서와 정규화된 회전을 사용한다.
	// WeightOfPoseOne은 첫 Pose의 비율(0~1)이며 두 번째 Pose의 비율은 1-WeightOfPoseOne이다.
	static void BlendTwoPosesTogetherInPlace(FAnimPose& InOutPoseOne, const FAnimPose& SourcePoseTwo, float WeightOfPoseOne);

	// 여러 Pose를 혼합해 명시적인 출력 OutPose에 기록한다. 두 Pose 전용 함수의 일반 버전과는 별개다.
	// OutPose가 입력과 별개이면 입력은 보존된다. 입력 중 하나를 OutPose로 지정하면 그 입력은 결과로 덮어쓴다.
	// Pose와 Weight 개수가 같고 1개 이상이어야 한다. Weight는 0~1이며 합이 1이다.
	// 모든 Pose의 Skeleton/Bone 순서가 같고 회전은 정규화되어 있어야 한다. 출력은 입력 중 하나와 같아도 된다.
	static void BlendPosesTogether(const TArray<FAnimPose>& SourcePoses, const TArray<float>& SourceWeights, FAnimPose& OutPose);
};
