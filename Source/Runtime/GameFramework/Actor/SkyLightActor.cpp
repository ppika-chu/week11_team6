#include "EnginePCH.h"
#include "SkyLightActor.h"
#include "Core/ResourcePaths.h"

ASkyLightActor::ASkyLightActor()
{
	SkyLightComponent = CreateDefaultSubobject<USkyLightComponent>("USkyLightComponent");
	SetupLightRoot(SkyLightComponent, ResourcePaths::SkyLightIcon);
}