#pragma once

class AActor;
class ULevel;
struct FSequenceBinding;
class USkeletalMeshComponent;

namespace SequenceBinding
{
bool Make(const USkeletalMeshComponent* Component, const ULevel* SequenceLevel, FSequenceBinding& OutBinding);
AActor* ResolveActor(const FSequenceBinding& Binding, const ULevel* SequenceLevel);
USkeletalMeshComponent* ResolveComponent(const FSequenceBinding& Binding, const ULevel* SequenceLevel);
}
