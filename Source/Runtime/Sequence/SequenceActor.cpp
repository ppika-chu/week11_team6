#include "EnginePCH.h"
#include "SequenceActor.h"

#include "SequenceBinding.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SceneComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/Level.h"

namespace
{
template<typename Func>
void ForEachSequenceInLevel(const ULevel* Level, Func&& Action)
{
	if (!Level)
	{
		return;
	}
	for (AActor* Actor : Level->GetActors())
	{
		if (auto* Sequence = Cast<ASequenceActor>(Actor))
		{
			Action(*Sequence);
		}
	}
}

void ApplyAnimationSample(USkeletalMeshComponent& Component, const FAnimationSample& Sample, bool bFireNotifies)
{
	USkeletalMesh* Mesh = Component.GetSkeletalMesh();
	if (!Mesh || Sample.Animation->GetSkeleton() != Mesh->Skeleton)
	{
		return;
	}

	if (Component.GetAnimationMode() != EAnimationMode::AnimationSingleNode)
	{
		// TODO
		Component.SetAnimationMode(EAnimationMode::AnimationSingleNode);
	}

	UAnimSingleNodeInstance* Instance = Component.GetSingleNodeInstance();
	if (!Instance)
	{
		return;
	}

	if (Instance->GetCurrentAsset() != Sample.Animation)
	{
		Component.SetAnimation(Sample.Animation);
	}

	Instance->SetPlaying(false);
	Component.SetPosition(Sample.Time, bFireNotifies);
	Component.RefreshBoneTransforms();
}

USkeletalMeshComponent* FindAnimatedComponent(const ULevel* Level, uint32 UUID)
{
	if (!Level || UUID)
	{
		return nullptr;
	}

	for (AActor* Actor : Level->GetActors())
	{
		if (!Actor || Actor->IsActorBeingDestroyed())
		{
			continue;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component && Component->GetUUID() == UUID)
			{
				auto* SkeletalMesh = Cast<USkeletalMeshComponent>(Component);
				return SkeletalMesh && !SkeletalMesh->IsBeingDestroyed() ? SkeletalMesh : nullptr;
			}
		}
	}

	return nullptr;
}

bool CanApplySample(const USkeletalMeshComponent& Component, const FAnimationSample& Sample)
{
	const USkeletalMesh* Mesh = Component.GetSkeletalMesh();
	return Mesh && Component.IsRegistered() && Sample.Animation->GetSkeleton() == Mesh->Skeleton;
}
}

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

void ASequenceActor::NotifyActorRenamed(const AActor* Renamed, const FString& OldName)
{
	if (!Renamed)
	{
		return;
	}
	const FString NewName = Renamed->GetName();
	ForEachSequenceInLevel(Renamed->GetLevel(), [&](ASequenceActor& Sequence)
	{
		Sequence.SequencePlayer.GetSequenceData().ForEachBinding([&](FSequenceBinding& Binding)
		{
			if (Binding.ActorName == OldName)
			{
				Binding.ActorName = NewName;
			}
		});
	});
}

void ASequenceActor::NotifyComponentRenamed(const UActorComponent* Renamed, const FString& OldName)
{
	const AActor* Owner = Renamed ? Renamed->GetOwner() : nullptr;
	if (!Owner)
	{
		return;
	}
	const FString OwnerName = Owner->GetName();
	const FString NewName = Renamed->GetName();
	ForEachSequenceInLevel(Owner->GetLevel(), [&](ASequenceActor& Sequence)
	{ 
		Sequence.SequencePlayer.GetSequenceData().ForEachBinding([&](FSequenceBinding& Binding)
		{
			if (Binding.ActorName == OwnerName && Binding.ComponentName == OldName)
			{
				Binding.ComponentName = NewName;
			}
		}); 
	});
}

void ASequenceActor::ApplyAnimationTracks(bool bFireNotifies)
{
	UWorld* World = GetWorld();
	if (!World || World->GetWorldType() == EWorldType::EditorPreview)
	{
		return;
	}

	if (World->GetWorldType() == EWorldType::Editor)
	{
		bFireNotifies = false;
	}

	TArray<uint32> AppliedUUIDs;
	for (const auto& Row : SequencePlayer.GetSequenceData().Rows)
	{
		const auto* Track = std::get_if<FAnimationTimelineTrack>(&Row.Rows);
		FAnimationSample Sample;
		if (!Track || !SequencePlayer.EvaluateAnimationTrack(*Track, Sample))
		{
			continue;
		}

		USkeletalMeshComponent* Component = SequenceBinding::ResolveComponent(Track->Binding, GetLevel());
		if (!Component || !CanApplySample(*Component, Sample))
		{
			continue;
		}

		CaptureAnimatedComponent(*Component);
		ApplyAnimationSample(*Component, Sample, bFireNotifies);
		AppliedUUIDs.Add(Component->GetUUID());
	}

	for (int32 Index = AnimatedComponents.Num() - 1; Index >= 0; --Index)
	{
		if (AppliedUUIDs.Find(AnimatedComponents[Index].ComponentUUID) == -1)
		{
			RestoreAnimatedComponentAt(Index);
		}
	}
}

void ASequenceActor::RestoreAnimatedComponents()
{
	// 기록 순서의 역순
	for (int32 Index = AnimatedComponents.Num() - 1; Index >= 0; --Index)
	{
		RestoreAnimatedComponentAt(Index);
	}
}

void ASequenceActor::RestoreAllAnimatedComponents(const UWorld* World)
{
	if (World)
	{
		ForEachSequenceInLevel(World->GetPersistentLevel(), [](ASequenceActor& Sequence)
		{
			Sequence.RestoreAnimatedComponents();
		});
	}
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
		RestoreAnimatedComponents();
		return;
	}

	// Player를 직접 Play한 경우도 지원한다. 시간은 Actor에서 한 번만 진행시킨다.
	if (bWasPlaying || bCameraPlaybackActive)
	{
		ApplyCameraView();
	}
	// 끝 프레임의 값을 적용한 뒤 다음 Tick에서 제어만 해제한다. 카메라 배치는 유지한다.

	if (bWasPlaying)
	{
		ApplyAnimationTracks(true);
	}
}

void ASequenceActor::Play()
{
	SequencePlayer.Play();
	ApplyCameraView();
	ApplyAnimationTracks(false);
}

void ASequenceActor::Stop()
{
	SequencePlayer.Stop();
	ReleaseCameraView();
	RestoreAnimatedComponents();
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

void ASequenceActor::CaptureAnimatedComponent(USkeletalMeshComponent& Component)
{
	const uint32 UUID = Component.GetUUID();
	for (const auto& State : AnimatedComponents)
	{
		if (State.ComponentUUID == UUID)
		{
			return;
		}
	}

	ForEachSequenceInLevel(GetLevel(), [&](ASequenceActor& Other)
	{
		if (&Other != this)
		{
			Other.RestoreAnimatedComponent(UUID);
		} 
	});

	FAnimatedComponentState State;
	State.ComponentUUID = UUID;
	State.bWasSingleNode = Component.GetAnimationMode() == EAnimationMode::AnimationSingleNode;
	if (const UAnimSingleNodeInstance* Instance = Component.GetSingleNodeInstance())
	{
		State.Asset = Instance->GetCurrentAsset();
		State.Position = Instance->GetCurrentTime();
		State.PlayRate = Instance->GetPlayRate();
		State.bPlaying = Instance->IsPlaying();
		State.bLooping = Instance->IsLooping();
	}
	AnimatedComponents.Add(State);
}

void ASequenceActor::RestoreAnimatedComponentAt(int32 Index)
{
	const FAnimatedComponentState State = AnimatedComponents[Index];
	AnimatedComponents.RemoveAt(Index, 1);

	USkeletalMeshComponent* Component = FindAnimatedComponent(GetLevel(), State.ComponentUUID);
	if (!Component)
	{
		return;
	}

	if (!State.bWasSingleNode)
	{
		Component->SetAnimationMode(EAnimationMode::AnimationClass);
	}
	else if (UAnimSingleNodeInstance* Instance = Component->GetSingleNodeInstance())
	{
		Instance->SetAnimationAsset(State.Asset, State.bLooping, State.PlayRate);
		Instance->SetPosition(State.Position, false);
		Instance->SetPlaying(State.bPlaying);
	}

	if (Component->GetAnimInstance())
	{
		Component->RefreshBoneTransforms();
	}
	else
	{
		Component->ResetToRefPose();
	}
}

bool ASequenceActor::RestoreAnimatedComponent(uint32 ComponentUUID)
{
	for (int32 Index = 0; Index < AnimatedComponents.Num(); ++Index)
	{
		if (AnimatedComponents[Index].ComponentUUID == ComponentUUID)
		{
			RestoreAnimatedComponentAt(Index);
			return true;
		}
	}

	return false;
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
