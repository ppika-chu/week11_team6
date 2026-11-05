#include "EnginePCH.h"
#include "SkyLightComponent.h"

#include "Engine/Scene.h"
#include "Engine/World.h"

USkyLightComponent::USkyLightComponent() {}
USkyLightComponent::~USkyLightComponent() {}

void USkyLightComponent::CreateRenderState()
{
	Super::CreateRenderState();
	GetWorld()->GetScene().AddSkyLight(this);
}

void USkyLightComponent::DestroyRenderState()
{
	GetWorld()->GetScene().RemoveSkyLight(this);
	Super::DestroyRenderState();
}
