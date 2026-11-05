#pragma once

#include "Archive.h"

struct FLazyObjectPtr;
struct FObjectPtr;
struct FSoftObjectPath;
struct FSoftObjectPtr;
struct FWeakObjectPtr;

class FArchiveUObject : public FArchive
{
public:
	using FArchive::operator<<;
};