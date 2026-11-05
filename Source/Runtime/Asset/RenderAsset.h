#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"

class URenderAsset : public UObject
{
	DECLARE_CLASS(URenderAsset, UObject)
public:
	URenderAsset() = default;
	virtual ~URenderAsset() = default;

	void SetPath(FString Path) { AssetPath = Path; }
	const FString& GetPath() const { return AssetPath; }
	bool IsRetired() const { return bRetired; }
	void SetRetired(bool Value) { bRetired = Value; }

private:
	FString AssetPath;
	// 세션 검색 상태다. 기존 컴포넌트가 보관한 포인터와 렌더링은 유효하다.
	bool bRetired = false;
};
