#include "EnginePCH.h"
#include "SequenceActor.h"
#include "Components/SceneComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "Engine/Level.h"

ASequenceActor::ASequenceActor()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>("SequenceRoot"));
	PrimaryActorTick.bCanEverTick = true;
	// 일반 이동/카메라 입력 이후, World의 CameraManager 갱신 이전에 최종 위치를 기록한다.
	PrimaryActorTick.TickGroup = ETickingGroup::PostUpdateWork;
}

float ASequenceActor::GetWorldFadeAlpha(UWorld* World)
{
	if (!World || (World->GetWorldType() != EWorldType::Game && World->GetWorldType() != EWorldType::PIE))
	{
		return 0.0f;
	}
	float FadeAlpha = 0.0f;
	for (AActor* Actor : World->GetPersistentLevel()->GetActors())
	{
		auto* Sequence = Cast<ASequenceActor>(Actor);
		if (!Sequence || Sequence->IsActorBeingDestroyed() || Sequence->SequencePlayer.IsStopped())
		{
			continue;
		}
		float CurrentAlpha = 0.0f;
		if (Sequence->SequencePlayer.GetCurrentFadeAlpha(CurrentAlpha))
		{
			// 동시 재생 중인 Fade가 여러 개면 더 어두운 값을 사용한다. Pause 시에는 현재 값을 유지한다.
			FadeAlpha = FMath::Max(FadeAlpha, CurrentAlpha);
		}
	}
	return FadeAlpha;
}

void ASequenceActor::BeginPlay() // bAutoPlay true 시 시작할때 Sequence가 시작됨
{
	Super::BeginPlay();
	Stop();
	if (bAutoPlay)
	{
		Play();
	}
}

void ASequenceActor::Tick(float DeltaTime)
{
	const bool bWasPlaying = SequencePlayer.IsPlaying();
	SequencePlayer.Tick(DeltaTime);
	if (SequencePlayer.IsStopped() || (SequencePlayer.HasFinished() && !bWasPlaying))
	{
		ReleaseCameraView();
		return;
	}

	// Player를 직접 Play한 경우도 지원한다. 시간은 Actor에서 한 번만 진행시킨다.
	if (bWasPlaying || bCameraPlaybackActive)
	{
		ApplyCameraView();
	}
	// 끝 프레임의 값을 적용한 뒤 다음 Tick에서 제어만 해제한다. 카메라 배치는 유지한다.
}

void ASequenceActor::Play()
{
	SequencePlayer.Play();
	ApplyCameraView();
}

void ASequenceActor::Stop()
{
	SequencePlayer.Stop();
	ReleaseCameraView();
}

void ASequenceActor::ApplyCameraView()
{
	UWorld* World = GetWorld();
	// 에디터 패널에서의 재생은 기존처럼 시간/값 확인용이다. 게임 월드의 카메라에만 전달한다.
	if (!World || (World->GetWorldType() != EWorldType::Game && World->GetWorldType() != EWorldType::PIE))
	{
		return;
	}
	CameraKey Key{};
	const bool bHasCamera = SequencePlayer.GetCurrentCameraKey(Key);
	if (!bHasCamera)
	{
		// 아직 카메라 구간이 없으면 제어만 해제한다. Fade만 있는 연출은 카메라를 건드리지 않는다.
		ReleaseCameraView();
		return;
	}
	APlayerCameraManager* Manager = World->GetPlayerCameraManager();
	UCameraComponent* Camera = Manager ? Manager->GetViewTargetCamera() : nullptr;
	if (!Camera || !Camera->IsRegistered() || !Camera->GetOwner() || Camera->GetOwner()->IsActorBeingDestroyed())
	{
		ReleaseCameraView();
		return;
	}
	if (ControlledCameraUUID != Camera->GetUUID())
	{
		ReleaseCameraView();
		AcquireCamera(Camera);
	}
	// 시퀀스가 제어하는 동안 카메라 컴포넌트의 Tick을 멈춘다. 해제할 때 원래 상태로 되돌린다.
	Camera->PrimaryComponentTick.SetTickFunctionEnable(false);
	// 기록된 키는 월드 좌표이므로 부모가 있는 카메라도 월드 좌표 설정 함수를 사용한다.
	Camera->SetWorldLocationAndRotation(Key.Location, Key.Rotation, false);
	Camera->SetFieldOfView(Key.FieldOfView);
}

void ASequenceActor::AcquireCamera(UCameraComponent* Camera)
{
	// 같은 카메라를 두 Sequence가 동시에 쓰지 않게 한다. 이전 연출의 제어를 해제한다.
	for (AActor* Actor : GetWorld()->GetPersistentLevel()->GetActors())
	{
		if (auto* Other = Cast<ASequenceActor>(Actor); Other && Other != this && Other->ControlledCameraUUID == Camera->GetUUID())
		{
			Other->Stop();
		}
	}
	ControlledCameraUUID = Camera->GetUUID();
	bSavedCameraTickEnabled = Camera->PrimaryComponentTick.IsTickFunctionRegistered()
		? Camera->PrimaryComponentTick.IsTickFunctionEnabled()
		: Camera->PrimaryComponentTick.bStartWithTickEnabled;
	bCameraPlaybackActive = true;
}

void ASequenceActor::ReleaseCameraView()
{
	// 카메라가 삭제되거나 씬이 바뀌었을 수 있으므로 보관한 포인터를 역참조하지 않고 식별자로 찾는다.
	if (ControlledCameraUUID)
	{
		for (UObject* Object : GUObjectArray)
		{
			if (Object && Object->GetUUID() == ControlledCameraUUID)
			{
				if (auto* Camera = Cast<UCameraComponent>(Object))
				{
					// 마지막 위치/회전/FOV는 그대로 두고 Tick 상태만 복원한다.
					Camera->PrimaryComponentTick.SetTickFunctionEnable(bSavedCameraTickEnabled);
				}
				break;
			}
		}
	}
	ControlledCameraUUID = 0;
	bCameraPlaybackActive = false;
}

void ASequenceActor::EndPlay()
{
	Stop();
	Super::EndPlay();
}

void ASequenceActor::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	FSequenceData Data = SequencePlayer.GetSequenceData();
	Data.Serialize(Record.EnterRecord("Sequence"));
	if (Record.GetUnderlyingArchive().IsLoading() && !Record.GetUnderlyingArchive().IsError())
	{
		ReleaseCameraView();
		SequencePlayer.SetSequenceData(Data);
	}
}
