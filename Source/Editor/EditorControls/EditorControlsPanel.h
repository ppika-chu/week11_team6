#pragma once
#include "Editor/EditorUI/EditorPanel.h"

#include "Editor/Gizmo/Gizmo.h"

#include "GameFramework/Actor/ParticleActor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/Actor/LightActorBase.h"
#include "GameFramework/Actor/TextRenderActor.h"
#include "GameFramework/Actor/RectLightActor.h"
#include "GameFramework/Actor/SpotLightActor.h"
#include "GameFramework/Actor/PointLightActor.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "GameFramework/Actor/ExponentialHeightFogActor.h"
#include "GameFramework/Actor/BoxActor.h"
#include "GameFramework/Actor/SphereActor.h"
#include "GameFramework/Actor/CapsuleActor.h"
#include "GameFramework/Actor/DecalActor.h"
#include "GameFramework/Actor/FakeSpotLightActor.h"
#include "GameFramework/Actor/SkeletalMeshActor.h"
#include "Sequence/SequenceActor.h"

class FViewportsPanel;

// 액터 생성과 카메라·기즈모 편집에 필요한 패널 상태를 보관한다.
class FEditorControlsPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Editor Controls"; }
	inline void SetGizmo(FGizmo* InGizmo) { Gizmo = InGizmo; }
	inline void SetWorld(UWorld* InWorld) { World = InWorld; }
	inline void SetIsPlayingQuery(std::function<bool()> InQuery) { IsPlayingQuery = InQuery; }
	// 다른 창(뷰어)이 키보드를 쓰는 중인지. true면 Space로 기즈모 모드를 바꾸지 않는다
	inline void SetKeyboardBlockedQuery(std::function<bool()> InQuery) { KeyboardBlockedQuery = InQuery; }

	float DeltaTime = 1.0f;
	UWorld* World = nullptr; // SpawnActor 대상 월드

	void AddActor(uint32 Index);

	int32 SelectedIndex = 0;

	// 아래 Classes와 같은 순서여야 한다. 고른 인덱스로 Classes[Index]를 스폰한다.
	const char* Items[18] = {
		"Actor",
		"Box",
		"Capsule",
		"Decal",
		"DirectionalLight",
		"ExponentialHeightFog",
		"FakeSpotLight",
		"Particle",
		"Pawn",
		"PointLight",
		"RectLight",
		"Sequence",
		"SkyLight",
		"Sphere",
		"SpotLight",
		"StaticMesh",
		"SkeletalMesh",
		"Text"
	};

	FGizmo* Gizmo = nullptr;
	int32 GizmoSelectedIndex = 0;
	const char* GizmoItems[3] = { "Location", "Rotation", "Scale" };

	int32 SpaceSelectedIndex = 0;
	const char* SpaceItems[2] = { "Local", "World" };

	TArray<UClass*> Classes{
		AActor::StaticClass(),
		ABoxActor::StaticClass(),
		ACapsuleActor::StaticClass(),
		ADecalActor::StaticClass(),
		ADirectionalLightActor::StaticClass(),
		AExponentialHeightFogActor::StaticClass(),
		AFakeSpotLightActor::StaticClass(),
		AParticleActor::StaticClass(),
		APawn::StaticClass(),
		APointLightActor::StaticClass(),
		ARectLightActor::StaticClass(),
		ASequenceActor::StaticClass(),
		ASkyLightActor::StaticClass(),
		ASphereActor::StaticClass(),
		ASpotLightActor::StaticClass(),
		AStaticMeshActor::StaticClass(),
		ASkeletalMeshActor::StaticClass(),
		ATextRenderActor::StaticClass()
	};

	void SetViewportsPanel(FViewportsPanel* InPanel) { ViewportsPanel = InPanel; }

private:
	static constexpr float SectionGap = 10.0f;
	static constexpr float SubsectionGap = 4.0f;

	void DrawCameraProperties();
	FViewportsPanel* ViewportsPanel = nullptr;
	std::function<bool()> IsPlayingQuery;
	std::function<bool()> KeyboardBlockedQuery;
};
