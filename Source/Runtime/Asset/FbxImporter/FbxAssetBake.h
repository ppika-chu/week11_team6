#pragma once
#include "FbxInfo.h"

class FFbxAssetBake
{
public:
	static bool ReadBaked(const FString& Path, FFbxAssetData& Out, FString& Error);
	static bool WriteBaked(const FString& Path, const FFbxAssetData& Data, FString& Error);
	// 같은 출처의 Animation을 재임포트할 때 저장된 편집 데이터만 새 Cook 결과에 합친다.
	static bool PreserveAnimationNotifies(const FString& Path, FFbxAssetData& Data, FString& Error);
	// 재임포트 이름 판정은 페이로드 버전과 독립적인 공통 출처 필드만 읽는다.
	static bool ReadSourceIdentity(const FString& Path, FString& Source, FString& Object);
};
