#include "EnginePCH.h"
#include "Audio/AudioSystem.h"

#include <miniaudio.h>

namespace
{
std::wstring ToWide(const FString& Utf8)
{
	if (Utf8.empty())
	{
		return {};
	}

	const int32 Length = MultiByteToWideChar(CP_UTF8, 0, Utf8.data(), static_cast<int32>(Utf8.size()), nullptr, 0);
	std::wstring Wide(static_cast<size_t>(Length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, Utf8.data(), static_cast<int32>(Utf8.size()), Wide.data(), Length);
	return Wide;
}
}

FAudioSystem::FAudioSystem() = default;

FAudioSystem::~FAudioSystem()
{
	Shutdown();
}

bool FAudioSystem::Initialize()
{
	if (Engine)
	{
		return true;
	}

	Engine = MakeUnique<ma_engine>();

	const ma_result Result = ma_engine_init(nullptr, Engine.get());
	if (Result != MA_SUCCESS)
	{
		HTR_LOG(Error, "[Audio] Engine initialization failed: {}", ma_result_description(Result));
		Engine.reset();
		return false;
	}

	HTR_LOG(Info, "[Audio] miniaudio {} initialized", MA_VERSION_STRING);
	return true;
}

void FAudioSystem::Shutdown()
{
	if (!Engine)
	{
		return;
	}

	StopAll();
	ma_engine_uninit(Engine.get());
	Engine.reset();
}

void FAudioSystem::Tick(float DeltaTime)
{
	if (!Engine)
	{
		return;
	}

	TArray<uint32> Finished;

	for (auto& [Handle, Entry] : Sounds)
	{
		ma_sound* Sound = Entry.Sound.get();

		if (Entry.bFading)
		{
			Entry.FadeElapsed += DeltaTime;
			const float Alpha = Entry.FadeDuration > 0.0f ? std::min(Entry.FadeElapsed / Entry.FadeDuration, 1.0f) : 1.0f;
			ma_sound_set_volume(Sound, Entry.FadeStartVolume + (Entry.FadeTargetVolume - Entry.FadeStartVolume) * Alpha);
			Entry.bFading = Alpha < 1.0f;
		}

		if (ma_sound_at_end(Sound))
		{
			Finished.Add(Handle);
		}
	}

	for (uint32 Handle : Finished)
	{
		Stop(Handle);
	}
}

uint32 FAudioSystem::Play(const FString& Path, bool bLoop, float Volume)
{
	if (!Engine)
	{
		return 0;
	}

	FSoundEntry Entry;
	Entry.Sound = MakeUnique<ma_sound>();

	const ma_uint32 Flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION;
	const std::wstring WidePath = ToWide(Path);

	const ma_result Result = ma_sound_init_from_file_w(Engine.get(), WidePath.c_str(), Flags, nullptr, nullptr, Entry.Sound.get());
	if (Result != MA_SUCCESS)
	{
		HTR_LOG(Error, "[Audio] Cannot load '{}': {}", Path, ma_result_description(Result));
		return 0;
	}

	ma_sound_set_looping(Entry.Sound.get(), bLoop ? MA_TRUE : MA_FALSE);
	ma_sound_set_volume(Entry.Sound.get(), Volume);
	ma_sound_start(Entry.Sound.get());

	const uint32 Handle = ++NextHandle;
	Sounds[Handle] = std::move(Entry);
	return Handle;
}

void FAudioSystem::Stop(uint32 Handle)
{
	FSoundEntry* Entry = FindEntry(Handle);
	if (!Entry)
	{
		return;
	}

	ReleaseSound(*Entry);
	Sounds.Remove(Handle);
}

void FAudioSystem::StopAll()
{
	for (auto& [Handle, Entry] : Sounds)
	{
		ReleaseSound(Entry);
	}

	Sounds.Reset();
}

void FAudioSystem::SetVolume(uint32 Handle, float Volume)
{
	FSoundEntry* Entry = FindEntry(Handle);
	if (!Entry)
	{
		return;
	}

	Entry->bFading = false;
	ma_sound_set_volume(Entry->Sound.get(), std::max(Volume, 0.0f));
}

void FAudioSystem::SetPitch(uint32 Handle, float Pitch)
{
	FSoundEntry* Entry = FindEntry(Handle);
	if (!Entry)
	{
		return;
	}

	ma_sound_set_pitch(Entry->Sound.get(), std::max(Pitch, 0.01f));
}

void FAudioSystem::FadeTo(uint32 Handle, float TargetVolume, float Duration)
{
	FSoundEntry* Entry = FindEntry(Handle);
	if (!Entry)
	{
		return;
	}

	Entry->FadeStartVolume = ma_sound_get_volume(Entry->Sound.get());
	Entry->FadeTargetVolume = std::max(TargetVolume, 0.0f);
	Entry->FadeDuration = std::max(Duration, 0.0f);
	Entry->FadeElapsed = 0.0f;
	Entry->bFading = true;
}

bool FAudioSystem::IsPlaying(uint32 Handle) const
{
	const FSoundEntry* Entry = FindEntry(Handle);
	return Entry && ma_sound_is_playing(Entry->Sound.get());
}

void FAudioSystem::SetMasterVolume(float Volume)
{
	if (!Engine)
	{
		return;
	}

	ma_engine_set_volume(Engine.get(), std::max(Volume, 0.0f));
}

FAudioSystem::FSoundEntry* FAudioSystem::FindEntry(uint32 Handle)
{
	return Sounds.Find(Handle);
}

const FAudioSystem::FSoundEntry* FAudioSystem::FindEntry(uint32 Handle) const
{
	return Sounds.Find(Handle);
}

void FAudioSystem::ReleaseSound(FSoundEntry& Entry)
{
	if (!Entry.Sound)
	{
		return;
	}

	ma_sound_uninit(Entry.Sound.get());
	Entry.Sound.reset();
}
