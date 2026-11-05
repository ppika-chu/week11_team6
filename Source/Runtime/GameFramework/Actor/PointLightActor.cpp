#include "EnginePCH.h"
#include "PointLightActor.h"
#include "Core/ResourcePaths.h"

APointLightActor::APointLightActor()
{
	PointLightComponent = CreateDefaultSubobject<UPointLightComponent>("UPointLightComponent");
	SetupLightRoot(PointLightComponent, ResourcePaths::PointLightIcon);
}