#include "EnginePCH.h"
#include "Class.h"

#include "Object.h"

UClass::UClass()
	: UObject(false)
{
}

const FFunction* UClass::FindFunctionByName(const FName& FunctionName) const
{
	for (const UClass* Class = this; Class; Class = Class->Super)
	{
		for (const FFunction& Function : Class->Functions)
		{
			if (Function.Name == FunctionName)
			{
				return &Function;
			}
		}
	}
	return nullptr;
}

bool UClass::IsChildOf(const UClass* BaseClass) const
{
	if (!BaseClass)
	{
		return false;
	}
	for (const UClass* c = this; c; c = c->Super)
	{
		if (c == BaseClass)
		{
			return true;
		}
	}
	return false;
}

UObject* UClass::GetDefaultObject()
{
	if (!DefaultObject)
	{
		DefaultObject = Constructor(UObject::operator new(ClassSize));
	}
	return DefaultObject;
}
