#pragma once

#include "Animation/AnimationAsset.h"
#include "Animation/AnimDataModel.h"

class UAnimSequenceBase : public UAnimationAsset
{
	DECLARE_CLASS(UAnimSequenceBase, UAnimationAsset)
public:
	virtual ~UAnimSequenceBase() override
	{
		// 엔진 종료 때 전역 정리가 DataModel을 먼저 지울 수 있다.
		if (DataModel && FindLiveObjectByAddress(DataModel))
		{
			delete DataModel;
		}
	}

	virtual float GetPlayLength() const override { return DataModel ? DataModel->GetPlayLength() : 0.0f; }

	TArray<FAnimNotifyEvent> Notifies;
	UAnimDataModel* DataModel = nullptr;
};
