#include "EnginePCH.h"
#include "Level.h"
#include "World.h"

// delete Actor가 소멸자를 호출하려면 AActor의 완전한 정의가 필요하다.
// 전방 선언만으로는 C4150(소멸자 미호출)이 되어 액터가 GUObjectArray에 남는다.
#include "GameFramework/Actor.h"
#include "Components/ExponentialHeightFogComponent.h"

void ULevel::AddActor(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	Actors.Add(Actor);
}

void ULevel::ClearActors()
{
	for (AActor* Actor : Actors)
	{
		if (!Actor)
		{
			continue;
		}

		delete Actor;
	}
	Actors.Reset();
}

void ULevel::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	FArchive& Ar = Record.GetUnderlyingArchive();

	if (Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
	{
		int32 Num = Actors.Num();
		FStructuredArchive::FArray ActorArray = Record.EnterArray("Actors", Num);
		if (Ar.IsLoading())
		{
			Actors.SetNum(Num);
		}
		for (int32 i = 0; i < Num; ++i)
		{
			UObject* Obj = Actors[i];
			ActorArray.EnterElement() << Obj;
			Actors[i] = static_cast<AActor*>(Obj);
		}

		UObject* WorldObj = OwningWorld;
		Record << SA_VALUE("OwningWorld", WorldObj);
		OwningWorld = static_cast<UWorld*>(WorldObj);
		return;
	}

	if (Ar.IsSaving())
	{
		// 개수를 먼저 알아야 EnterArray를 할 수 있으므로 저장할 액터부터 고른다
		TArray<AActor*> SavedActors;
		for (AActor* Actor : Actors)
		{
			if (Actor && !Actor->HasAnyFlags(EObjectFlags::RF_Transient))
			{
				SavedActors.Add(Actor);
			}
		}

		int32 Num = SavedActors.Num();
		FStructuredArchive::FArray ActorArray = Record.EnterArray("Actors", Num);
		for (AActor* Actor : SavedActors)
		{
			FStructuredArchive::FRecord ActorRecord = ActorArray.EnterElement().EnterRecord();
			FString ClassName = Actor->GetClass()->Name;
			ActorRecord << SA_VALUE("Class", ClassName);
			if (TOptional<FStructuredArchive::FSlot> NameSlot{ ActorRecord.TryEnterField("Name", true) })
			{
				FString ActorName{ Actor->GetName() };
				NameSlot.GetValue() << ActorName;
			}
			Actor->Serialize(ActorRecord);

			USceneComponent* Root = Actor->GetRootComponent();
			USceneComponent* AttachParent = Root ? Root->GetAttachParent() : nullptr;
			int32 ParentIndex = AttachParent ? SavedActors.Find(AttachParent->GetOwner()) : -1;
			if (TOptional<FStructuredArchive::FSlot> AttachSlot = ActorRecord.TryEnterField("AttachParent", ParentIndex >= 0))
			{
				FString ParentComponentName = AttachParent->GetName();
				FStructuredArchive::FRecord AttachRecord = AttachSlot.GetValue().EnterRecord();
				AttachRecord << SA_VALUE("Actor", ParentIndex) << SA_VALUE("Component", ParentComponentName);
			}
		}
		return;
	}

	// 불러오기: 월드를 비우는 건 호출하는 쪽 책임 (PIE 복제는 새 월드라 비울 필요가 없다)
	assert(OwningWorld);

	struct FPendingActorAttachment
	{
		AActor* Child;
		int32 ParentIndex;
		FString ParentComponentName;
	};
	TArray<FPendingActorAttachment> PendingAttachments;

	int32 Num = 0;
	FStructuredArchive::FArray ActorArray = Record.EnterArray("Actors", Num);
	TArray<AActor*> LoadedActors;
	LoadedActors.SetNum(Num);
	for (int32 i = 0; i < Num; ++i)
	{
		LoadedActors[i] = nullptr;
		FStructuredArchive::FRecord ActorRecord = ActorArray.EnterElement().EnterRecord();

		FString ClassName;
		ActorRecord << SA_VALUE("Class", ClassName);
		FString ActorName{};
		if (TOptional<FStructuredArchive::FSlot> NameSlot{ ActorRecord.TryEnterField("Name", false) })
		{
			NameSlot.GetValue() << ActorName;
		}

		UClass* Class = FindClass(ClassName);
		if (!Class || !Class->IsChildOf(AActor::StaticClass()))
		{
			HTR_LOG(Warning, "Load: unknown actor class '{}', skipped", ClassName);
			continue;
		}

		if (AActor * Actor{ OwningWorld->SpawnActor(Class, FName{ ActorName }) })
		{
			Actor->Serialize(ActorRecord);
			LoadedActors[i] = Actor;

			if (TOptional<FStructuredArchive::FSlot> AttachSlot = ActorRecord.TryEnterField("AttachParent", false))
			{
				FPendingActorAttachment Attachment{ Actor, -1, FString() };
				FStructuredArchive::FRecord AttachRecord = AttachSlot.GetValue().EnterRecord();
				AttachRecord << SA_VALUE("Actor", Attachment.ParentIndex) << SA_VALUE("Component", Attachment.ParentComponentName);
				PendingAttachments.Add(Attachment);
			}

			// SpawnActor가 기본값으로 등록한 Fog를 방금 읽은 값으로 갱신한다.
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (UExponentialHeightFogComponent* Fog = Cast<UExponentialHeightFogComponent>(Component))
				{
					OwningWorld->GetScene().UpdateFogInfo(Fog->GetUUID(), Fog->GetFogInfo());
				}
			}
		}
	}

	for (const FPendingActorAttachment& Attachment : PendingAttachments)
	{
		if (Attachment.ParentIndex < 0 || Attachment.ParentIndex >= Num)
		{
			continue;
		}

		AActor* ParentActor = LoadedActors[Attachment.ParentIndex];
		if (!ParentActor)
		{
			continue;
		}

		USceneComponent* ParentComponent = Cast<USceneComponent>(ParentActor->FindComponentByName(FName(Attachment.ParentComponentName)));
		if (!ParentComponent)
		{
			ParentComponent = ParentActor->GetRootComponent();
		}

		Attachment.Child->AttachToComponent(ParentComponent, EAttachmentRule::KeepRelative);
	}
}
