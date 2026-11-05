#include "EnginePCH.h"
#include "Scripting/CameraManagerBindings.h"

#include "Camera/CameraComponent.h"
#include "Camera/CameraModifier_CameraShake.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"

#include <sol/sol.hpp>

namespace
{
class FLuaPlayerCameraManager;
class FLuaCameraShakeModifier;

class FLuaCameraModifier
{
	friend class FLuaPlayerCameraManager;
	friend class FLuaCameraShakeModifier;

public:
	explicit FLuaCameraModifier(UCameraModifier* Modifier);

public:
	bool IsValid() const;
	bool Equals(const FLuaCameraModifier& Other) const;
	bool EnableModifier() const;
	bool DisableModifier(sol::optional<bool> Immediately) const;
	bool IsDisabled() const;
	bool IsPendingDisable() const;
	float GetAlpha() const;
	int32 GetPriority() const;
	bool SetPriority(int32 Priority) const;
	bool SetBlendTimes(float BlendInTime, float BlendOutTime) const;
	sol::object GetCameraOwner(sol::this_state State) const;

private:
	UCameraModifier* Resolve() const;

private:
	uint32 mObjectUUID{};
};

class FLuaCameraShakeModifier : public FLuaCameraModifier
{
public:
	explicit FLuaCameraShakeModifier(UCameraModifier_CameraShake* Modifier);

public:
	bool StopShake(sol::optional<bool> Immediately) const;
	bool IsFinished() const;
	float GetShakeScale() const;
	bool SetShakeScale(float Scale) const;
};

class FLuaPlayerCameraManager
{
public:
	explicit FLuaPlayerCameraManager(APlayerCameraManager* Manager);

public:
	bool IsValid() const;
	bool Equals(const FLuaPlayerCameraManager& Other) const;
	sol::object AddCameraModifier(const FString& ClassName, sol::optional<sol::table> Parameters, sol::this_state State) const;
	sol::object FindCameraModifierByClass(const FString& ClassName, sol::this_state State) const;
	bool RemoveCameraModifier(const FLuaCameraModifier* Modifier) const;
	bool RemoveAllCameraModifiers() const;
	AActor* GetViewTarget() const;
	UCameraComponent* GetViewTargetCamera() const;
	bool SetViewTarget(AActor* Target) const;
	bool SetViewTargetWithBlend(AActor* Target, float BlendTime) const;
	bool SetViewTargetCamera(UCameraComponent* Camera, sol::optional<float> BlendTime) const;
	bool HasValidCameraView() const;

private:
	APlayerCameraManager* Resolve() const;

private:
	uint32 mObjectUUID{};
};

UObject* FindLiveObject(uint32 ObjectUUID)
{
	if (ObjectUUID == 0)
	{
		return nullptr;
	}
	for (UObject* Object : GUObjectArray)
	{
		if (Object && Object->GetUUID() == ObjectUUID)
		{
			return Object;
		}
	}
	return nullptr;
}

sol::object MakeModifierReference(UCameraModifier* Modifier, sol::this_state State)
{
	if (UCameraModifier_CameraShake * Shake{ Cast<UCameraModifier_CameraShake>(Modifier) })
	{
		return sol::make_object(State, FLuaCameraShakeModifier{ Shake });
	}
	return Modifier ? sol::make_object(State, FLuaCameraModifier{ Modifier }) : sol::make_object(State, sol::nil);
}

void ReadOscillator(const sol::table& Definition, const char* Name, float& Amplitude, float& Frequency)
{
	if (sol::optional<sol::table> Oscillator{ Definition.get<sol::optional<sol::table>>(Name) })
	{
		Amplitude = Oscillator->get_or("Amplitude", Amplitude);
		Frequency = Oscillator->get_or("Frequency", Frequency);
	}
}

void ReadSimplePattern(FSimpleCameraShakePattern& Pattern, const sol::table& Definition)
{
	Pattern.StateParams.Duration = Definition.get_or("Duration", Pattern.StateParams.Duration);
	Pattern.StateParams.BlendInTime = Definition.get_or("BlendInTime", Pattern.StateParams.BlendInTime);
	Pattern.StateParams.BlendOutTime = Definition.get_or("BlendOutTime", Pattern.StateParams.BlendOutTime);
}

std::unique_ptr<FCameraShakePattern> ReadShakePattern(const sol::table& Definition, uint32 Depth = 0)
{
	if (Depth >= 32)
	{
		return nullptr;
	}
	const FString Type{ Definition.get_or("Type", FString{ "Wave" }) };
	const char* Names[]{ "X", "Y", "Z", "Pitch", "Yaw", "Roll", "FOV" };
	if (Type == "Wave")
	{
		auto Pattern{ std::make_unique<FWaveOscillatorCameraShakePattern>() };
		ReadSimplePattern(*Pattern, Definition);
		Pattern->LocationAmplitudeMultiplier = Definition.get_or("LocationAmplitudeMultiplier", Pattern->LocationAmplitudeMultiplier);
		Pattern->LocationFrequencyMultiplier = Definition.get_or("LocationFrequencyMultiplier", Pattern->LocationFrequencyMultiplier);
		Pattern->RotationAmplitudeMultiplier = Definition.get_or("RotationAmplitudeMultiplier", Pattern->RotationAmplitudeMultiplier);
		Pattern->RotationFrequencyMultiplier = Definition.get_or("RotationFrequencyMultiplier", Pattern->RotationFrequencyMultiplier);
		FWaveOscillator* Oscillators[]{ &Pattern->X, &Pattern->Y, &Pattern->Z, &Pattern->Pitch, &Pattern->Yaw, &Pattern->Roll, &Pattern->FOV };
		for (int32 Index{}; Index < 7; ++Index)
		{
			ReadOscillator(Definition, Names[Index], Oscillators[Index]->Amplitude, Oscillators[Index]->Frequency);
			if (sol::optional<sol::table> Oscillator{ Definition.get<sol::optional<sol::table>>(Names[Index]) })
			{
				Oscillators[Index]->InitialOffsetType = Oscillator->get_or("InitialOffsetType", Oscillators[Index]->InitialOffsetType);
			}
		}
		return Pattern;
	}
	if (Type == "Perlin")
	{
		auto Pattern{ std::make_unique<FPerlinNoiseCameraShakePattern>() };
		ReadSimplePattern(*Pattern, Definition);
		Pattern->LocationAmplitudeMultiplier = Definition.get_or("LocationAmplitudeMultiplier", Pattern->LocationAmplitudeMultiplier);
		Pattern->LocationFrequencyMultiplier = Definition.get_or("LocationFrequencyMultiplier", Pattern->LocationFrequencyMultiplier);
		Pattern->RotationAmplitudeMultiplier = Definition.get_or("RotationAmplitudeMultiplier", Pattern->RotationAmplitudeMultiplier);
		Pattern->RotationFrequencyMultiplier = Definition.get_or("RotationFrequencyMultiplier", Pattern->RotationFrequencyMultiplier);
		FPerlinNoiseShaker* Oscillators[]{ &Pattern->X, &Pattern->Y, &Pattern->Z, &Pattern->Pitch, &Pattern->Yaw, &Pattern->Roll, &Pattern->FOV };
		for (int32 Index{}; Index < 7; ++Index)
		{
			ReadOscillator(Definition, Names[Index], Oscillators[Index]->Amplitude, Oscillators[Index]->Frequency);
		}
		return Pattern;
	}
	if (Type == "Composite")
	{
		const sol::optional<sol::table> Children{ Definition.get<sol::optional<sol::table>>("Children") };
		if (!Children)
		{
			return nullptr;
		}
		auto Pattern{ std::make_unique<FCompositeCameraShakePattern>() };
		for (size_t Index{ 1 }; Index <= Children->size(); ++Index)
		{
			const sol::optional<sol::table> Child{ Children->get<sol::optional<sol::table>>(Index) };
			if (!Child)
			{
				return nullptr;
			}
			auto ChildPattern{ ReadShakePattern(*Child, Depth + 1) };
			if (!ChildPattern)
			{
				return nullptr;
			}
			Pattern->AddChildPattern(std::move(ChildPattern));
		}
		return Pattern;
	}
	return nullptr;
}

FLuaCameraModifier::FLuaCameraModifier(UCameraModifier* Modifier)
{
	mObjectUUID = Modifier ? Modifier->GetUUID() : 0;
}

UCameraModifier* FLuaCameraModifier::Resolve() const
{
	UCameraModifier* Modifier{ Cast<UCameraModifier>(FindLiveObject(mObjectUUID)) };
	return Modifier && Modifier->GetCameraOwner() ? Modifier : nullptr;
}

bool FLuaCameraModifier::IsValid() const
{
	return Resolve() != nullptr;
}

bool FLuaCameraModifier::Equals(const FLuaCameraModifier& Other) const
{
	return mObjectUUID == Other.mObjectUUID;
}

bool FLuaCameraModifier::EnableModifier() const
{
	if (UCameraModifier * Modifier{ Resolve() })
	{
		Modifier->EnableModifier();
		return true;
	}
	return false;
}

bool FLuaCameraModifier::DisableModifier(sol::optional<bool> Immediately) const
{
	if (UCameraModifier * Modifier{ Resolve() })
	{
		Modifier->DisableModifier(Immediately.value_or(false));
		return true;
	}
	return false;
}

bool FLuaCameraModifier::IsDisabled() const
{
	const UCameraModifier* Modifier{ Resolve() };
	return !Modifier || Modifier->IsDisabled();
}

bool FLuaCameraModifier::IsPendingDisable() const
{
	const UCameraModifier* Modifier{ Resolve() };
	return Modifier && Modifier->IsPendingDisable();
}

float FLuaCameraModifier::GetAlpha() const
{
	const UCameraModifier* Modifier{ Resolve() };
	return Modifier ? Modifier->GetAlpha() : 0.0f;
}

int32 FLuaCameraModifier::GetPriority() const
{
	const UCameraModifier* Modifier{ Resolve() };
	return Modifier ? Modifier->GetPriority() : 0;
}

bool FLuaCameraModifier::SetPriority(int32 Priority) const
{
	if (UCameraModifier * Modifier{ Resolve() })
	{
		Modifier->SetPriority(Priority);
		return true;
	}
	return false;
}

bool FLuaCameraModifier::SetBlendTimes(float BlendInTime, float BlendOutTime) const
{
	if (UCameraModifier * Modifier{ Resolve() })
	{
		Modifier->SetBlendTimes(BlendInTime, BlendOutTime);
		return true;
	}
	return false;
}

sol::object FLuaCameraModifier::GetCameraOwner(sol::this_state State) const
{
	UCameraModifier* Modifier{ Resolve() };
	return Modifier ? sol::make_object(State, FLuaPlayerCameraManager{ Modifier->GetCameraOwner() }) : sol::make_object(State, sol::nil);
}

FLuaCameraShakeModifier::FLuaCameraShakeModifier(UCameraModifier_CameraShake* Modifier)
	: FLuaCameraModifier(Modifier)
{
}

bool FLuaCameraShakeModifier::StopShake(sol::optional<bool> Immediately) const
{
	if (UCameraModifier_CameraShake * Modifier{ Cast<UCameraModifier_CameraShake>(Resolve()) })
	{
		Modifier->StopShake(Immediately.value_or(false));
		return true;
	}
	return false;
}

bool FLuaCameraShakeModifier::IsFinished() const
{
	const UCameraModifier_CameraShake* Modifier{ Cast<UCameraModifier_CameraShake>(Resolve()) };
	return !Modifier || Modifier->IsFinished();
}

float FLuaCameraShakeModifier::GetShakeScale() const
{
	const UCameraModifier_CameraShake* Modifier{ Cast<UCameraModifier_CameraShake>(Resolve()) };
	return Modifier ? Modifier->GetShakeScale() : 0.0f;
}

bool FLuaCameraShakeModifier::SetShakeScale(float Scale) const
{
	if (UCameraModifier_CameraShake * Modifier{ Cast<UCameraModifier_CameraShake>(Resolve()) })
	{
		Modifier->SetShakeScale(Scale);
		return true;
	}
	return false;
}

FLuaPlayerCameraManager::FLuaPlayerCameraManager(APlayerCameraManager* Manager)
{
	mObjectUUID = Manager ? Manager->GetUUID() : 0;
}

APlayerCameraManager* FLuaPlayerCameraManager::Resolve() const
{
	return Cast<APlayerCameraManager>(FindLiveObject(mObjectUUID));
}

bool FLuaPlayerCameraManager::IsValid() const
{
	return Resolve() != nullptr;
}

bool FLuaPlayerCameraManager::Equals(const FLuaPlayerCameraManager& Other) const
{
	return mObjectUUID == Other.mObjectUUID;
}

sol::object FLuaPlayerCameraManager::AddCameraModifier(const FString& ClassName, sol::optional<sol::table> Parameters, sol::this_state State) const
{
	APlayerCameraManager* Manager{ Resolve() };
	UClass* ModifierClass{ FindClass(ClassName) };
	if (!Manager || !ModifierClass || !ModifierClass->IsChildOf(UCameraModifier::StaticClass()))
	{
		return sol::make_object(State, sol::nil);
	}
	UCameraModifier* Modifier{};
	if (ModifierClass == UCameraModifier_CameraShake::StaticClass())
	{
		const sol::optional<sol::table> Definition{ Parameters ? Parameters->get<sol::optional<sol::table>>("Pattern") : sol::nullopt };
		if (!Definition)
		{
			return sol::make_object(State, sol::nil);
		}
		auto Pattern{ ReadShakePattern(*Definition) };
		if (!Pattern)
		{
			return sol::make_object(State, sol::nil);
		}
		const float Scale{ Parameters->get_or("Scale", 1.0f) };
		const ECameraShakePlaySpace PlaySpace{ Parameters->get_or("PlaySpace", ECameraShakePlaySpace::CameraLocal) };
		const FRotator UserPlaySpaceRot{ Parameters->get<sol::optional<FRotator>>("UserPlaySpaceRot").value_or(FRotator{}) };
		if (PlaySpace != ECameraShakePlaySpace::CameraLocal && PlaySpace != ECameraShakePlaySpace::World && PlaySpace != ECameraShakePlaySpace::UserDefined)
		{
			return sol::make_object(State, sol::nil);
		}
		Modifier = Manager->AddCameraModifier<UCameraModifier_CameraShake>(std::move(Pattern), Scale, PlaySpace, UserPlaySpaceRot);
	}
	else
	{
		Modifier = Manager->AddCameraModifier(ModifierClass);
	}
	if (Modifier && Parameters)
	{
		if (sol::optional<int32> Priority{ Parameters->get<sol::optional<int32>>("Priority") })
		{
			Modifier->SetPriority(*Priority);
		}
	}
	return MakeModifierReference(Modifier, State);
}

sol::object FLuaPlayerCameraManager::FindCameraModifierByClass(const FString& ClassName, sol::this_state State) const
{
	APlayerCameraManager* Manager{ Resolve() };
	return MakeModifierReference(Manager ? Manager->FindCameraModifierByClass(FindClass(ClassName)) : nullptr, State);
}

bool FLuaPlayerCameraManager::RemoveCameraModifier(const FLuaCameraModifier* Modifier) const
{
	APlayerCameraManager* Manager{ Resolve() };
	UCameraModifier* NativeModifier{ Modifier ? Modifier->Resolve() : nullptr };
	return Manager && NativeModifier && Manager->RemoveCameraModifier(NativeModifier);
}

bool FLuaPlayerCameraManager::RemoveAllCameraModifiers() const
{
	if (APlayerCameraManager * Manager{ Resolve() })
	{
		Manager->RemoveAllCameraModifiers();
		return true;
	}
	return false;
}

AActor* FLuaPlayerCameraManager::GetViewTarget() const
{
	APlayerCameraManager* Manager{ Resolve() };
	return Manager ? Manager->GetViewTarget() : nullptr;
}

UCameraComponent* FLuaPlayerCameraManager::GetViewTargetCamera() const
{
	APlayerCameraManager* Manager{ Resolve() };
	return Manager ? Manager->GetViewTargetCamera() : nullptr;
}

bool FLuaPlayerCameraManager::SetViewTarget(AActor* Target) const
{
	APlayerCameraManager* Manager{ Resolve() };
	return Manager && (!Target || Cast<AActor>(FindLiveObjectByAddress(Target))) && Manager->SetViewTarget(Target);
}

bool FLuaPlayerCameraManager::SetViewTargetWithBlend(AActor* Target, float BlendTime) const
{
	APlayerCameraManager* Manager{ Resolve() };
	return Manager && (!Target || Cast<AActor>(FindLiveObjectByAddress(Target))) && Manager->SetViewTargetWithBlend(Target, BlendTime);
}

bool FLuaPlayerCameraManager::SetViewTargetCamera(UCameraComponent* Camera, sol::optional<float> BlendTime) const
{
	APlayerCameraManager* Manager{ Resolve() };
	return Manager && (!Camera || Cast<UCameraComponent>(FindLiveObjectByAddress(Camera))) && Manager->SetViewTargetCamera(Camera, BlendTime.value_or(0.0f));
}

bool FLuaPlayerCameraManager::HasValidCameraView() const
{
	const APlayerCameraManager* Manager{ Resolve() };
	return Manager && Manager->HasValidCameraView();
}

sol::object GetActorCameraManager(const AActor* Actor, sol::this_state State)
{
	const AActor* LiveActor{ Cast<AActor>(FindLiveObjectByAddress(Actor)) };
	APlayerCameraManager* Manager{ LiveActor && LiveActor->GetWorld() ? LiveActor->GetWorld()->GetPlayerCameraManager() : nullptr };
	return Manager ? sol::make_object(State, FLuaPlayerCameraManager{ Manager }) : sol::make_object(State, sol::nil);
}
}

void BindPlayerCameraManagerAPI(sol::state& LuaState)
{
	LuaState.new_enum("ECameraShakePlaySpace", "CameraLocal", ECameraShakePlaySpace::CameraLocal, "World", ECameraShakePlaySpace::World, "UserDefined", ECameraShakePlaySpace::UserDefined);
	LuaState.new_enum("EInitialWaveOscillatorOffsetType", "Random", EInitialWaveOscillatorOffsetType::Random, "Zero", EInitialWaveOscillatorOffsetType::Zero);
	LuaState.new_usertype<FLuaCameraModifier>("UCameraModifier", sol::no_constructor, sol::meta_function::equal_to, &FLuaCameraModifier::Equals, "IsValid", &FLuaCameraModifier::IsValid, "EnableModifier", &FLuaCameraModifier::EnableModifier, "DisableModifier", &FLuaCameraModifier::DisableModifier, "IsDisabled", &FLuaCameraModifier::IsDisabled, "IsPendingDisable", &FLuaCameraModifier::IsPendingDisable, "GetAlpha", &FLuaCameraModifier::GetAlpha, "GetPriority", &FLuaCameraModifier::GetPriority, "SetPriority", &FLuaCameraModifier::SetPriority, "SetBlendTimes", &FLuaCameraModifier::SetBlendTimes, "GetCameraOwner", &FLuaCameraModifier::GetCameraOwner);
	LuaState.new_usertype<FLuaCameraShakeModifier>("UCameraModifier_CameraShake", sol::no_constructor, sol::base_classes, sol::bases<FLuaCameraModifier>(), sol::meta_function::equal_to, &FLuaCameraModifier::Equals, "StopShake", &FLuaCameraShakeModifier::StopShake, "IsFinished", &FLuaCameraShakeModifier::IsFinished, "GetShakeScale", &FLuaCameraShakeModifier::GetShakeScale, "SetShakeScale", &FLuaCameraShakeModifier::SetShakeScale);
	LuaState.new_usertype<FLuaPlayerCameraManager>("APlayerCameraManager", sol::no_constructor, sol::meta_function::equal_to, &FLuaPlayerCameraManager::Equals, "IsValid", &FLuaPlayerCameraManager::IsValid, "AddCameraModifier", &FLuaPlayerCameraManager::AddCameraModifier, "FindCameraModifierByClass", &FLuaPlayerCameraManager::FindCameraModifierByClass, "RemoveCameraModifier", &FLuaPlayerCameraManager::RemoveCameraModifier, "RemoveAllCameraModifiers", &FLuaPlayerCameraManager::RemoveAllCameraModifiers, "GetViewTarget", &FLuaPlayerCameraManager::GetViewTarget, "GetViewTargetCamera", &FLuaPlayerCameraManager::GetViewTargetCamera, "SetViewTarget", &FLuaPlayerCameraManager::SetViewTarget, "SetViewTargetWithBlend", &FLuaPlayerCameraManager::SetViewTargetWithBlend, "SetViewTargetCamera", &FLuaPlayerCameraManager::SetViewTargetCamera, "HasValidCameraView", &FLuaPlayerCameraManager::HasValidCameraView);
	sol::usertype<AActor> ActorType{ LuaState.get<sol::usertype<AActor>>("AActor") };
	ActorType.set_function("GetPlayerCameraManager", &GetActorCameraManager);
}

void BindPlayerCameraManagerEnvironment(sol::environment& Environment, AActor* Owner)
{
	const uint32 OwnerUUID{ Owner ? Owner->GetUUID() : 0 };
	Environment.set_function("GetPlayerCameraManager", [OwnerUUID](sol::this_state State)
		{ return GetActorCameraManager(Cast<AActor>(FindLiveObject(OwnerUUID)), State); });
}
