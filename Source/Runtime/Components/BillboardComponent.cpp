#include "EnginePCH.h"
#include "BillboardComponent.h"

#include "Asset/AssetManager.h"
#include "Serialization/TypeSerializer.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "Engine/BillboardSceneProxy.h"

// Billboard 컴포넌트의 초기 상태를 구성한다.
UBillboardComponent::UBillboardComponent()
{
	QuadMesh = UAssetManager::GetAssetByPath<UStaticMesh>("ParticleQuad");
	Material = UAssetManager::GetAssetByPath<UMaterial>("SubUVMaterial");
}

// Billboard 컴포넌트의 소멸을 처리한다.
UBillboardComponent::~UBillboardComponent()
{
}

// Scene이 이 Proxy를 등록하고 Component의 렌더 상태가 끝날 때 삭제한다.
FPrimitiveSceneProxy* UBillboardComponent::CreateSceneProxy()
{
	return new FBillboardSceneProxy(this);
}

// 기본 Mesh와 Material을 에셋 관리자에서 가져온다.
void UBillboardComponent::BeginPlay()
{
	Super::BeginPlay();
}

// 부모 컴포넌트의 프레임 갱신을 호출한다.
void UBillboardComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
}

void UBillboardComponent::OnSiblingComponentDestroyed(UActorComponent* DestroyedComponent)
{
	Super::OnSiblingComponentDestroyed(DestroyedComponent);

	if (TintSource == DestroyedComponent)
	{
		TintSource = nullptr;
	}
}

// 쿼드 AABB를 그대로 쓰면 두께가 0이라, 옆에서 본 View의 Ray가 BVH에서 빌보드를 놓친다.
// 어느 방향으로 돌아도 쿼드를 감싸는 반지름을 쓰되, 렌더처럼 스케일 Y·Z를 폭·높이로 본다.
FBox UBillboardComponent::CalcLocalBounds() const
{
	if (!QuadMesh)
	{
		return Super::CalcLocalBounds();
	}

	const FBox& Quad = QuadMesh->GetMeshData().AABB;
	const FVector Scale = GetWorldScale3D();
	// 렌더·피킹과 같은 크기(에디터 아이콘은 IconSize)로 감싼다.
	const FVector2 RenderSize = GetRenderSize();
	const float HalfWidth = std::max(std::fabs(Quad.Min.Y), std::fabs(Quad.Max.Y)) * std::fabs(RenderSize.X);
	const float HalfHeight = std::max(std::fabs(Quad.Min.Z), std::fabs(Quad.Max.Z)) * std::fabs(RenderSize.Y);
	const float Radius = std::sqrt(HalfWidth * HalfWidth + HalfHeight * HalfHeight);

	// 프록시가 이 상자에 컴포넌트 월드 행렬(스케일 포함)을 곱하므로 축마다 스케일을 미리 나눈다.
	constexpr float MinScale = 1.0e-4f;
	const FVector Half(
		Radius / std::max(std::fabs(Scale.X), MinScale),
		Radius / std::max(std::fabs(Scale.Y), MinScale),
		Radius / std::max(std::fabs(Scale.Z), MinScale));
	return FBox{ Half * -1.0f, Half };
}

bool UBillboardComponent::LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit)
{
	if (!QuadMesh)
	{
		return false;
	}

	FMatrix BillboardMatrix;
	GetWorldTransformedMatrix(&BillboardMatrix); // 카메라를 향하는, 실제로 그려지는 행 렬
	return TraceMesh(WorldRay, QuadMesh->GetMeshData(), BillboardMatrix, OutHit);
}

// View별 렌더 행렬을 그대로 사용해 메인 카메라와 다른 방향에서도 같은 면을 선택한다.
bool UBillboardComponent::LineTraceComponentForView(
	const FRay& WorldRay, FHitResult& OutHit, const FMatrix& BillboardWorldMatrix)
{
	return QuadMesh && TraceMesh(WorldRay, QuadMesh->GetMeshData(), BillboardWorldMatrix, OutHit);
}

// 기본 카메라용 행렬을 구해 공통 렌더 패킷 제출 경로로 전달한다.
void UBillboardComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue)
{
	// 렌더러가 역참조하므로 둘 중 하나라도 없으면 보내지 않는다
	if (QuadMesh == nullptr || Material == nullptr)
	{
		return;
	}

	FMatrix BillboardWorldMatrix;
	GetWorldTransformedMatrix(&BillboardWorldMatrix);
	SubmitToRenderQueue(RenderQueue, BillboardWorldMatrix);
}

// View별 Billboard 행렬과 Material을 렌더 패킷에 담는다.
void UBillboardComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FMatrix& BillboardWorldMatrix)
{
	if (QuadMesh == nullptr || Material == nullptr)
	{
		return;
	}

	// Light Component light 가져와서 IconParams로 전달
	IconParams.Tint = TintSource ? TintSource->GetLightColor().ToVector4() : FVector4(1, 1, 1, 1);
	// 에디터 아이콘은 Overlay 패스(톤 매핑 이후 LDR)에, 일반 빌보드는 HDR 씬에 그려진다.
	IconParams.DecodeSRGB = bEditorIcon ? 0.0f : 1.0f;
	FRenderPacket Packet;
	Packet.VertexFactory = &QuadMesh->GetVertexFactory(0);
	Packet.IndexBuffer = QuadMesh->GetIndexBuffer(0);
	Packet.Material = Material;
	Packet.Model = RenderQueue.StoreWorldMatrix(BillboardWorldMatrix);
	Packet.MaterialParamData = &IconParams;
	Packet.MaterialParamDataSize = sizeof(FSubUVConstants);
	RenderQueue.Add(Packet);
}

void UBillboardComponent::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);

	if (bIsLoading)
	{
		// 예전 파일은 "Material"이 문자열(경로)이라 형식을 확인하고 읽는다
		if (Handle.contains("Material") && Handle["Material"].is_object())
		{
			if (UMaterial* Loaded = UMaterial::LoadMaterial(Handle["Material"]))
			{
				Material = Loaded; // 못 만들었으면 생성자 기본값 유지
			}
		}
	}
	else
	{
		Handle["Material"] = Material ? UMaterial::SaveMaterial(Material) : json(nullptr);
	}
}

FMatrix MakePlayCameraFacingMatrix(UWorld* World, const FRotator& FallbackRotation, const FVector& Position, const FVector& Scale)
{
	const UCameraComponent* Camera{ World ? World->GetPlayCameraComponent() : nullptr };
	const FTransform Transform{ Camera ? FTransform{ Camera->GetWorldRotation(), Camera->GetWorldLocation() } : FTransform{ FallbackRotation, Position } };

	const FVector Right = Transform.GetRight().Normalized();
	const FVector Forward = Transform.GetForward().Normalized();

	const FVector BbUp = Transform.GetUp().Normalized();
	FVector BbRight = FVector::Cross(BbUp, Forward).Normalized();
	FVector BbFwd = FVector::Cross(BbUp, BbRight);

	if (BbRight.Length() <= 1e-6f)
	{
		BbRight = Right;
		BbFwd = FVector::Cross(BbUp, BbRight);
	}

	FMatrix Result;
	Result.SetIdentity();

	// X -> Billboard Forward
	Result.M[0][0] = BbFwd.X * Scale.X;
	Result.M[0][1] = BbFwd.Y * Scale.X;
	Result.M[0][2] = BbFwd.Z * Scale.X;
	Result.M[0][3] = 0.0f;

	// Y -> Billboard Right
	Result.M[1][0] = BbRight.X * Scale.Y;
	Result.M[1][1] = BbRight.Y * Scale.Y;
	Result.M[1][2] = BbRight.Z * Scale.Y;
	Result.M[1][3] = 0.0f;

	// Z -> Billboard Up
	Result.M[2][0] = BbUp.X * Scale.Z;
	Result.M[2][1] = BbUp.Y * Scale.Z;
	Result.M[2][2] = BbUp.Z * Scale.Z;
	Result.M[2][3] = 0.0f;

	// Position
	Result.M[3][0] = Position.X;
	Result.M[3][1] = Position.Y;
	Result.M[3][2] = Position.Z;
	Result.M[3][3] = 1.0f;
	return Result;
}

// 카메라를 향하는 기저와 위치·크기로 Billboard 행렬을 구성한다.
void UBillboardComponent::GetWorldTransformedMatrix(FMatrix* OutWorldMatrix) const
{
	// Icon 일 때만 IconSize 넘기기
	const FVector Scale = IsEditorIcon() ? FVector(IconSize, IconSize, IconSize) : GetWorldScale3D();
	UWorld* World{ GetOwner() ? GetOwner()->GetWorld() : nullptr };
	*OutWorldMatrix = MakePlayCameraFacingMatrix(World, GetWorldRotation(), GetWorldLocation(), Scale);
}

void UBillboardComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	UMaterial* Loaded = Material;
	UMaterial::SerializeMaterialReference(Record.EnterField("Material"), Loaded);

	// 불러오기에 실패하면(null) 생성자가 넣어 둔 기본 머티리얼을 유지한다
	if (Record.GetUnderlyingArchive().IsLoading() && Loaded)
	{
		Material = Loaded;
	}
}
