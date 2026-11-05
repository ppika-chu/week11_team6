#pragma once

#include "SceneComponent.h"
#include "Math/Color.h"

class FLineBatcher;

enum class ELightType
{
	Directional,
	Point,
	Spot,
	Rect,
	Sky
};

class ULightComponentBase : public USceneComponent
{
	DECLARE_CLASS(ULightComponentBase, USceneComponent)
	REFLECT_START(ULightComponentBase)
		PROPERTY(Intensity)
		PROPERTY(LightColor)
		PROPERTY(bVisible)
	REFLECT_END()
public:
	ULightComponentBase();
	virtual ~ULightComponentBase() override;


	// Getter
	float GetIntensity() const { return Intensity; }
	FColor GetLightColor() const { return LightColor; }
	bool IsVisible() const { return bVisible; }

	// Setter
	void SetIntensity(float InIntensity) { Intensity = InIntensity; }
	void SetLightColor(FColor InLightColor) { LightColor = InLightColor; }
	void SetVisibility(bool bNewVisibility) { bVisible = bNewVisibility; }

	// 에디터에서 선택했을 때 영향 범위 같은 보조선을 그린다.
	virtual void DrawDebug(FLineBatcher* LineBatcher) const {}


protected:
	float Intensity = 1.0f;
	FColor LightColor = FColor(255, 255, 255, 255);
	bool bVisible = true;

private:
};