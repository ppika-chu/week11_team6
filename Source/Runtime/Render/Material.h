#pragma once

#include "MaterialInterface.h"
#include "Render/Buffer.h" // ParamBuffer(TUniquePtr<FConstantBuffer>)를 지우려면 완전한 타입이 필요하다
#include "Render/RenderEnums.h"
#include "Render/RenderStates.h"
#include "Render/Texture2D.h"

class FShader;



enum class EMaterialParamLayout
{
	None,
	StaticMesh,
	ParticleSubUV
};

struct FStaticMeshMaterialParams
{
	FVector4 DiffuseOrFactor; // 선형 RGB와 불투명도. UMaterial::DiffuseOrFactor를 그대로 업로드한다.
	FVector2 UVOffset;
	float bOpaque;   // 1이면 PS가 알파를 1로 출력한다
	float Shininess; // Blinn-Phong 광택 지수.
	FVector2 WorldUVTileSize; // 텍스처 한 장의 월드 크기 (가로, 세로). X가 0이면 꺼짐
	FVector2 UVTiling;        // 메시 UV에 곱하는 반복 횟수
	float WorldUVLocalSpace;  // 1이면 World Tile을 메시 로컬 축 기준으로 깐다 (메시를 돌리면 무늬도 돈다)
	float MaterialPadding[3];
	FVector SpecularColor = FVector(1, 1, 1); // 선형 반사광 색상
	float SpecularFactor = 1.f; // 반사광 강도 계수
	FVector EmissiveColor = FVector(0, 0, 0); // 선형 자체 발광 색상
	float EmissiveFactor = 1.f; // 자체 발광 강도 계수
};
static_assert(sizeof(FStaticMeshMaterialParams) == 96, "UberLit.hlsl cbuffer MaterialParams 크기와 맞아야 한다");

class UMaterial : public UMaterialInterface
{
	DECLARE_CLASS(UMaterial, UMaterialInterface)

public:
	UMaterial()
		: SortID(NextSortID++) {};
	virtual ~UMaterial() override = default;

	EMaterialParamLayout ParamLayout = EMaterialParamLayout::None;
	FShaderProgram* Shader;
	TArray<UTexture2D*> Textures;
	TUniquePtr<FConstantBuffer> ParamBuffer;
	EBlendState BlendState = EBlendState::Opaque;
	EDepthStencilState DepthStencilState = EDepthStencilState::Default;
	ESamplerState SamplerState = ESamplerState::LinearClamp;
	ELightingModel ShadingModel = ELightingModel::BlinnPhong;

	// 선형 RGB: 색상 맵이 없으면 diffuse 계수, 있으면 맵 RGB에 곱하는 factor. 알파는 불투명도.
	FVector4 DiffuseOrFactor = FVector4(1, 1, 1, 1);
	float Shininess = 32.0f; // Blinn-Phong Specular의 pow 지수
	FVector SpecularColor = FVector(1, 1, 1); // 선형 반사광 색상
	float SpecularFactor = 1.f; // 반사광 강도 계수
	FVector EmissiveColor = FVector(0, 0, 0); // 선형 자체 발광 색상
	float EmissiveFactor = 1.f; // 자체 발광 강도 계수
	FVector2 UVScrollSpeed = FVector2(0.0f, 0.0f);
	// 메시 UV(0~1)에 곱해서 텍스처를 몇 번 반복할지 정한다. 반복해 보이려면 SamplerState가 LinearWrap이어야 한다.
	FVector2 UVTiling = FVector2(1.0f, 1.0f);
	// X가 0보다 크면 메시 UV 대신 월드 좌표로 깐다. 텍스처 한 장이 월드에서 가로 X, 세로 Y만큼을 덮는다.
	// Y가 0이면 X와 같게 본다(정사각형). 켜져 있으면 UVTiling은 쓰지 않는다.
	FVector2 WorldUVTileSize = FVector2(0.0f, 0.0f);
	// World Tile의 기준 축. false면 월드 축(메시를 돌려도 무늬는 월드에 고정),
	// true면 메시 로컬 축(무늬가 메시를 따라 돈다). 어느 쪽이든 크기는 월드 단위라 스케일하면 반복 개수가 늘어난다.
	bool bWorldUVLocalSpace = false;

	bool bIsInstance = false;

	// 블렌드를 바꿀 때는 이 함수를 거친다. 스태틱 메시는 깊이 상태도 함께 맞춘다.
	// (반투명은 깊이를 쓰면 뒤의 반투명을 가리고, 불투명은 깊이를 안 쓰면 정렬이 깨진다)
	void SetBlendState(EBlendState InBlendState);

	// 인스턴스가 복제된 원본. 인스턴스는 경로가 없어서, 저장할 때 원본을 따라가 기준 에셋을 찾는다
	const UMaterial* Parent = nullptr;

	// Source의 내용을 복사한 편집용 복제본을 만든다
	static UMaterial* CreateInstance(const UMaterial* Source);

	// 컴포넌트가 지워져 쓰지 않는 인스턴스가 생겼을 수 있음을 알린다. 실제 삭제는 TickUnusedInstanceRelease가 한다.
	static void RequestUnusedInstanceRelease() { bUnusedInstanceReleaseRequested = true; }
	// 요청이 있으면 주기마다 한 번 수거한다. 머티리얼을 들고 있는 코드가 없는 프레임 시작에서만 부른다.
	static void TickUnusedInstanceRelease(float DeltaTime);

	// 경로가 있는(=에셋으로 등록된) 가장 가까운 원본. 자신이 에셋이면 자신
	const UMaterial* GetBaseAsset() const;

	static json SaveMaterial(const UMaterial* Material);
	static UMaterial* LoadMaterial(const json& In);

	const uint16 SortID;

	using Super::Serialize;
	// 인스턴스의 값(색, 텍스처, 상태). 객체는 이미 만들어져 있어야 한다.
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	// 머티리얼 참조 하나를 Slot에 읽고 쓴다.
	// 에셋이면 경로만, 인스턴스면 기준 에셋 + 값을 그 자리에 쓰고, 불러올 때 인스턴스를 새로 만든다.
	// null이면 빈 Record({})로 기록되고, 불러오면 nullptr이 된다.
	static void SerializeMaterialReference(FStructuredArchive::FSlot Slot, UMaterial*& Material);

private:
	// 컴포넌트·메시·렌더 프록시 어디에서도 참조하지 않는 인스턴스를 지운다.
	static void ReleaseUnusedInstances();

	inline static uint16 NextSortID = 0;
	inline static bool bUnusedInstanceReleaseRequested = false;
	inline static float UnusedInstanceReleaseElapsed = 0.0f;
};
