#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"
#include "Containers/Map.h"

struct ma_engine;
struct ma_sound;

class FAudioSystem final
{
public:
	FAudioSystem();
	~FAudioSystem();

	FAudioSystem(const FAudioSystem&) = delete;
	FAudioSystem& operator=(const FAudioSystem&) = delete;

	bool Initialize();
	void Shutdown();
	void Tick(float DeltaTime);

	bool IsInitialized() const
	{
		return Engine != nullptr;
	}

	uint32 Play(const FString& Path, bool bLoop = false, float Volume = 1.0f);
	void Stop(uint32 Handle);
	void StopAll();

	void SetVolume(uint32 Handle, float Volume);
	void SetPitch(uint32 Handle, float Pitch);
	void FadeTo(uint32 Handle, float TargetVolume, float Duration);
	bool IsPlaying(uint32 Handle) const;

	void SetMasterVolume(float Volume);

private:
	struct FSoundEntry
	{
		TUniquePtr<ma_sound> Sound;
		float FadeStartVolume = 0.0f;
		float FadeTargetVolume = 0.0f;
		float FadeDuration = 0.0f;
		float FadeElapsed = 0.0f;
		bool bFading = false;
	};

	FSoundEntry* FindEntry(uint32 Handle);
	const FSoundEntry* FindEntry(uint32 Handle) const;
	static void ReleaseSound(FSoundEntry& Entry);

	TUniquePtr<ma_engine> Engine;
	TMap<uint32, FSoundEntry> Sounds;
	uint32 NextHandle = 0;
};
