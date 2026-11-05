#pragma once
#include "FbxInfo.h"

class URenderAsset;

// 소스 해석은 FFbxImporter, 바이너리/런타임 애셋 생성과 교체는 이 로더가 담당한다.
class FFbxAssetLoader
{
public:
	static URenderAsset* LoadAsset(const FString& Path, FString& Error);
	static FFbxImportResult Import(const FString& SourcePath, const FFbxImportOptions& Options, const FString& OutputDirectory = {});
private:
	static URenderAsset* CreateTexture(const FImageData& Image, FString& Error);
};
