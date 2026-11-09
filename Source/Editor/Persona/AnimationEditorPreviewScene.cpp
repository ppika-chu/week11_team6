#include "EnginePCH.h"

#include "Editor/Persona/AnimationEditorPreviewScene.h"

#include "Asset/AssetManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "DebugSkelMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "Render/Mesh.h"
#include "Render/RenderCommand.h"
#include "Render/SkeletalMesh.h"

#include <imgui.h>
#include <cfloat>
#include <cmath>

namespace
{
	constexpr float FloorSize = 40.0f;       // 바닥 한 변 (m)
	constexpr float FloorThickness = 1.0f;   // 바닥 두께 (m)
}

FAnimationEditorPreviewScene::FAnimationEditorPreviewScene(USkeletalMesh* InMesh)
	: Mesh(InMesh)
{
	CreatePreviewScene();
	SceneRadius = ComputeSceneRadius();
	FocusOnMesh();
}

FAnimationEditorPreviewScene::~FAnimationEditorPreviewScene()
{
	// PIE 종료와 같은 순서. 액터는 월드가 지운다
	// TODO: 렌더가 붙으면 GPU가 이 월드의 자원을 쓰는 중에 지우지 않는지 확인
	if (PreviewWorld)
	{
		PreviewWorld->ClearWorld();
		delete PreviewWorld;
		PreviewWorld = nullptr;
		FloorActor = nullptr;
		PreviewActor = nullptr;
		PreviewComponent = nullptr;
	}
}

void FAnimationEditorPreviewScene::Tick(float DeltaTime)
{
	PreviewWorld->Tick(DeltaTime);

	// 렌더(RenderMultipleViewports)가 Tick 뒤에 오므로 여기서 타깃과 카메라를 맞춰 둔다
	UpdateRenderTargets();
	UpdatePreviewCamera();
}

void FAnimationEditorPreviewScene::SetMeshVisible(bool bVisible)
{
	bShowMesh = bVisible;
	PreviewComponent->SetVisible(bShowMesh);
}

void FAnimationEditorPreviewScene::SetAnimationPlaybackSpeedMode(EAnimationPlaybackSpeeds::Type InMode)
{
	AnimationPlaybackSpeedMode = InMode;

	if (PreviewWorld)
	{
		const float AnimationSpeed = (InMode == EAnimationPlaybackSpeeds::Custom)
			? GetCustomAnimationSpeed()
			: EAnimationPlaybackSpeeds::Values[AnimationPlaybackSpeedMode];
		// 0 ~ 10배로 제한된다
		PreviewWorld->SetTimeScale(AnimationSpeed);
	}
}

void FAnimationEditorPreviewScene::SetCustomAnimationSpeed(float InAnimationSpeed)
{
	CustomAnimationSpeed = InAnimationSpeed;
	SetAnimationPlaybackSpeedMode(EAnimationPlaybackSpeeds::Custom);
}

void FAnimationEditorPreviewScene::SetFloorVisible(bool bVisible)
{
	bShowFloor = bVisible;
	FloorActor->GetStaticMeshComponent()->SetVisible(bShowFloor);
}

void FAnimationEditorPreviewScene::HandleCameraInput(bool bAllowOrbit)
{
	const ImGuiIO& IO = ImGui::GetIO();

	// 이미지 위에서 누른 채 드래그 중일 때만
	if (ImGui::IsItemActive())
	{
		// 언리얼처럼 Alt + 좌클릭만 오빗. 그냥 좌클릭은 본 선택 / 기즈모용으로 남겨 둔다
		if (IO.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left) && bAllowOrbit)
		{
			// 뒤집힌 상태에서는 화면 좌우도 반대이므로 Yaw 입력을 반전
			const bool bUpsideDown = std::cos(OrbitPitch * (3.14159265f / 180.0f)) < 0.0f;
			OrbitYaw += IO.MouseDelta.x * 0.3f * (bUpsideDown ? -1.0f : 1.0f);
			OrbitPitch -= IO.MouseDelta.y * 0.3f;
			// 계속 돌려도 값이 커지지 않게 -180 ~ 180으로 감싼다
			OrbitPitch = std::remainder(OrbitPitch, 360.0f);
		}
		else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
		{
			// 화면 기준 오른쪽, 위쪽으로 대상 점을 옮긴다. 멀리 있을수록 크게 움직인다
			const float YawRad = OrbitYaw * (3.14159265f / 180.0f);
			const float PitchRad = OrbitPitch * (3.14159265f / 180.0f);
			const FVector Right(-std::sin(YawRad), std::cos(YawRad), 0.0f);
			const FVector Up(-std::sin(PitchRad) * std::cos(YawRad), -std::sin(PitchRad) * std::sin(YawRad), std::cos(PitchRad));
			const float PanSpeed = OrbitDistance * 0.02f;
			OrbitTarget = OrbitTarget - Right * (IO.MouseDelta.x * PanSpeed) + Up * (IO.MouseDelta.y * PanSpeed);
		}
		else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
		{
			const auto CalcForward = [this]()
			{
				const float YawRad = OrbitYaw * (3.14159265f / 180.0f);
				const float PitchRad = OrbitPitch * (3.14159265f / 180.0f);
				return FVector(std::cos(PitchRad) * std::cos(YawRad), std::cos(PitchRad) * std::sin(YawRad), std::sin(PitchRad));
			};

			// 제자리 회전: 카메라 위치는 고정하고 방향만 돌린 뒤, 그 앞쪽으로 Target을 다시 잡는다
			FVector Location = OrbitTarget - CalcForward() * OrbitDistance;
			// 오빗으로 뒤집힌 상태였다면, 같은 방향을 보는 바로 선 값으로 바꾼다 (Pitch' = 180 - Pitch, Yaw' = Yaw + 180)
			// 언리얼 Fly 카메라처럼 우클릭 회전은 뒤집히지 않게 ±89로 막는다
			if (std::cos(OrbitPitch * (3.14159265f / 180.0f)) < 0.0f)
			{
				OrbitPitch = std::remainder(180.0f - OrbitPitch, 360.0f);
				OrbitYaw += 180.0f;
			}
			OrbitYaw += IO.MouseDelta.x * 0.3f;
			OrbitPitch -= IO.MouseDelta.y * 0.3f;
			OrbitPitch = OrbitPitch > 89.0f ? 89.0f : (OrbitPitch < -89.0f ? -89.0f : OrbitPitch);
			const FVector Forward = CalcForward();

			// WASD: 바라보는 방향 기준 앞뒤 / 좌우, QE: 월드 위아래 (언리얼과 동일)
			const float YawRad = OrbitYaw * (3.14159265f / 180.0f);
			const FVector Right(-std::sin(YawRad), std::cos(YawRad), 0.0f);
			const float Axis[3] = {
				(ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f),
				(ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f),
				(ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f)
			};
			// 메시 크기에 맞춰 1초에 오빗 거리만큼 이동
			const float MoveSpeed = OrbitDistance * IO.DeltaTime;
			Location = Location + (Forward * Axis[0] + Right * Axis[1] + FVector(0.0f, 0.0f, 1.0f) * Axis[2]) * MoveSpeed;

			OrbitTarget = Location + Forward * OrbitDistance;
		}
	}

	if (ImGui::IsItemHovered())
	{
		if (IO.MouseWheel != 0.0f)
		{
			// 한 칸마다 10%씩. 대상을 뚫고 지나가지 않게 최소 거리 유지
			OrbitDistance *= std::pow(0.9f, IO.MouseWheel);
			OrbitDistance = OrbitDistance < 0.1f ? 0.1f : OrbitDistance;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_F))
		{
			FocusOnMesh();
		}
	}
}

void FAnimationEditorPreviewScene::UpdateRenderTargets()
{
	if (DesiredWidth == 0 || DesiredHeight == 0)
	{
		return;
	}
	if (DesiredWidth == TargetWidth && DesiredHeight == TargetHeight)
	{
		return;
	}

	// PIE 창 타깃과 같은 구성. Fog가 깊이를 읽으므로 깊이는 TYPELESS로 만든다
	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = DesiredWidth;
	Desc.Height = DesiredHeight;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	ColorTarget = RenderCommand::CreateTexture2D(Desc);

	Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	DepthTarget = RenderCommand::CreateTexture2D(Desc);

	Desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	HDRTarget = RenderCommand::CreateTexture2D(Desc);

	RenderingInfo.ColorRenderTargets.Reset();
	RenderingInfo.ViewportSetting.Width = DesiredWidth;
	RenderingInfo.ViewportSetting.Height = DesiredHeight;
	FRenderingDesc ColorDesc{};
	ColorDesc.Texture = ColorTarget.get();
	RenderingInfo.ColorRenderTargets.Add(ColorDesc);
	RenderingInfo.DepthStencil.Texture = DepthTarget.get();

	TargetWidth = DesiredWidth;
	TargetHeight = DesiredHeight;
	ViewportClient.SetViewRect({ 0.0f, 0.0f, static_cast<float>(TargetWidth), static_cast<float>(TargetHeight) });
}

void FAnimationEditorPreviewScene::FocusOnMesh()
{
	// Bind Pose 메시 높이의 가운데를 본다. 정점이 없으면 키 1.7m 기준
	const TArray<FVertex>& Vertices = Mesh->GetMeshData().Vertices;
	float MinZ = 0.0f;
	float MaxZ = 1.7f;
	if (Vertices.Num() > 0)
	{
		MinZ = FLT_MAX;
		MaxZ = -FLT_MAX;
		for (const FVertex& Vertex : Vertices)
		{
			MinZ = Vertex.Position.Z < MinZ ? Vertex.Position.Z : MinZ;
			MaxZ = Vertex.Position.Z > MaxZ ? Vertex.Position.Z : MaxZ;
		}
	}

	const float Height = MaxZ - MinZ > 0.01f ? MaxZ - MinZ : 0.01f;
	OrbitTarget = FVector(0.0f, 0.0f, (MinZ + MaxZ) * 0.5f);
	// 임포트한 메시는 언리얼 관례대로 +Y가 정면이라 +Y 쪽에서 15도 내려다본다
	// 언리얼 SAnimationEditorViewport의 초기 카메라(EditorViewportDefs::DefaultPerspectiveViewRotation(-15, -90, 0))와 같음
	OrbitYaw = -90.0f;
	OrbitPitch = -15.0f;
	OrbitDistance = Height * 2.0f;
}

void FAnimationEditorPreviewScene::UpdatePreviewCamera()
{
	// 구면좌표 → 바라보는 방향. 카메라 위치 계산에 쓴다
	const float YawRad = OrbitYaw * (3.14159265f / 180.0f);
	const float PitchRad = OrbitPitch * (3.14159265f / 180.0f);
	const FVector Forward(std::cos(PitchRad) * std::cos(YawRad), std::cos(PitchRad) * std::sin(YawRad), std::sin(PitchRad));
	const FVector Location = OrbitTarget - Forward * OrbitDistance;

	// 이 엔진의 FRotator는 Pitch +가 아래라서 부호를 뒤집는다
	const FQuat Rotation = FRotator(-OrbitPitch, OrbitYaw, 0.0f).Quaternion();

	// 카메라 → 오빗 대상 → 원점 → 장면 끝까지 더한 값. 어떤 줌과 패닝에서도 장면에서 가장 먼 점이 이 안에 든다
	const float FarClip = OrbitDistance + OrbitTarget.Size() + SceneRadius;
	const FCameraProjection Projection{ EProjectionMode::Perspective, 60.0f, 10.0f, 0.01f, FarClip };
	ViewportClient.SetCameraOverride(Location, Forward, Projection, &Rotation);
}

void FAnimationEditorPreviewScene::CreatePreviewScene()
{
	// 언리얼 FPreviewScene처럼 프리뷰 전용 타입. 컴포넌트 Tick만 돈다
	PreviewWorld = UWorld::CreateWorld(EWorldType::EditorPreview, false);

	// -X 방향으로 40도 내려 비춘다.
	const FTransform LightTransform(FRotator(40.0f, 180.0f, 0.0f), FVector(0.0f, 0.0f, 3.0f));
	ADirectionalLightActor* DirectionalLight = PreviewWorld->SpawnActor<ADirectionalLightActor>(NAME_None, &LightTransform);
	// 그림자 맵 한 장으로는 메시 근처가 거칠어서 가까운 쪽에 해상도를 몰아준다. 이름과 달리 Directional 컴포넌트를 돌려준다
	DirectionalLight->GetPointLightComponent()->SetCSMEnabled(true);

	// 그늘진 면도 보이게 고른 환경광 (언리얼 프리뷰 씬의 Sky Light). 아이콘이 메시와 겹치지 않게 라이트와 같은 높이에 둔다
	// 기본 Intensity 1은 그늘이 거의 없어 낮춘다
	const FTransform SkyLightTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 3.0f));
	ASkyLightActor* SkyLight = PreviewWorld->SpawnActor<ASkyLightActor>(NAME_None, &SkyLightTransform);
	SkyLight->GetSkyLightComponent()->SetIntensity(0.3f);

	// 윗면이 바닥 높이에 오도록 두께 절반만큼 내린다
	const FTransform FloorTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, ComputeFloorHeight() - FloorThickness * 0.5f), FVector(FloorSize, FloorSize, FloorThickness));
	FloorActor = PreviewWorld->SpawnActor<AStaticMeshActor>(NAME_None, &FloorTransform);
	// AStaticMeshActor::SetPrimitiveType은 비어 있어서 메시를 직접 지정한다
	FloorActor->GetStaticMeshComponent()->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Cube"));
	FloorActor->GetStaticMeshComponent()->SetVisible(bShowFloor);

	// 언리얼 PersonaToolkit처럼 빈 액터를 원점에 두고 스켈레탈 메시 컴포넌트를 루트로 붙인다
	// 액터에 루트가 없으면 AddComponent가 루트로 삼는다. SetSkeletalMesh가 포즈를 Bind Pose로 초기화한다
	PreviewActor = PreviewWorld->SpawnActor<AActor>(NAME_None);
	PreviewComponent = Cast<UDebugSkelMeshComponent>(PreviewActor->AddComponent(UDebugSkelMeshComponent::StaticClass(), FName("PreviewMesh")));
	PreviewComponent->SetSkeletalMesh(Mesh);
	PreviewComponent->SetVisible(bShowMesh);
}

float FAnimationEditorPreviewScene::ComputeFloorHeight() const
{
	const TArray<FVertex>& Vertices = Mesh->GetMeshData().Vertices;
	if (Vertices.Num() == 0)
	{
		return 0.0f;
	}

	float MinZ = FLT_MAX;
	for (const FVertex& Vertex : Vertices)
	{
		MinZ = Vertex.Position.Z < MinZ ? Vertex.Position.Z : MinZ;
	}
	return MinZ;
}

float FAnimationEditorPreviewScene::ComputeSceneRadius() const
{
	// 바닥 아랫면 귀퉁이까지의 거리로 시작한다
	const float HalfFloor = FloorSize * 0.5f;
	float Radius = FVector(HalfFloor, HalfFloor, ComputeFloorHeight() - FloorThickness).Size();
	for (const FVertex& Vertex : Mesh->GetMeshData().Vertices)
	{
		const float Distance = Vertex.Position.Size();
		Radius = Distance > Radius ? Distance : Radius;
	}
	return Radius;
}
