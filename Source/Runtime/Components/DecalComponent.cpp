#include "EnginePCH.h"
#include "DecalComponent.h"
#include "Asset/AssetManager.h"
#include "Core/ResourcePaths.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UDecalComponent::UDecalComponent()
{
	// 페이드 인·아웃 시간을 매 프레임 진행한다.
	PrimaryComponentTick.bCanEverTick = true;

	// 액터 스폰이든 컴포넌트 추가든 바로 보이도록 기본 텍스처를 넣어 둔다. 디테일 패널에서 바꿀 수 있다.
	UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
	UTexture2D* Texture = UAssetManager::Get().LoadTexture(ResourcePaths::DefaultDecalTexture);
	if (Base && Texture)
	{
		DecalMaterial = UMaterial::CreateInstance(Base);
		DecalMaterial->Textures[0] = Texture;
	}
}

void UDecalComponent::BeginPlay()
{
	Super::BeginPlay();

	// 시작 시점부터 시간을 센다. 페이드 인이 있으면 첫 프레임부터 투명하게 시작한다.
	FadeElapsedTime = 0.0f;
	UpdateFadeAlpha();
}

void UDecalComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	FadeElapsedTime += DeltaTime;
	if (!UpdateFadeAlpha())
	{
		return;
	}

	// 페이드 아웃이 끝났다. UE와 같이 옵션이 꺼져 있어도 컴포넌트는 지우고, 켜져 있으면 액터까지 지운다.
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		PrimaryComponentTick.SetTickFunctionEnable(false);
	}
	else if (bDestroyOwnerAfterFade)
	{
		Owner->Destroy();
	}
	else
	{
		Owner->DestroyComponent(this);
	}
}

bool UDecalComponent::UpdateFadeAlpha()
{
	// UE와 같이 두 페이드 모두 시작 시점부터 잰다. 지연 시간 동안은 페이드 인은 0, 페이드 아웃은 1이다.
	float FadeIn = 1.0f;
	if (FadeInDuration > 0.0f || FadeInStartDelay > 0.0f)
	{
		const float Time = FadeElapsedTime - FadeInStartDelay;
		if (Time < 0.0f)
		{
			FadeIn = 0.0f;
		}
		else if (FadeInDuration > 0.0f)
		{
			FadeIn = FMath::Clamp(Time / FadeInDuration, 0.0f, 1.0f);
		}
	}

	// FadeDuration이 0 이하면 페이드 아웃을 하지 않는다.
	float FadeOut = 1.0f;
	bool bFadeOutFinished = false;
	if (FadeDuration > 0.0f)
	{
		const float Time = FadeElapsedTime - FadeStartDelay;
		FadeOut = 1.0f - FMath::Clamp(Time / FadeDuration, 0.0f, 1.0f);
		bFadeOutFinished = Time >= FadeDuration;
	}

	FadeAlpha = FadeIn * FadeOut;
	return bFadeOutFinished;
}

void UDecalComponent::SetDecalMaterial(UMaterial* NewDecalMaterial)
{
	DecalMaterial = NewDecalMaterial;
}

void UDecalComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	// 디테일 패널에서 텍스처를 넣으면 머티리얼 인스턴스가 생기므로, 에셋 경로만 저장하는 리플렉션 대신
	// 기준 에셋 + 값을 함께 저장하는 SerializeMaterialReference를 쓴다 (BillboardComponent와 같은 방식).
	UMaterial* Loaded = DecalMaterial;
	UMaterial::SerializeMaterialReference(Record.EnterField("DecalMaterial"), Loaded);
	if (Record.GetUnderlyingArchive().IsLoading())
	{
		DecalMaterial = Loaded;
	}
}

UMaterial* UDecalComponent::GetDecalMaterial() const
{
	return DecalMaterial;
}

void UDecalComponent::SetDecalSize(const FVector& Size)
{
	DecalSize = Size;
}

FVector UDecalComponent::GetDecalSize() const
{
	return DecalSize;
}

void UDecalComponent::SetDecalColor(const FLinearColor& Color)
{
	DecalColor = Color;
}

FLinearColor UDecalComponent::GetDecalColor() const
{
	return DecalColor;
}

void UDecalComponent::SetSortOrder(int32 Order)
{
	SortOrder = Order;
}

int32 UDecalComponent::GetSortOrder() const
{
	return SortOrder;
}

void UDecalComponent::SetFadeStartDelay(float Delay)
{
	FadeStartDelay = Delay;
}

float UDecalComponent::GetFadeStartDelay() const
{
	return FadeStartDelay;
}

void UDecalComponent::SetFadeDuration(float Duration)
{
	FadeDuration = Duration;
}

float UDecalComponent::GetFadeDuration() const
{
	return FadeDuration;
}

void UDecalComponent::SetFadeInStartDelay(float Delay)
{
	FadeInStartDelay = Delay;
}

float UDecalComponent::GetFadeInStartDelay() const
{
	return FadeInStartDelay;
}

void UDecalComponent::SetFadeInDuration(float Duration)
{
	FadeInDuration = Duration;
}

float UDecalComponent::GetFadeInDuration() const
{
	return FadeInDuration;
}

void UDecalComponent::SetDestroyOwnerAfterFade(bool bDestroy)
{
	bDestroyOwnerAfterFade = bDestroy;
}

bool UDecalComponent::GetDestroyOwnerAfterFade() const
{
	return bDestroyOwnerAfterFade;
}

float UDecalComponent::GetFadeAlpha() const
{
	return FadeAlpha;
}

FMatrix UDecalComponent::GetDecalWorldMatrix() const
{
	FMatrix Scale = FMatrix::Identity;
	Scale.M[0][0] = DecalSize.X;
	Scale.M[1][1] = DecalSize.Y;
	Scale.M[2][2] = DecalSize.Z;
	return Scale * GetWorldMatrix();
}

FMatrix UDecalComponent::GetDecalViewProjection() const
{
	return GetDecalWorldMatrix().Inverse();
}

Collision::FOrientedBox UDecalComponent::GetOrientedBox() const
{
	const FQuat Rot = GetWorldRotation().Quaternion();
	const FVector S = GetWorldScale3D();

	Collision::FOrientedBox Box;
	Box.Center = GetWorldLocation();
	Box.Axis[0] = Rot.GetForwardVector();
	Box.Axis[1] = Rot.GetRightVector();
	Box.Axis[2] = Rot.GetUpVector();
	Box.Extent = FVector(DecalSize.X * std::abs(S.X), DecalSize.Y * std::abs(S.Y), DecalSize.Z * std::abs(S.Z));
	return Box;
}

FBox UDecalComponent::CalcLocalBounds() const
{
	return FBox{ -DecalSize, DecalSize };
}

bool UDecalComponent::ShouldCreateRenderState() const
{
	return true;
}

void UDecalComponent::CreateRenderState()
{
	Super::CreateRenderState();
	GetWorld()->GetScene().AddDecal(this);
}

void UDecalComponent::DestroyRenderState()
{
	GetWorld()->GetScene().RemoveDecal(this);
	Super::DestroyRenderState();
}
