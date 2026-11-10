#pragma once

#include "GameFramework/Actor.h"
#include "SequencePlayer.h"

class UAnimationAsset;
class USkeletalMeshComponent;
class UCameraComponent;

class ASequenceActor : public AActor
{
	DECLARE_CLASS(ASequenceActor, AActor)
	REFLECT_START(ASequenceActor)
		PROPERTY(bAutoPlay)
	REFLECT_END()

public:
	ASequenceActor();
	void BeginPlay() override;
	void Tick(float DeltaTime) override;
	void EndPlay() override;
	using Super::Serialize;
	void Serialize(FStructuredArchive::FRecord Record) override;

	FSequencePlayer& GetSequencePlayer() { return SequencePlayer; }
	const FSequencePlayer& GetSequencePlayer() const { return SequencePlayer; }
	void Play();
	void Pause() { SequencePlayer.Pause(); }
	void Stop();
	// 해당 게임 월드에서 재생을 시작한 Sequence의 현재 Fade 값. Stop 시에는 제외한다.
	static float GetWorldFadeAlpha(UWorld* World);
	static void NotifyActorRenamed(const AActor* Renamed, const FString& OldName);
	static void NotifyComponentRenamed(const UActorComponent* Renamed, const FString& OldName);

	void ApplyAnimationTracks(bool bFireNotifies);
	void RestoreAnimatedComponents();
	static void RestoreAllAnimatedComponents(const UWorld* World);

	bool bAutoPlay = false;

private:
	struct FAnimatedComponentState
	{
		uint32 ComponentUUID = 0;
		bool bWasSingleNode = false;
		UAnimationAsset* Asset = nullptr;
		float Position = 0.0f;
		float PlayRate = 1.0f;
		bool bPlaying = false;
		bool bLooping = true;
	};

	void ApplyCameraView();
	void AcquireCamera(UCameraComponent* Camera);
	void ReleaseCameraView();

	void CaptureAnimatedComponent(USkeletalMeshComponent& Component);
	void RestoreAnimatedComponentAt(int32 Index);
	bool RestoreAnimatedComponent(uint32 ComponentUUID);

	FSequencePlayer SequencePlayer;
	bool bCameraPlaybackActive = false;
	uint32 ControlledCameraUUID = 0;
	bool bSavedCameraTickEnabled = false;

	TArray<FAnimatedComponentState> AnimatedComponents;
};
