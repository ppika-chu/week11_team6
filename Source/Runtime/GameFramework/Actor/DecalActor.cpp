#include "EnginePCH.h"
#include "DecalActor.h"
#include "Components/DecalComponent.h"
#include "Components/BillboardComponent.h"
#include "Core/ResourcePaths.h"
#include "Asset/AssetManager.h"

ADecalActor::ADecalActor()
{
	UDecalComponent* Decal = CreateDefaultSubobject<UDecalComponent>("UDecalComponent");
	SetRootComponent(Decal);

	UBillboardComponent* Icon = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
	Icon->SetEditorIcon(true);
	Icon->SetupAttachment(Decal);
	Icon->SetHiddenInDetails(true);

	// Fog 액터와 같은 방식: SubUV 머티리얼 인스턴스에 아이콘 텍스처만 바꿔 끼운다.
	UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>("SubUVMaterial");
	UTexture2D* IconTexture = UAssetManager::Get().LoadTexture(ResourcePaths::DecalIcon);
	if (Base && IconTexture)
	{
		UMaterial* IconMat = UMaterial::CreateInstance(Base);
		IconMat->Textures[0] = IconTexture;
		Icon->SetMaterial(0, IconMat);
	}
}

UDecalComponent* ADecalActor::GetDecal() const
{
	return FindComponentByClass<UDecalComponent>();
}
