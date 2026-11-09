#pragma once

#include "Core/EngineString.h"
#include "Math/EngineMath.h"
#include "Math/Color.h"
#include "Math/Transform.h"
#include "Math/Color.h"

class UMaterial;
class UFont;

enum class EPropertyType
{
	Unknown,
	Float,
	Int,
	String,
	Bool,
	Vector,
	Vector4,
	Color,
	LinearColor,
	Rotator,
	Transform,
	Object,
	Enum // int32 크기 enum. 값 순서대로의 표시 이름을 FProperty::EnumNames에 둔다 (UE FEnumProperty + UEnum)
};

template<typename T>
constexpr EPropertyType GetPropertyType()
{
	if constexpr (std::is_pointer_v<T> &&
		std::is_base_of_v<UObject, std::remove_pointer_t<T>>)
	{
		return EPropertyType::Object;
	}
	else
	{
		static_assert(sizeof(T) == 0, "Not Valid Type");
		return EPropertyType::Unknown;
	}
}

#define DEFINE_PROPERTY_TYPE(CppType, EnumValue)       \
	template<>                                         \
	constexpr EPropertyType GetPropertyType<CppType>() \
	{                                                  \
		return EPropertyType::EnumValue;               \
	}

DEFINE_PROPERTY_TYPE(int, Int)
DEFINE_PROPERTY_TYPE(uint32, Int)
DEFINE_PROPERTY_TYPE(float, Float)
DEFINE_PROPERTY_TYPE(bool, Bool)
DEFINE_PROPERTY_TYPE(FString, String)
DEFINE_PROPERTY_TYPE(FVector, Vector)
DEFINE_PROPERTY_TYPE(FVector4, Vector4)
DEFINE_PROPERTY_TYPE(FTransform, Transform)
DEFINE_PROPERTY_TYPE(FRotator, Rotator)
DEFINE_PROPERTY_TYPE(FColor, Color)
DEFINE_PROPERTY_TYPE(FLinearColor, LinearColor)

// 프로퍼티 지정자 (UE의 UPROPERTY 지정자에 대응)
enum EPropertyFlags : uint32
{
	CPF_None = 0,
	CPF_Edit = 1 << 0,      // 디테일 패널에 노출
	CPF_EditConst = 1 << 1, // 노출하되 수정 불가
	CPF_Transient = 1 << 2, // 직렬화에서 제외

	CPF_EditAnywhere = CPF_Edit,
	CPF_VisibleAnywhere = CPF_Edit | CPF_EditConst,
	CPF_Default = CPF_EditAnywhere,
};

struct FProperty
{
	// TODO : FName으로 변경해서 속도올리는 방안 필요.
	FString Name;
	EPropertyType Type;
	size_t Offset;
	size_t Size;
	UClass* Class = nullptr;
	uint32 Flags = CPF_Default;
	// Enum 타입만 쓴다. 인덱스가 enum 값이다. 저장도 이 이름으로 한다 (UE도 enum을 이름으로 저장)
	TArray<FString> EnumNames;

	bool HasAnyFlags(uint32 InFlags) const { return (Flags & InFlags) != 0; }

	// 이름에 맞는 enum 값. 없으면 INDEX_NONE (UE UEnum::GetValueByName)
	int32 GetEnumValueByName(const FString& InName) const
	{
		for (int32 Index = 0; Index < EnumNames.Num(); ++Index)
		{
			if (EnumNames[Index] == InName)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	// 값에 맞는 이름. 범위 밖이면 빈 문자열 (UE UEnum::GetNameStringByValue)
	FString GetEnumNameStringByValue(int32 Value) const
	{
		return Value >= 0 && Value < EnumNames.Num() ? EnumNames[Value] : FString();
	}
};