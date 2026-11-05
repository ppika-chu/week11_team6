#pragma once
#include "FbxInfo.h"

// 원본 파싱/Cook만 담당한다. 런타임 객체 생성·파일 반영은 FBX 애셋 로더가 담당한다.
class FFbxImporter
{
public:
	static bool IsAvailable();
	static bool Cook(const FString& Source, const FFbxImportOptions& Options, const FString& ExtractionDirectory,
		TArray<FFbxAssetData>& Out, TArray<FString>& Warnings, FString& Error, const FString& OutputDirectory = {});
};
