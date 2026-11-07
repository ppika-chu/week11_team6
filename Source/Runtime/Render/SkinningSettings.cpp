#include "EnginePCH.h"
#include "SkinningSettings.h"

#include "Components/SkinnedMeshComponent.h"
#include "UObject/UObjectIterator.h"

namespace
{
bool EqualsIgnoreCase(const FString& A, const char* B)
{
	uint64 Index = 0;
	for (; Index < A.size() && B[Index] != '\0'; ++Index)
	{
		const int Left = std::tolower(static_cast<unsigned char>(A[Index]));
		const int Right = std::tolower(static_cast<unsigned char>(B[Index]));
		if (Left != Right)
		{
			return false;
		}
	}
	return Index == A.size() && B[Index] == '\0';
}

TArray<FString> Tokenize(const FString& Line)
{
	TArray<FString> Result;

	uint64 Cursor = 0;
	while (Cursor < Line.size())
	{
		while (Cursor < Line.size() && std::isspace(static_cast<unsigned char>(Line[Cursor])))
		{
			++Cursor;
		}
		const uint64 Start = Cursor;
		while (Cursor < Line.size() && !std::isspace(static_cast<unsigned char>(Line[Cursor])))
		{
			++Cursor;
		}
		if (Cursor > Start)
		{
			Result.Add(Line.substr(Start, Cursor - Start));
		}
	}

	return Result;
}
}

bool FSkinningSettings::ExecCommand(const FString& CommandLine, FString& OutMessage)
{
	const TArray<FString> Tokens = Tokenize(CommandLine);
	if (Tokens.IsEmpty())
	{
		return false;
	}

	// skinning [gpu/cpu]
	if (EqualsIgnoreCase(Tokens[0], "skinning"))
	{
		if (Tokens.Num() < 2)
		{
			SetSkinningMode(SkinningMode == ESkinningMode::GPU ? ESkinningMode::CPU : ESkinningMode::GPU);
		}
		else if (EqualsIgnoreCase(Tokens[1], "gpu"))
		{
			SetSkinningMode(ESkinningMode::GPU);
		}
		else if (EqualsIgnoreCase(Tokens[1], "cpu"))
		{
			SetSkinningMode(ESkinningMode::CPU);
		}
		else
		{
			OutMessage = std::format("Usage: skinning [gpu/cpu]");
		}
		return true;
	}
	
	return false;
}

void FSkinningSettings::SetSkinningMode(ESkinningMode NewMode)
{
	if (SkinningMode == NewMode)
	{
		return;
	}

	SkinningMode = NewMode;
	for (TObjectIterator<USkinnedMeshComponent> It; It; ++It)
	{
		It->RecreateRenderState();
	}
}
