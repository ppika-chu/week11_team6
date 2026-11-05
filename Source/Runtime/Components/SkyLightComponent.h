#pragma once

#include "LightComponentBase.h"

// 장면 전체에 고르게 비추는 환경광. 방향·그림자가 없어 UE처럼 ULightComponentBase를 바로 상속한다.
class USkyLightComponent : public ULightComponentBase
{
	DECLARE_CLASS(USkyLightComponent, ULightComponentBase)
	REFLECT_START(USkyLightComponent)
	REFLECT_END()
public:
	USkyLightComponent();
	virtual ~USkyLightComponent() override;

protected:
	// 프록시 없이 Scene의 환경광 목록에만 등록한다.
	virtual bool ShouldCreateRenderState() const override { return true; }
	virtual void CreateRenderState() override;
	virtual void DestroyRenderState() override;

protected:
private:
};