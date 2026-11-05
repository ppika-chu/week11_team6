#include "EnginePCH.h"
#include "EditorEntryPoint.h"

#include "Launch/EntryPoint.h"

#if defined(EXECUTABLE)
#include "Engine/GameEngine.h"
#else
#include "Editor/HitoriEd/EditorEngine.h"
#endif

UClass* GetEngineClass()
{
#if defined(EXECUTABLE)
	return UGameEngine::StaticClass();
#else
	return UEditorEngine::StaticClass();
#endif
}
