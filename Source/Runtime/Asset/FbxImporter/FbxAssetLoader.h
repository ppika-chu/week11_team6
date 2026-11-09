#pragma once
#include "FbxInfo.h"

class URenderAsset;
class UAnimSequenceBase;

// 소스 해석은 FFbxImporter, 바이너리/런타임 애셋 생성과 교체는 이 로더가 담당한다.
class FFbxAssetLoader
{
public:
	// Viewer에서 호출한다. 기존 Track과 출처 정보는 유지하고 Sequence의 편집 데이터를 저장한다.
	static bool SaveAnimationAsset(UAnimSequenceBase* Sequence, FString& Error);
	static URenderAsset* LoadAsset(const FString& Path, FString& Error);
	static FFbxImportResult Import(const FString& SourcePath, const FFbxImportOptions& Options, const FString& OutputDirectory = {});
private:
	static URenderAsset* CreateTexture(const FImageData& Image, FString& Error);
};
