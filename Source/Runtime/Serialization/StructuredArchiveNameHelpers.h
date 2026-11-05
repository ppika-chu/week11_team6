#pragma once

struct FArchiveFieldName
{
	const char* Name;

	FArchiveFieldName(const char* InName)
		: Name(InName)
	{
	}
};

template<typename T>
struct TNamedValue
{
	FArchiveFieldName Name;
	T& Value;
};

template<typename T>
struct TNamedAttribute
{
	FArchiveFieldName Name;
	T& Value;
};

template<typename T>
TNamedValue<T> MakeNamedValue(FArchiveFieldName Name, T& Value)
{
	return TNamedValue<T>{ Name, Value };
}

template<typename T>
TNamedAttribute<T> MakeNamedAttribute(FArchiveFieldName Name, T& Value)
{
	return TNamedAttribute<T>{ Name, Value };
}

#define SA_VALUE(Name, Value) MakeNamedValue(FArchiveFieldName(Name), Value)

#define SA_ATTRIBUTE(Name, Value) MakeNamedAttribute(FArchiveFieldName(Name), Value)
