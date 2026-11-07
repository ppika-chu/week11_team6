#pragma once

enum class ESkinningMode : uint8
{
	GPU,
	CPU,
};

class FSkinningSettings
{
public:
	static bool ExecCommand(const FString& CommandLine, FString& OutMessage);

	static ESkinningMode GetSkinningMode() { return SkinningMode; }
	static void SetSkinningMode(ESkinningMode NewMode);

private:
	inline static ESkinningMode SkinningMode = ESkinningMode::GPU;
};
