#pragma once

#include "Animation/AnimationAsset.h"
#include "Animation/AnimDataModel.h"

class UAnimSequenceBase : public UAnimationAsset
{
	DECLARE_CLASS(UAnimSequenceBase, UAnimationAsset)
public:
	virtual float GetPlayLength() const override { return DataModel ? DataModel->GetPlayLength() : 0.0f; }

	TArray<FAnimNotifyEvent> Notifies;
	UAnimDataModel* DataModel = nullptr;
};
