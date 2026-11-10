#include "EnginePCH.h"
#include "Scripting/AnimationGraphBindings.h"
#include <sol/sol.hpp>

#include "Animation/AnimInstance.h"
#include "Animation/AnimState.h"
#include "Animation/AnimTransition.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationRuntime.h"
#include "Animation/AnimNodes/AnimNode_SequencePlayer.h"
#include "Animation/AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "Animation/AnimNodes/AnimNode_StateMachine.h"
#include "Animation/AnimNodes/AnimNode_Slot.h"
#include "Animation/AnimNodes/AnimNode_TwoWayBlend.h"
#include "Animation/AnimNodes/AnimNode_UseCachedPose.h"
#include "Asset/AssetManager.h"
#include "Components/SkeletalMeshComponent.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <vector>

struct FLuaAnimationCondition
{
	sol::protected_function Function;
	UAnimInstance* Instance = nullptr;
	uint32 UUID = 0;
};

// C++ 그래프는 raw pointer만 연결한다. Lua 핸들은 입력 객체의 수명만 별도로 유지한다.
struct FLuaAnimationGraphObject
{
	std::shared_ptr<void> Value;
	std::unordered_map<FString, std::shared_ptr<FLuaAnimationGraphObject>> Inputs;
};

struct FLuaAnimationRoot
{
	UAnimInstance* Instance;
	uint32 UUID;
	FAnimNode_Base* Node;
	std::shared_ptr<FLuaAnimationGraphObject> Lifetime;
};

struct FLuaAnimationGraphBindings
{
	std::vector<std::weak_ptr<FLuaAnimationCondition>> Conditions;
	std::unordered_map<void*, std::weak_ptr<FLuaAnimationGraphObject>> Objects;
	std::unordered_map<uint32, FLuaAnimationRoot> Roots;
};

namespace
{
// 기존 UAnimInstance의 확장 지점으로 Lua 그래프를 공급한다.
// 노드 소유권은 Lua 바인딩에 있으며 이 어댑터는 Root의 raw pointer만 보관한다.
class ULuaAnimInstance final : public UAnimInstance
{
	DECLARE_CLASS(ULuaAnimInstance, UAnimInstance)
public:
	void SetGraphRoot(FAnimNode_Base* InRoot)
	{
		ClearMontages();
		GraphRoot = InRoot;
		if (IsInitialized())
		{
			InitializeAnimation();
		}
	}
	FAnimNode_Base* GetGraphRoot() const { return GraphRoot; }

	void PlayMontage(FAnimNode_Slot* Slot, UAnimSequence* Sequence, float PlayRate, float BlendIn, float BlendOut, bool bLooping)
	{
		FAnimPose Pose;
		Sequence->GetAnimationPose(Pose, FAnimExtractContext(0.0f));
		const USkeletalMeshComponent* Component = GetSkelMeshComponent();
		const USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
		if (!Mesh || Pose.LocalTransforms.Num() != Mesh->RefSkeleton.GetNum())
		{
			throw std::runtime_error("Montage pose bone count does not match the skeletal mesh");
		}
		StopMontage(Slot, 0.0f);
		FActiveMontage& Playback = Montages.emplace_back();
		Playback.Slot = Slot;
		Playback.Sequence = Sequence;
		Playback.PlayRate = PlayRate;
		Playback.BlendIn = BlendIn;
		Playback.BlendOut = BlendOut;
		Playback.bLooping = bLooping;
		Playback.CurrentTime = 0.0f;
		Playback.ElapsedTime = 0.0f;
		// 재생 직후 평가가 일어나도 이전 Montage 포즈가 남지 않게 한다.
		Slot->SetSlotPose(Pose);
		Slot->SetMontageWeight(BlendIn == 0.0f ? 1.0f : 0.0f);
	}

	void StopMontage(FAnimNode_Slot* Slot, float BlendOut)
	{
		for (FActiveMontage& Playback : Montages)
		{
			if (Playback.Slot != Slot) { continue; }
			if (BlendOut > 0.0f)
			{
				Playback.bStopping = true;
				Playback.StopDuration = BlendOut;
				Playback.StopElapsed = 0.0f;
				Playback.StopStartWeight = Slot->GetMontageWeight();
			}
			else { ClearMontagePose(Slot); }
		}
		if (BlendOut <= 0.0f)
		{
			std::erase_if(Montages, [Slot](const FActiveMontage& Playback) { return Playback.Slot == Slot; });
		}
	}

	bool IsMontagePlaying(const FAnimNode_Slot* Slot) const
	{
		return std::any_of(Montages.begin(), Montages.end(), [Slot](const FActiveMontage& Playback) { return Playback.Slot == Slot; });
	}

	float GetMontageTime(const FAnimNode_Slot* Slot) const
	{
		for (const FActiveMontage& Playback : Montages)
		{
			if (Playback.Slot == Slot) { return Playback.CurrentTime; }
		}
		return 0.0f;
	}

protected:
	FAnimNode_Base* GetCustomRootNode() override { return GraphRoot; }
	void NativeUninitializeAnimation() override { ClearMontages(); }
	void NativeUpdateAnimation(float DeltaSeconds) override
	{
		for (FActiveMontage& Playback : Montages)
		{
			const float Length = Playback.Sequence->GetPlayLength();
			if (Length <= 0.0f) { ClearMontagePose(Playback.Slot); Playback.bFinished = true; continue; }
			const float Step = FMath::Max(0.0f, DeltaSeconds);
			float NextTime = Playback.CurrentTime;
			FAnimationRuntime::AdvanceTime(Playback.bLooping,
				Step * Playback.PlayRate * Playback.Sequence->RateScale, NextTime, Length);
			Playback.ElapsedTime += Step;
			const float InWeight = Playback.BlendIn > 0.0f
				? FMath::Clamp(Playback.ElapsedTime / Playback.BlendIn, 0.0f, 1.0f) : 1.0f;
			const float RemainingSeconds = (Length - NextTime) /
				(Playback.PlayRate * Playback.Sequence->RateScale);
			const float OutWeight = Playback.bLooping ? 1.0f :
				(Playback.BlendOut > 0.0f
					? FMath::Clamp(RemainingSeconds / Playback.BlendOut, 0.0f, 1.0f)
					: (NextTime < Length ? 1.0f : 0.0f));
			float Weight = FMath::Min(InWeight, OutWeight);
			if (Playback.bStopping)
			{
				Playback.StopElapsed += Step;
				Weight = FMath::Min(Weight, Playback.StopStartWeight *
					FMath::Clamp(1.0f - Playback.StopElapsed / Playback.StopDuration, 0.0f, 1.0f));
			}
			SequenceAdvanceImmediate(Playback.Sequence, Playback.bLooping, Playback.PlayRate,
				Step, Playback.CurrentTime, Weight);
			if (Weight <= 0.0f && (Playback.bStopping || (!Playback.bLooping && NextTime >= Length)))
			{
				ClearMontagePose(Playback.Slot);
				Playback.bFinished = true;
				continue;
			}
			FAnimPose Pose;
			Playback.Sequence->GetAnimationPose(Pose, FAnimExtractContext(Playback.CurrentTime));
			Playback.Slot->SetSlotPose(Pose);
			Playback.Slot->SetMontageWeight(Weight);
		}
		std::erase_if(Montages, [](const FActiveMontage& Playback) { return Playback.bFinished; });
	}

private:
	struct FActiveMontage
	{
		FAnimNode_Slot* Slot = nullptr;
		UAnimSequence* Sequence = nullptr;
		float CurrentTime = 0.0f;
		float ElapsedTime = 0.0f;
		float PlayRate = 1.0f;
		float BlendIn = 0.0f;
		float BlendOut = 0.0f;
		bool bLooping = false;
		bool bStopping = false;
		bool bFinished = false;
		float StopDuration = 0.0f;
		float StopElapsed = 0.0f;
		float StopStartWeight = 0.0f;
	};
	static void ClearMontagePose(FAnimNode_Slot* Slot)
	{
		Slot->SetMontageWeight(0.0f);
		Slot->SetSlotPose(FAnimPose{});
	}
	void ClearMontages()
	{
		for (FActiveMontage& Playback : Montages) { ClearMontagePose(Playback.Slot); }
		Montages.clear();
	}
	FAnimNode_Base* GraphRoot = nullptr;
	std::vector<FActiveMontage> Montages;
};

ULuaAnimInstance* RequireLuaAnimInstance(UAnimInstance* Instance)
{
	ULuaAnimInstance* LuaInstance = Cast<ULuaAnimInstance>(Instance);
	if (!LuaInstance)
	{
		throw std::runtime_error("Lua root access requires an instance prepared by mesh:CreateAnimInstance()");
	}
	return LuaInstance;
}

// AnimInstance는 Component 소유다. UObject 주소 재사용은 UUID로 구분한다.
struct FLuaAnimInstance
{
	UAnimInstance* Address = nullptr;
	uint32 UUID = 0;
	std::shared_ptr<FLuaAnimationGraphBindings> Bindings;

	FLuaAnimInstance() = default;
	FLuaAnimInstance(UAnimInstance* Instance, std::shared_ptr<FLuaAnimationGraphBindings> InBindings)
		: Address(Instance), UUID(Instance ? Instance->GetUUID() : 0), Bindings(std::move(InBindings)) {}

	UAnimInstance* Resolve() const
	{
		UObject* Live = FindLiveObjectByAddress(Address);
		return Live && Live->GetUUID() == UUID ? Cast<UAnimInstance>(Live) : nullptr;
	}
	bool IsValid() const { return Resolve() != nullptr; }
	UAnimInstance* Get() const
	{
		UAnimInstance* Instance = Resolve();
		if (!Instance) { throw std::runtime_error("AnimInstance has been destroyed"); }
		return Instance;
	}
	void CheckOwner(const FLuaAnimInstance& Other) const
	{
		Get();
		Other.Get();
		if (Address != Other.Address || UUID != Other.UUID)
		{
			throw std::runtime_error("Animation graph connections must belong to the same AnimInstance");
		}
	}
};

struct FLuaAnimNode
{
	FLuaAnimInstance Owner;
	FAnimNode_Base* Node = nullptr;
	std::shared_ptr<FLuaAnimationGraphObject> Lifetime;

	FLuaAnimNode() = default;
	FLuaAnimNode(FLuaAnimInstance InOwner, FAnimNode_Base* InNode) : Owner(InOwner), Node(InNode)
	{
		Lifetime = Owner.Bindings->Objects.at(Node).lock();
		if (!Lifetime) { throw std::runtime_error("Lua animation object has been released"); }
	}
	bool IsValid() const { return Node && Owner.IsValid(); }
	FAnimNode_Base* Get() const
	{
		Owner.Get();
		if (!Node) { throw std::runtime_error("Animation node is null"); }
		return Node;
	}
};

template<typename T>
struct TLuaAnimNode : FLuaAnimNode
{
	TLuaAnimNode(FLuaAnimInstance InOwner, T* InNode) : FLuaAnimNode(InOwner, InNode) {}
	T* Get() const { return static_cast<T*>(FLuaAnimNode::Get()); }
};

template<typename T>
struct TLuaAnimDefinition
{
	FLuaAnimInstance Owner;
	T* Value;
	std::shared_ptr<FLuaAnimationGraphObject> Lifetime;
	T* Get() const { Owner.Get(); return Value; }
	bool IsValid() const { return Value && Owner.IsValid(); }
};

using FLuaAnimState = TLuaAnimDefinition<FAnimState>;
using FLuaAnimTransition = TLuaAnimDefinition<FAnimTransition>;

template<typename T>
std::shared_ptr<FLuaAnimationGraphObject> CreateGraphObject(const FLuaAnimInstance& Owner)
{
	Owner.Get();
	auto Object = std::make_shared<T>();
	auto Lifetime = std::make_shared<FLuaAnimationGraphObject>();
	Lifetime->Value = Object;
	Owner.Bindings->Objects[Object.get()] = Lifetime;
	return Lifetime;
}

bool Reaches(const FLuaAnimationGraphObject* From, const FLuaAnimationGraphObject* Target)
{
	if (From == Target) { return true; }
	for (const auto& [Name, Input] : From->Inputs)
	{
		if (Reaches(Input.get(), Target)) { return true; }
	}
	return false;
}

void KeepInput(const std::shared_ptr<FLuaAnimationGraphObject>& Output, const FString& Name,
	const std::shared_ptr<FLuaAnimationGraphObject>& Input)
{
	if (!Input) { Output->Inputs.erase(Name); return; }
	if (Reaches(Input.get(), Output.get())) { throw std::runtime_error("Animation pose graphs cannot contain cycles"); }
	Output->Inputs[Name] = Input;
}

FAnimNode_Base* ResolveInput(const FLuaAnimInstance& Owner, const FLuaAnimNode* Input)
{
	if (!Input) { return nullptr; }
	Owner.CheckOwner(Input->Owner);
	FAnimNode_Base* Node = Input->Get();
	if (dynamic_cast<FAnimNode_TransitionResult*>(Node))
	{
		throw std::runtime_error("TransitionResult produces a bool and cannot be a pose input");
	}
	return Node;
}

template<typename T>
T* ResolveTyped(const FLuaAnimInstance& Owner, const TLuaAnimNode<T>* Input)
{
	if (!Input) { return nullptr; }
	Owner.CheckOwner(Input->Owner);
	return Input->Get();
}

template<typename T>
sol::object NodeReference(sol::this_state State, FLuaAnimInstance Owner, T* Node)
{
	return Node ? sol::make_object(State, TLuaAnimNode<T>(Owner, Node)) : sol::make_object(State, sol::nil);
}

// GetInputNode도 실제 타입의 핸들을 반환해 이후 타입별 설정 함수를 계속 사용할 수 있다.
sol::object NodeReference(sol::this_state State, FLuaAnimInstance Owner, FAnimNode_Base* Node)
{
#define ANIM_NODE_REFERENCE(Type) if (auto* Typed = dynamic_cast<Type*>(Node)) { return NodeReference(State, Owner, Typed); }
	ANIM_NODE_REFERENCE(FAnimNode_StateResult)
	ANIM_NODE_REFERENCE(FAnimNode_Root)
	ANIM_NODE_REFERENCE(FAnimNode_SequencePlayer)
	ANIM_NODE_REFERENCE(FAnimNode_BlendSpacePlayer)
	ANIM_NODE_REFERENCE(FAnimNode_StateMachine)
	ANIM_NODE_REFERENCE(FAnimNode_TransitionResult)
	ANIM_NODE_REFERENCE(FAnimNode_Slot)
	ANIM_NODE_REFERENCE(FAnimNode_TwoWayBlend)
	ANIM_NODE_REFERENCE(FAnimNode_SaveCachedPose)
	ANIM_NODE_REFERENCE(FAnimNode_UseCachedPose)
#undef ANIM_NODE_REFERENCE
	return Node ? sol::make_object(State, FLuaAnimNode(Owner, Node)) : sol::make_object(State, sol::nil);
}

template<typename T>
sol::usertype<TLuaAnimNode<T>> RegisterNode(sol::state& Lua, const char* Name)
{
	return Lua.new_usertype<TLuaAnimNode<T>>(Name, sol::no_constructor,
		sol::base_classes, sol::bases<FLuaAnimNode>());
}

template<typename T, typename TValue>
void BindSetter(sol::usertype<TLuaAnimNode<T>>& Type, const char* Name, void(T::*Setter)(TValue))
{
	Type.set_function(Name, [Setter](const TLuaAnimNode<T>& Node, TValue Value) { (Node.Get()->*Setter)(Value); });
}

template<typename T, typename TValue>
void BindGetter(sol::usertype<TLuaAnimNode<T>>& Type, const char* Name, TValue(T::*Getter)() const)
{
	Type.set_function(Name, [Getter](const TLuaAnimNode<T>& Node) { return (Node.Get()->*Getter)(); });
}

template<typename T>
void BindPoseInput(sol::usertype<TLuaAnimNode<T>>& Type)
{
	Type.set_function("SetInputNode", [](const TLuaAnimNode<T>& Node, const FLuaAnimNode* Input)
	{
		FAnimNode_Base* NativeInput = ResolveInput(Node.Owner, Input);
		KeepInput(Node.Lifetime, "Input", Input ? Input->Lifetime : nullptr);
		Node.Get()->SetInputNode(NativeInput);
	});
	Type.set_function("GetInputNode", [](const TLuaAnimNode<T>& Node, sol::this_state State)
	{
		return NodeReference(State, Node.Owner, Node.Get()->GetInputNode());
	});
}

template<typename T>
T* FindGraphAsset(const FLuaAnimInstance& Owner, const FString& Path)
{
	T* Asset = UAssetManager::GetAssetByPath<T>(Path);
	USkeletalMeshComponent* Component = Owner.Get()->GetSkelMeshComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (!Asset || !Mesh || !Asset->GetSkeleton() || Asset->GetSkeleton() != Mesh->Skeleton)
	{
		throw std::runtime_error("Animation asset is missing, has the wrong type, or uses a different skeleton: " + Path);
	}
	return Asset;
}

ULuaAnimInstance* RequireAttachedSlot(const TLuaAnimNode<FAnimNode_Slot>& Node)
{
	auto* Instance = RequireLuaAnimInstance(Node.Owner.Get());
	const auto Root = Node.Owner.Bindings->Roots.find(Node.Owner.UUID);
	if (Root == Node.Owner.Bindings->Roots.end() ||
		Instance->GetGraphRoot() != Root->second.Node ||
		!Reaches(Root->second.Lifetime.get(), Node.Lifetime.get()))
	{
		throw std::runtime_error("Montage playback requires a Slot connected to the current Root");
	}
	return Instance;
}

sol::object CreateNode(const FLuaAnimInstance& Instance, const FString& Type, sol::this_state State)
{
	Instance.Get();
#define CREATE_ANIM_NODE(Name) if (Type == #Name) { auto Lifetime = CreateGraphObject<FAnimNode_##Name>(Instance); return NodeReference(State, Instance, static_cast<FAnimNode_##Name*>(Lifetime->Value.get())); }
	CREATE_ANIM_NODE(Root)
	CREATE_ANIM_NODE(StateResult)
	CREATE_ANIM_NODE(SequencePlayer)
	CREATE_ANIM_NODE(BlendSpacePlayer)
	CREATE_ANIM_NODE(StateMachine)
	CREATE_ANIM_NODE(TransitionResult)
	CREATE_ANIM_NODE(Slot)
	CREATE_ANIM_NODE(TwoWayBlend)
	CREATE_ANIM_NODE(SaveCachedPose)
	CREATE_ANIM_NODE(UseCachedPose)
#undef CREATE_ANIM_NODE
	throw std::runtime_error("Unknown animation node type: " + Type);
}

void BindNodes(sol::state& Lua)
{
	Lua.new_usertype<FLuaAnimNode>("FAnimNode_Base", sol::no_constructor, "IsValid", &FLuaAnimNode::IsValid);
	auto Root = RegisterNode<FAnimNode_Root>(Lua, "FAnimNode_Root");
	BindPoseInput(Root);
	auto Result = RegisterNode<FAnimNode_StateResult>(Lua, "FAnimNode_StateResult");
	BindPoseInput(Result);
	auto Sequence = RegisterNode<FAnimNode_SequencePlayer>(Lua, "FAnimNode_SequencePlayer");
	Sequence.set_function("SetSequencePlayer", [](const TLuaAnimNode<FAnimNode_SequencePlayer>& Node, const FString& Path)
	{
		Node.Get()->SetSequencePlayer(FindGraphAsset<UAnimSequence>(Node.Owner, Path));
	});
	Sequence.set_function("GetPlayLength", [](const TLuaAnimNode<FAnimNode_SequencePlayer>& Node)
	{
		UAnimSequence* Asset = Node.Get()->GetSequencePlayer();
		return Asset ? Asset->GetPlayLength() : 0.0f;
	});
	BindSetter(Sequence, "SetStartPosition", &FAnimNode_SequencePlayer::SetStartPosition);
	BindGetter(Sequence, "GetStartPosition", &FAnimNode_SequencePlayer::GetStartPosition);
	BindSetter(Sequence, "SetPlayRate", &FAnimNode_SequencePlayer::SetPlayRate);
	BindGetter(Sequence, "GetPlayRate", &FAnimNode_SequencePlayer::GetPlayRate);
	BindSetter(Sequence, "SetLooping", &FAnimNode_SequencePlayer::SetLooping);
	BindGetter(Sequence, "IsLooping", &FAnimNode_SequencePlayer::IsLooping);
	BindSetter(Sequence, "SetPlaying", &FAnimNode_SequencePlayer::SetPlaying);
	BindGetter(Sequence, "IsPlaying", &FAnimNode_SequencePlayer::IsPlaying);
	// Windows SDK의 함수형 GetCurrentTime 매크로와 동일하게 호출식으로 연결한다.
	Sequence.set_function("GetCurrentTime", [](const TLuaAnimNode<FAnimNode_SequencePlayer>& Node) { return Node.Get()->GetCurrentTime(); });
	BindGetter(Sequence, "GetPreviousTime", &FAnimNode_SequencePlayer::GetPreviousTime);

	auto BlendSpace = RegisterNode<FAnimNode_BlendSpacePlayer>(Lua, "FAnimNode_BlendSpacePlayer");
	using FBlendSpaceHandle = TLuaAnimNode<FAnimNode_BlendSpacePlayer>;
	BlendSpace.set_function("SetBlendSpace", [](const FBlendSpaceHandle& Node, const FString& Path)
	{
		return Node.Get()->SetBlendSpace(FindGraphAsset<UBlendSpace>(Node.Owner, Path));
	});
	BlendSpace.set_function("SetPosition", [](const FBlendSpaceHandle& Node, FVector Position) { return Node.Get()->SetPosition(Position); });
	BlendSpace.set_function("SetStartPosition", [](const FBlendSpaceHandle& Node, float Value) { return Node.Get()->SetStartPosition(Value); });
	BlendSpace.set_function("SetPlayRate", [](const FBlendSpaceHandle& Node, float Value) { return Node.Get()->SetPlayRate(Value); });
	BlendSpace.set_function("SetLoop", [](const FBlendSpaceHandle& Node, bool Value) { return Node.Get()->SetLoop(Value); });
	BlendSpace.set_function("SetResetPlayTimeWhenBlendSpaceChanges", [](const FBlendSpaceHandle& Node, bool Value) { return Node.Get()->SetResetPlayTimeWhenBlendSpaceChanges(Value); });
	BindGetter(BlendSpace, "GetPosition", &FAnimNode_BlendSpacePlayer::GetPosition);
	BindGetter(BlendSpace, "GetStartPosition", &FAnimNode_BlendSpacePlayer::GetStartPosition);
	BindGetter(BlendSpace, "GetPlayRate", &FAnimNode_BlendSpacePlayer::GetPlayRate);
	BindGetter(BlendSpace, "IsLooping", &FAnimNode_BlendSpacePlayer::IsLooping);
	BindGetter(BlendSpace, "GetAccumulatedTime", &FAnimNode_BlendSpacePlayer::GetAccumulatedTime);

	auto Machine = RegisterNode<FAnimNode_StateMachine>(Lua, "FAnimNode_StateMachine");
	using FMachineHandle = TLuaAnimNode<FAnimNode_StateMachine>;
	Machine.set_function("AddState", [](const FMachineHandle& Node, const FLuaAnimState& State)
	{
		Node.Owner.CheckOwner(State.Owner);
		// 상태 출력 → Machine으로 다시 연결하는 재귀 그래프를 차단한다.
		if (Reaches(State.Lifetime.get(), Node.Lifetime.get())) { throw std::runtime_error("Animation pose graphs cannot contain cycles"); }
		int32 Index = Node.Get()->AddState(State.Get());
		if (Index >= 0) { KeepInput(Node.Lifetime, "State" + std::to_string(Index), State.Lifetime); }
		return Index;
	});
	Machine.set_function("AddTransition", [](const FMachineHandle& Node, const FLuaAnimTransition& Transition)
	{
		Node.Owner.CheckOwner(Transition.Owner);
		int32 Index = Node.Get()->AddTransition(Transition.Get());
		if (Index >= 0) { KeepInput(Node.Lifetime, "Transition" + std::to_string(Index), Transition.Lifetime); }
		return Index;
	});
	Machine.set_function("SetInitialStateIndex", [](const FMachineHandle& Node, int32 Index) { return Node.Get()->SetInitialStateIndex(Index); });
	BindGetter(Machine, "GetInitialStateIndex", &FAnimNode_StateMachine::GetInitialStateIndex);
	BindGetter(Machine, "GetCurrentStateIndex", &FAnimNode_StateMachine::GetCurrentStateIndex);
	BindGetter(Machine, "GetActiveTransitionIndex", &FAnimNode_StateMachine::GetActiveTransitionIndex);
	BindGetter(Machine, "GetTransitionElapsedTime", &FAnimNode_StateMachine::GetTransitionElapsedTime);

	auto TransitionResult = RegisterNode<FAnimNode_TransitionResult>(Lua, "FAnimNode_TransitionResult");
	BindGetter(TransitionResult, "GetCanTransition", &FAnimNode_TransitionResult::GetCanTransition);

	auto Slot = RegisterNode<FAnimNode_Slot>(Lua, "FAnimNode_Slot");
	BindPoseInput(Slot);
	Slot.set_function("SetSlotName", [](const TLuaAnimNode<FAnimNode_Slot>& Node, const FString& Name) { Node.Get()->SetSlotName(FName(Name)); });
	Slot.set_function("GetSlotName", [](const TLuaAnimNode<FAnimNode_Slot>& Node) { return Node.Get()->GetSlotName().ToString(); });
	BindSetter(Slot, "SetMontageWeight", &FAnimNode_Slot::SetMontageWeight);
	BindGetter(Slot, "GetMontageWeight", &FAnimNode_Slot::GetMontageWeight);
	// 이 엔진에는 UAnimMontage asset이 아직 없다. 단일 UAnimSequence를 Slot에 재생하는 경량 경로다.
	Slot.set_function("PlayMontage", [](const TLuaAnimNode<FAnimNode_Slot>& Node, const FString& SequencePath,
		sol::optional<float> PlayRate, sol::optional<float> BlendIn, sol::optional<float> BlendOut, sol::optional<bool> Loop)
	{
		auto* Instance = RequireAttachedSlot(Node);
		const float Rate = PlayRate.value_or(1.0f);
		const float In = BlendIn.value_or(0.12f);
		const float Out = BlendOut.value_or(0.18f);
		if (!std::isfinite(Rate) || Rate <= 0.0f || !std::isfinite(In) || In < 0.0f ||
			!std::isfinite(Out) || Out < 0.0f)
		{
			throw std::runtime_error("PlayMontage requires positive PlayRate and non-negative finite blend times");
		}
		UAnimSequence* Sequence = FindGraphAsset<UAnimSequence>(Node.Owner, SequencePath);
		if (Sequence->GetPlayLength() <= 0.0f || !std::isfinite(Sequence->RateScale) || Sequence->RateScale <= 0.0f)
		{
			throw std::runtime_error("PlayMontage requires a sequence with positive duration and RateScale");
		}
		Instance->PlayMontage(Node.Get(), Sequence, Rate, In, Out, Loop.value_or(false));
	});
	Slot.set_function("StopMontage", [](const TLuaAnimNode<FAnimNode_Slot>& Node, sol::optional<float> BlendOut)
	{
		const float Duration = BlendOut.value_or(0.18f);
		if (!std::isfinite(Duration) || Duration < 0.0f)
		{
			throw std::runtime_error("StopMontage blend time must be finite and non-negative");
		}
		RequireAttachedSlot(Node)->StopMontage(Node.Get(), Duration);
	});
	Slot.set_function("IsMontagePlaying", [](const TLuaAnimNode<FAnimNode_Slot>& Node)
	{
		return RequireAttachedSlot(Node)->IsMontagePlaying(Node.Get());
	});
	Slot.set_function("GetMontageTime", [](const TLuaAnimNode<FAnimNode_Slot>& Node)
	{
		return RequireAttachedSlot(Node)->GetMontageTime(Node.Get());
	});
	// 별도 재생기에서 계산한 포즈를 공급할 때 사용하는 단일 Sequence 샘플링 입구.
	Slot.set_function("SetMontagePoseFromSequence", [](const TLuaAnimNode<FAnimNode_Slot>& Node,
		const FString& SequencePath, float Time, float Weight)
	{
		auto* Instance = RequireAttachedSlot(Node);
		if (!std::isfinite(Time) || !std::isfinite(Weight) || Weight < 0.0f || Weight > 1.0f)
		{
			throw std::runtime_error("Montage pose time and weight must be finite; weight must be within [0, 1]");
		}
		UAnimSequence* Sequence = FindGraphAsset<UAnimSequence>(Node.Owner, SequencePath);
		FAnimPose Pose;
		Sequence->GetAnimationPose(Pose, FAnimExtractContext(FMath::Clamp(Time, 0.0f, Sequence->GetPlayLength())));
		const USkeletalMesh* Mesh = Node.Owner.Get()->GetSkelMeshComponent()->GetSkeletalMesh();
		if (Pose.LocalTransforms.Num() != Mesh->RefSkeleton.GetNum())
		{
			throw std::runtime_error("Montage pose bone count does not match the skeletal mesh");
		}
		Instance->StopMontage(Node.Get(), 0.0f);
		Node.Get()->SetSlotPose(Pose);
		Node.Get()->SetMontageWeight(Weight);
	});
	Slot.set_function("ClearMontagePose", [](const TLuaAnimNode<FAnimNode_Slot>& Node)
	{
		RequireAttachedSlot(Node)->StopMontage(Node.Get(), 0.0f);
		Node.Get()->SetMontageWeight(0.0f);
		Node.Get()->SetSlotPose(FAnimPose{});
	});

	auto Blend = RegisterNode<FAnimNode_TwoWayBlend>(Lua, "FAnimNode_TwoWayBlend");
	using FBlendHandle = TLuaAnimNode<FAnimNode_TwoWayBlend>;
	Blend.set_function("SetInputNodeA", [](const FBlendHandle& Node, const FLuaAnimNode* Input)
	{
		auto* Native = ResolveInput(Node.Owner, Input);
		KeepInput(Node.Lifetime, "A", Input ? Input->Lifetime : nullptr);
		Node.Get()->SetInputNodeA(Native);
	});
	Blend.set_function("SetInputNodeB", [](const FBlendHandle& Node, const FLuaAnimNode* Input)
	{
		auto* Native = ResolveInput(Node.Owner, Input);
		KeepInput(Node.Lifetime, "B", Input ? Input->Lifetime : nullptr);
		Node.Get()->SetInputNodeB(Native);
	});
	Blend.set_function("GetInputNodeA", [](const FBlendHandle& Node, sol::this_state State) { return NodeReference(State, Node.Owner, Node.Get()->GetInputNodeA()); });
	Blend.set_function("GetInputNodeB", [](const FBlendHandle& Node, sol::this_state State) { return NodeReference(State, Node.Owner, Node.Get()->GetInputNodeB()); });
	BindSetter(Blend, "SetAlpha", &FAnimNode_TwoWayBlend::SetAlpha);
	BindGetter(Blend, "GetAlpha", &FAnimNode_TwoWayBlend::GetAlpha);

	auto Save = RegisterNode<FAnimNode_SaveCachedPose>(Lua, "FAnimNode_SaveCachedPose");
	BindPoseInput(Save);
	Save.set_function("SetCachePoseName", [](const TLuaAnimNode<FAnimNode_SaveCachedPose>& Node, const FString& Name) { Node.Get()->SetCachePoseName(FName(Name)); });
	Save.set_function("GetCachePoseName", [](const TLuaAnimNode<FAnimNode_SaveCachedPose>& Node) { return Node.Get()->GetCachePoseName().ToString(); });
	auto Use = RegisterNode<FAnimNode_UseCachedPose>(Lua, "FAnimNode_UseCachedPose");
	Use.set_function("SetCachedPoseNode", [](const TLuaAnimNode<FAnimNode_UseCachedPose>& Node, const TLuaAnimNode<FAnimNode_SaveCachedPose>* Input)
	{
		auto* Native = ResolveTyped(Node.Owner, Input);
		KeepInput(Node.Lifetime, "Cache", Input ? Input->Lifetime : nullptr);
		Node.Get()->SetCachedPoseNode(Native);
	});
	Use.set_function("GetCachedPoseNode", [](const TLuaAnimNode<FAnimNode_UseCachedPose>& Node, sol::this_state State)
	{
		return NodeReference(State, Node.Owner, Node.Get()->GetCachedPoseNode());
	});
}

void BindDefinitions(sol::state& Lua, const std::shared_ptr<FLuaAnimationGraphBindings>& Bindings)
{
	auto State = Lua.new_usertype<FLuaAnimState>("FAnimState", sol::no_constructor, "IsValid", &FLuaAnimState::IsValid);
	State.set_function("SetStateResult", [](const FLuaAnimState& Value, const TLuaAnimNode<FAnimNode_StateResult>* Result)
	{
		auto* Native = ResolveTyped(Value.Owner, Result);
		KeepInput(Value.Lifetime, "Result", Result ? Result->Lifetime : nullptr);
		Value.Get()->SetStateResult(Native);
	});
	State.set_function("GetStateResult", [](const FLuaAnimState& Value, sol::this_state LuaState)
	{
		return NodeReference(LuaState, Value.Owner, Value.Get()->GetStateResult());
	});
	auto Transition = Lua.new_usertype<FLuaAnimTransition>("FAnimTransition", sol::no_constructor, "IsValid", &FLuaAnimTransition::IsValid);
	Transition.set_function("SetTransitionResult", [](const FLuaAnimTransition& Value, const TLuaAnimNode<FAnimNode_TransitionResult>* Result)
	{
		auto* Native = ResolveTyped(Value.Owner, Result);
		KeepInput(Value.Lifetime, "Result", Result ? Result->Lifetime : nullptr);
		Value.Get()->SetTransitionResult(Native);
	});
	Transition.set_function("GetTransitionResult", [](const FLuaAnimTransition& Value, sol::this_state LuaState)
	{
		return NodeReference(LuaState, Value.Owner, Value.Get()->GetTransitionResult());
	});
	Transition.set_function("SetFromStateIndex", [](const FLuaAnimTransition& Value, int32 Index) { Value.Get()->SetFromStateIndex(Index); });
	Transition.set_function("GetFromStateIndex", [](const FLuaAnimTransition& Value) { return Value.Get()->GetFromStateIndex(); });
	Transition.set_function("SetToStateIndex", [](const FLuaAnimTransition& Value, int32 Index) { Value.Get()->SetToStateIndex(Index); });
	Transition.set_function("GetToStateIndex", [](const FLuaAnimTransition& Value) { return Value.Get()->GetToStateIndex(); });
	Transition.set_function("SetPriority", [](const FLuaAnimTransition& Value, int32 Priority) { Value.Get()->SetPriority(Priority); });
	Transition.set_function("GetPriority", [](const FLuaAnimTransition& Value) { return Value.Get()->GetPriority(); });
	Transition.set_function("SetBlendDuration", [](const FLuaAnimTransition& Value, float Duration)
	{
		if (!std::isfinite(Duration) || Duration < 0.0f) { throw std::runtime_error("BlendDuration must be finite and non-negative"); }
		Value.Get()->SetBlendDuration(Duration);
	});
	Transition.set_function("GetBlendDuration", [](const FLuaAnimTransition& Value) { return Value.Get()->GetBlendDuration(); });
	Transition.set_function("SetConditionFunction", [Bindings](const FLuaAnimTransition& Value, sol::optional<sol::protected_function> Function)
	{
		FAnimTransition* Native = Value.Get();
		if (!Function || !Function->valid()) { Native->SetConditionFunction({}); return; }
		auto Condition = std::make_shared<FLuaAnimationCondition>();
		Condition->Function = std::move(*Function);
		Condition->Instance = Value.Owner.Get();
		Condition->UUID = Value.Owner.UUID;
		std::erase_if(Bindings->Conditions, [](const auto& Entry) { return Entry.expired(); });
		Bindings->Conditions.emplace_back(Condition);
		Native->SetConditionFunction([Condition, WeakBindings = std::weak_ptr(Bindings)](const FAnimationUpdateContext& Context)
		{
			if (!Condition->Function.valid()) { return false; }
			// 복사 가능한 값 스냅샷이다. Update의 스택 Context 포인터를 Lua에 노출하지 않는다.
			sol::protected_function Function = Condition->Function;
			sol::state_view State(Function.lua_state());
			sol::table Input = State.create_table_with("AnimInstance", FLuaAnimInstance(Context.AnimInstance, WeakBindings.lock()),
				"DeltaTime", Context.GetDeltaTime(), "FinalBlendWeight", Context.GetFinalBlendWeight());
			sol::protected_function_result Result = Function(Input);
			if (!Result.valid())
			{
				sol::error Error = Result;
				HTR_LOG(Error, "[Lua] Animation transition condition failed: {}", Error.what());
				return false;
			}
			if (Result.get_type() != sol::type::boolean)
			{
				HTR_LOG(Error, "[Lua] Animation transition condition must return a bool");
				return false;
			}
			return Result.get<bool>();
		});
	});
}
}

std::shared_ptr<FLuaAnimationGraphBindings> BindAnimationGraphAPI(sol::state& Lua)
{
	auto Bindings = std::make_shared<FLuaAnimationGraphBindings>();
	BindNodes(Lua);
	BindDefinitions(Lua, Bindings);
	auto Instance = Lua.new_usertype<FLuaAnimInstance>("UAnimInstance", sol::no_constructor, "IsValid", &FLuaAnimInstance::IsValid);
	Instance.set_function("CreateNode", &CreateNode);
	Instance.set_function("CreateState", [](const FLuaAnimInstance& Owner)
	{
		auto Lifetime = CreateGraphObject<FAnimState>(Owner);
		return FLuaAnimState{ Owner, static_cast<FAnimState*>(Lifetime->Value.get()), Lifetime };
	});
	Instance.set_function("CreateTransition", [](const FLuaAnimInstance& Owner)
	{
		auto Lifetime = CreateGraphObject<FAnimTransition>(Owner);
		return FLuaAnimTransition{ Owner, static_cast<FAnimTransition*>(Lifetime->Value.get()), Lifetime };
	});
	Instance.set_function("SetRootNode", [Bindings](const FLuaAnimInstance& Owner, const FLuaAnimNode* Node)
	{
		ULuaAnimInstance* LuaInstance = RequireLuaAnimInstance(Owner.Get());
		FAnimNode_Base* Native = ResolveInput(Owner, Node);
		// 기존 Root를 초기화하는 동안에도 수명이 유지되어야 한다.
		auto Previous = Bindings->Roots.extract(Owner.UUID);
		if (Node) { Bindings->Roots.emplace(Owner.UUID, FLuaAnimationRoot{ Owner.Get(), Owner.UUID, Native, Node->Lifetime }); }
		LuaInstance->SetGraphRoot(Native);
	});
	Instance.set_function("GetRootNode", [](const FLuaAnimInstance& Owner, sol::this_state State)
	{
		return NodeReference(State, Owner, RequireLuaAnimInstance(Owner.Get())->GetGraphRoot());
	});
	Instance.set_function("InitializeAnimation", [](const FLuaAnimInstance& Owner) { Owner.Get()->InitializeAnimation(); });
	Instance.set_function("UninitializeAnimation", [](const FLuaAnimInstance& Owner) { Owner.Get()->UninitializeAnimation(); });
	Instance.set_function("IsInitialized", [](const FLuaAnimInstance& Owner) { return Owner.Get()->IsInitialized(); });

	sol::usertype<USkeletalMeshComponent> Mesh = Lua["USkeletalMeshComponent"];
	Mesh.set_function("CreateAnimInstance", [Bindings](USkeletalMeshComponent& Component)
	{
		if (!FindLiveObjectByAddress(&Component)) { throw std::runtime_error("SkeletalMeshComponent has been destroyed"); }
		if (!Component.GetSkeletalMesh() || !Component.IsRegistered())
		{
			throw std::runtime_error("CreateAnimInstance requires a registered SkeletalMeshComponent with a mesh");
		}
		// Component가 실제 Instance를 소유한다. 이미 Lua 어댑터면 기존 그래프를 유지한다.
		Component.SetAnimInstanceClass(ULuaAnimInstance::StaticClass());
		Component.InitAnim(false);
		return FLuaAnimInstance(Component.GetAnimInstance(), Bindings);
	});
	Mesh.set_function("GetAnimInstance", [Bindings](const USkeletalMeshComponent& Component, sol::this_state State)
	{
		if (!FindLiveObjectByAddress(&Component)) { throw std::runtime_error("SkeletalMeshComponent has been destroyed"); }
		UAnimInstance* Native = Component.GetAnimInstance();
		return Native ? sol::make_object(State, FLuaAnimInstance(Native, Bindings)) : sol::make_object(State, sol::nil);
	});
	return Bindings;
}

void CollectAnimationGraphReferences(const std::shared_ptr<FLuaAnimationGraphBindings>& Bindings)
{
	if (!Bindings) { return; }
	std::erase_if(Bindings->Roots, [](const auto& Entry)
	{
		const FLuaAnimationRoot& Root = Entry.second;
		UObject* Live = FindLiveObjectByAddress(Root.Instance);
		ULuaAnimInstance* LuaInstance = Live && Live->GetUUID() == Root.UUID ? Cast<ULuaAnimInstance>(Live) : nullptr;
		return !LuaInstance || LuaInstance->GetGraphRoot() != Root.Node;
	});
	std::erase_if(Bindings->Conditions, [](const auto& Entry)
	{
		auto Condition = Entry.lock();
		if (!Condition) { return true; }
		UObject* Live = FindLiveObjectByAddress(Condition->Instance);
		if (!Live || Live->GetUUID() != Condition->UUID)
		{
			Condition->Function = sol::nil;
			return true;
		}
		return false;
	});
	std::erase_if(Bindings->Objects, [](const auto& Entry) { return Entry.second.expired(); });
}

void ReleaseAnimationGraphCallbacks(const std::shared_ptr<FLuaAnimationGraphBindings>& Bindings)
{
	if (!Bindings) { return; }
	// Lua가 그래프를 해제하기 전에 살아 있는 Instance의 raw Root를 분리한다.
	for (const auto& [UUID, Root] : Bindings->Roots)
	{
		UObject* Live = FindLiveObjectByAddress(Root.Instance);
		ULuaAnimInstance* LuaInstance = Live && Live->GetUUID() == UUID ? Cast<ULuaAnimInstance>(Live) : nullptr;
		if (LuaInstance && LuaInstance->GetGraphRoot() == Root.Node)
		{
			LuaInstance->SetGraphRoot(nullptr);
		}
	}
	for (const auto& Entry : Bindings->Conditions)
	{
		if (auto Condition = Entry.lock()) { Condition->Function = sol::nil; }
	}
	Bindings->Conditions.clear();
	Bindings->Roots.clear();
	Bindings->Objects.clear();
}
