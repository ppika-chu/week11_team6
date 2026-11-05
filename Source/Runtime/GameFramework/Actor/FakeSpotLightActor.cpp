#include "EnginePCH.h"
#include "FakeSpotLightActor.h"
#include "Components/DecalComponent.h"
#include "Components/BillboardComponent.h"
#include "Core/ResourcePaths.h"
#include "Asset/AssetManager.h"

AFakeSpotLightActor::AFakeSpotLightActor()
{
	// 빌보드와 데칼의 스케일이 서로 영향을 주지 않도록 빈 루트에 나란히 붙인다.
	SetRootComponent(CreateDefaultSubobject<USceneComponent>("FakeSpotLightRoot"));

	// 게임에서도 보이는 빛의 원천. 에디터 아이콘이 아니므로 크기는 스케일 Y·Z를 따른다.
	UBillboardComponent* Billboard = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
	Billboard->SetupAttachment(GetRootComponent());
	Billboard->SetRelativeScale3D(FVector(1.0f, 0.5f, 0.5f));

	UMaterial* SubUV = UAssetManager::GetAssetByPath<UMaterial>("SubUVMaterial");
	UTexture2D* IconTexture = UAssetManager::Get().LoadTexture(ResourcePaths::SpotLightIcon);
	if (SubUV && IconTexture)
	{
		UMaterial* IconMat = UMaterial::CreateInstance(SubUV);
		IconMat->Textures[0] = IconTexture;
		Billboard->SetMaterial(0, IconMat);
	}

	const FVector Size(5.0f, 2.0f, 2.0f);
	UDecalComponent* Decal = CreateDefaultSubobject<UDecalComponent>("UDecalComponent");
	Decal->SetupAttachment(GetRootComponent());
	Decal->SetRelativeLocation(FVector(Size.X, 0.0f, 0.0f));
	Decal->SetDecalSize(Size);
	Decal->SetDecalColor(FLinearColor(1.0f, 0.85f, 0.6f, 1.0f));

	UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
	UTexture2D* LightTexture = UAssetManager::Get().LoadTexture(ResourcePaths::FakeSpotLightTexture);
	if (Base && LightTexture)
	{
		UMaterial* LightMat = UMaterial::CreateInstance(Base);
		LightMat->Textures[0] = LightTexture;
		LightMat->SetBlendState(EBlendState::Additive);
		Decal->SetDecalMaterial(LightMat);
	}
}

UDecalComponent* AFakeSpotLightActor::GetDecal() const
{
	return FindComponentByClass<UDecalComponent>();
}

UBillboardComponent* AFakeSpotLightActor::GetBillboard() const
{
	return FindComponentByClass<UBillboardComponent>();
}
