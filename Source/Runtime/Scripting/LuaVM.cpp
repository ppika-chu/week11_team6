#include "EnginePCH.h"
#include "Scripting/LuaVM.h"
#include "Scripting/CameraManagerBindings.h"
#include "Sequence/SequenceActor.h"

#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Camera/CameraComponent.h"
#include "Math/Vector.h"
#include "Math/Vector2.h"
#include "Math/Rotator.h"
#include "Collision/HitResult.h"
#include "Audio/AudioSystem.h"
#include "Engine/Engine.h"
#include "Core/ResourcePaths.h"
#include "Render/UIRenderer.h"
#include "Render/RenderResourceManager.h"
#include "Render/Texture2D.h"
#include "Text/Font.h"
#include "Input/InputSystem.h"

#include <sol/sol.hpp>
#include <luajit.h>
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace
{
bool CheckLuaResult(
	sol::protected_function_result& Result,
	const FString& Context)
{
	if (Result.valid())
	{
		return true;
	}

	sol::error Error = Result;
	HTR_LOG(
		Error,
		"[Lua] {} failed: {}",
		Context,
		Error.what());

	return false;
}

FString LuaValueToString(const sol::object& Value)
{
	switch (Value.get_type())
	{
	case sol::type::nil:
		return "nil";

	case sol::type::string:
		return Value.as<FString>();

	case sol::type::number:
		return std::format("{}", Value.as<double>());

	case sol::type::boolean:
		return Value.as<bool>() ? "true" : "false";

	default:
		return "<value>";
	}
}

void ConfigurePackagePath(sol::state& LuaState)
{
	sol::table Package = LuaState["package"];
	const FString DefaultPath = Package.get_or<FString>("path", "");
	Package["path"] = "Assets/Scripts/?.lua;" + DefaultPath;
}

void BindLoggingAPI(sol::state& LuaState)
{
	auto LogFunction = [](sol::variadic_args Args)
	{
		FString Message;

		for (const auto& Argument : Args)
		{
			if (!Message.empty())
			{
				Message += " ";
			}

			const sol::object Value = Argument;
			Message += LuaValueToString(Value);
		}

		HTR_LOG(Info, "[Lua] {}", Message);

		Message += "\n";
		OutputDebugStringA(Message.c_str());
	};

	// Lua에서 Log("Score:", 100, true)처럼 호출할 수 있습니다.
	LuaState.set_function("Log", LogFunction);

	// 기본 Lua print도 에디터 Output Log 쪽으로 연결합니다.
	LuaState.set_function("print", LogFunction);
}

void BindVectorAPI(sol::state& LuaState)
{
	LuaState.new_usertype<FVector>(
		"FVector",
		sol::constructors<
			FVector(),
			FVector(float, float, float)>(),
		"X", &FVector::X,
		"Y", &FVector::Y,
		"Z", &FVector::Z,
		"Set", &FVector::Set,
		"Size", &FVector::Size,
		"SizeSquared", &FVector::SizeSquared,
		"Length", &FVector::Length,
		"Normalized", &FVector::Normalized,
		"Add", &FVector::Add,
		"Subtract", &FVector::Subtract,
		"Dot", [](const FVector& A, const FVector& B)
		{ return FVector::Dot(A, B); },
		"Cross", [](const FVector& A, const FVector& B)
		{ return FVector::Cross(A, B); },
		"Distance", [](const FVector& A, const FVector& B)
		{ return FVector::Distance(A, B); },
		sol::meta_function::addition,
		[](const FVector& A, const FVector& B)
		{
			return A + B;
		},
		sol::meta_function::subtraction,
		[](const FVector& A, const FVector& B)
		{
			return A - B;
		},
		sol::meta_function::multiplication,
		[](const FVector& Vector, float Scalar)
		{
			return Vector * Scalar;
		},
		sol::meta_function::equal_to,
		[](const FVector& A, const FVector& B)
		{
			return A == B;
		});
}

void BindRotatorAPI(sol::state& LuaState)
{
	LuaState.new_usertype<FRotator>(
		"FRotator",
		sol::constructors<
			FRotator(),
			FRotator(float, float, float)>(),
		"Pitch", &FRotator::Pitch,
		"Yaw", &FRotator::Yaw,
		"Roll", &FRotator::Roll);
}

FAudioSystem* GetAudio()
{
	return GEngine ? GEngine->GetAudioSystem() : nullptr;
}

void BindAudioAPI(sol::state& LuaState)
{
	LuaState.set_function("PlaySound",
		[](const FString& Path, sol::optional<bool> bLoop, sol::optional<float> Volume) -> uint32
		{
			FAudioSystem* Audio = GetAudio();
			return Audio ? Audio->Play(Path, bLoop.value_or(false), Volume.value_or(1.0f)) : 0;
		});

	LuaState.set_function("StopSound", [](uint32 Handle)
		{
				if (FAudioSystem* Audio = GetAudio())
				{
					Audio->Stop(Handle);
				} });

	LuaState.set_function("StopAllSounds", []()
		{
				if (FAudioSystem* Audio = GetAudio())
				{
					Audio->StopAll();
				} });

	LuaState.set_function("SetSoundVolume", [](uint32 Handle, float Volume)
		{
				if (FAudioSystem* Audio = GetAudio())
				{
					Audio->SetVolume(Handle, Volume);
				} });

	LuaState.set_function("SetSoundPitch", [](uint32 Handle, float Pitch)
		{
				if (FAudioSystem* Audio = GetAudio())
				{
					Audio->SetPitch(Handle, Pitch);
				} });

	LuaState.set_function("FadeSound", [](uint32 Handle, float TargetVolume, float Duration)
		{
				if (FAudioSystem* Audio = GetAudio())
				{
					Audio->FadeTo(Handle, TargetVolume, Duration);
				} });

	LuaState.set_function("IsSoundPlaying", [](uint32 Handle)
		{
				FAudioSystem* Audio = GetAudio();
				return Audio && Audio->IsPlaying(Handle); });

	LuaState.set_function("SetMasterVolume", [](float Volume)
		{
				if (FAudioSystem* Audio = GetAudio())
				{
					Audio->SetMasterVolume(Volume);
				} });
}

void BindCoroutineAPI(sol::state& LuaState, CoroutineScheduler& Scheduler)
{
	LuaState.set_function("RegisterCoroutine", [&Scheduler](sol::thread Co)
		{ Scheduler.RegisterLuaCoroutine(Co); });

	LuaState.set_function("StartCoroutine", [&LuaState, &Scheduler](sol::thread Co)
		{
			sol::protected_function Resume = LuaState["coroutine"]["resume"];
			Scheduler.RunCoroutine(LuaState, Resume, Co); });

	// 액터별 Environment는 이 State의 공통 함수를 상속한다.
	sol::protected_function_result Result = LuaState.safe_script(R"lua(
		function Wait(seconds)
		coroutine.yield("wait", seconds)
		end

		function WaitUntil(condition)
		coroutine.yield("wait_until", condition)
		end
		)lua",
		sol::script_pass_on_error);
	if (!CheckLuaResult(Result, "Coroutine helpers"))
	{
		throw std::runtime_error("Failed to initialize Coroutine helpers");
	}
}

void BindInputAPI(sol::state& LuaState)
{
	LuaState.set_function("IsKeyDown", &FInputSystem::IsKeyDown);
	LuaState.set_function("IsKeyPressed", &FInputSystem::IsKeyPressed);
	LuaState.set_function("IsKeyReleased", &FInputSystem::IsKeyReleased);

	LuaState.set_function("IsMouseDown", &FInputSystem::IsMouseDown);
	LuaState.set_function("IsMousePressed", &FInputSystem::IsMousePressed);
	LuaState.set_function("IsMouseReleased", &FInputSystem::IsMouseReleased);
	LuaState.set_function("GetMouseDeltaX", &FInputSystem::GetMouseDeltaX);
	LuaState.set_function("GetMouseDeltaY", &FInputSystem::GetMouseDeltaY);

	// SetCursorMode·GetCursorMode는 스크립트 World의 PlayerController를 쓰므로 스크립트 환경마다 등록한다.
	LuaState.new_enum("CursorMode", "Free", ECursorMode::Free, "Locked", ECursorMode::Locked);

	LuaState.new_enum("MouseButton", "Left", EMouseButton::Left, "Right", EMouseButton::Right, "Middle", EMouseButton::Middle, "Side1", EMouseButton::Side1, "Side2", EMouseButton::Side2);

	// IsKeyDown(Key.W) 와 같이 호출

	LuaState.new_enum("Key",
		"LButton", EKeyCode::LButton,
		"RButton", EKeyCode::RButton,
		"MButton", EKeyCode::MButton,
		"Backspace", EKeyCode::Backspace,
		"Tab", EKeyCode::Tab,
		"Clear", EKeyCode::Clear,
		"Enter", EKeyCode::Enter,
		"Shift", EKeyCode::Shift,
		"Control", EKeyCode::Control,
		"Alt", EKeyCode::Alt,
		"Pause", EKeyCode::Pause,
		"CapsLock", EKeyCode::CapsLock,
		"Escape", EKeyCode::Escape,
		"Space", EKeyCode::Space,
		"PageUp", EKeyCode::PageUp,
		"PageDown", EKeyCode::PageDown,
		"End", EKeyCode::End,
		"Home", EKeyCode::Home,
		"Left", EKeyCode::Left,
		"Up", EKeyCode::Up,
		"Right", EKeyCode::Right,
		"Down", EKeyCode::Down,
		"PrintScreen", EKeyCode::PrintScreen,
		"Insert", EKeyCode::Insert,
		"Delete", EKeyCode::Delete,
		"Num0", EKeyCode::Num0,
		"Num1", EKeyCode::Num1,
		"Num2", EKeyCode::Num2,
		"Num3", EKeyCode::Num3,
		"Num4", EKeyCode::Num4,
		"Num5", EKeyCode::Num5,
		"Num6", EKeyCode::Num6,
		"Num7", EKeyCode::Num7,
		"Num8", EKeyCode::Num8,
		"Num9", EKeyCode::Num9,
		"A", EKeyCode::A,
		"B", EKeyCode::B,
		"C", EKeyCode::C,
		"D", EKeyCode::D,
		"E", EKeyCode::E,
		"F", EKeyCode::F,
		"G", EKeyCode::G,
		"H", EKeyCode::H,
		"I", EKeyCode::I,
		"J", EKeyCode::J,
		"K", EKeyCode::K,
		"L", EKeyCode::L,
		"M", EKeyCode::M,
		"N", EKeyCode::N,
		"O", EKeyCode::O,
		"P", EKeyCode::P,
		"Q", EKeyCode::Q,
		"R", EKeyCode::R,
		"S", EKeyCode::S,
		"T", EKeyCode::T,
		"U", EKeyCode::U,
		"V", EKeyCode::V,
		"W", EKeyCode::W,
		"X", EKeyCode::X,
		"Y", EKeyCode::Y,
		"Z", EKeyCode::Z,
		"LWin", EKeyCode::LWin,
		"RWin", EKeyCode::RWin,
		"NumPad0", EKeyCode::NumPad0,
		"NumPad1", EKeyCode::NumPad1,
		"NumPad2", EKeyCode::NumPad2,
		"NumPad3", EKeyCode::NumPad3,
		"NumPad4", EKeyCode::NumPad4,
		"NumPad5", EKeyCode::NumPad5,
		"NumPad6", EKeyCode::NumPad6,
		"NumPad7", EKeyCode::NumPad7,
		"NumPad8", EKeyCode::NumPad8,
		"NumPad9", EKeyCode::NumPad9,
		"Multiply", EKeyCode::Multiply,
		"Add", EKeyCode::Add,
		"Separator", EKeyCode::Separator,
		"Subtract", EKeyCode::Subtract,
		"Decimal", EKeyCode::Decimal,
		"Divide", EKeyCode::Divide,
		"F1", EKeyCode::F1,
		"F2", EKeyCode::F2,
		"F3", EKeyCode::F3,
		"F4", EKeyCode::F4,
		"F5", EKeyCode::F5,
		"F6", EKeyCode::F6,
		"F7", EKeyCode::F7,
		"F8", EKeyCode::F8,
		"F9", EKeyCode::F9,
		"F10", EKeyCode::F10,
		"F11", EKeyCode::F11,
		"F12", EKeyCode::F12,
		"NumLock", EKeyCode::NumLock,
		"ScrollLock", EKeyCode::ScrollLock,
		"LShift", EKeyCode::LShift,
		"RShift", EKeyCode::RShift,
		"LControl", EKeyCode::LControl,
		"RControl", EKeyCode::RControl,
		"LAlt", EKeyCode::LAlt,
		"RAlt", EKeyCode::RAlt,
		"Semicolon", EKeyCode::Semicolon,
		"Equals", EKeyCode::Equals,
		"Comma", EKeyCode::Comma,
		"Minus", EKeyCode::Minus,
		"Period", EKeyCode::Period,
		"Slash", EKeyCode::Slash,
		"Tilde", EKeyCode::Tilde,
		"LeftBracket", EKeyCode::LeftBracket,
		"Backslash", EKeyCode::Backslash,
		"RightBracket", EKeyCode::RightBracket,
		"Quote", EKeyCode::Quote);
}

void BindUIAPI(sol::state& LuaState)
{
	LuaState.new_usertype<FVector2>(
		"FVector2",
		sol::constructors<FVector2(), FVector2(float, float)>(),
		"X", &FVector2::X,
		"Y", &FVector2::Y);

	// AddUIText·AddUIImage·AddUIShader는 스크립트 World의 HUD에 그리므로 스크립트 환경마다 등록한다.
}

UCameraComponent* FindCameraComponent(USceneComponent* Component)
{
	if (!Component)
	{
		return nullptr;
	}

	if (UCameraComponent * Camera{ Cast<UCameraComponent>(Component) })
	{
		return Camera;
	}

	for (USceneComponent* Child : Component->GetAttachChildren())
	{
		if (UCameraComponent * Camera{ FindCameraComponent(Child) })
		{
			return Camera;
		}
	}

	return nullptr;
}

FVector GetCameraRelativeLocation(const UCameraComponent& Camera)
{
	return Camera.GetRelativeLocation();
}

FRotator GetCameraRelativeRotation(const UCameraComponent& Camera)
{
	return Camera.GetRelativeRotation();
}

FVector GetSceneRelativeLocation(const USceneComponent& Component)
{
	return Component.GetRelativeLocation();
}

FRotator GetSceneRelativeRotation(const USceneComponent& Component)
{
	return Component.GetRelativeRotation();
}

USceneComponent* FindChildSceneComponent(const USceneComponent& Component, const FString& Name)
{
	for (USceneComponent* Child : Component.GetAttachChildren())
	{
		if (Child->GetName() == Name)
		{
			return Child;
		}

		if (USceneComponent * Found{ FindChildSceneComponent(*Child, Name) })
		{
			return Found;
		}
	}
	return nullptr;
}

// 스크립트가 저장해 둔 참조는 지워졌을 수 있다. 나중에 쓰기 전에 스크립트가 IsValid로 확인한다.
template<typename TComponent>
bool IsValidComponent(const TComponent& Component)
{
	const TComponent* Live{ Cast<TComponent>(FindLiveObjectByAddress(&Component)) };
	return Live && !Live->IsBeingDestroyed();
}

bool IsValidActor(const AActor& Actor)
{
	const AActor* Live{ Cast<AActor>(FindLiveObjectByAddress(&Actor)) };
	return Live && !Live->IsActorBeingDestroyed();
}

void BindCameraAPI(sol::state& LuaState)
{
	LuaState.new_usertype<USceneComponent>("USceneComponent", sol::no_constructor, "IsValid", &IsValidComponent<USceneComponent>, "GetRelativeLocation", &GetSceneRelativeLocation, "SetRelativeLocation", &USceneComponent::SetRelativeLocation, "GetRelativeRotation", &GetSceneRelativeRotation, "SetRelativeRotation", &USceneComponent::SetRelativeRotation, "GetWorldRotation", &USceneComponent::GetWorldRotation, "FindChildComponent", &FindChildSceneComponent);
	LuaState.new_usertype<UCameraComponent>("UCameraComponent", sol::no_constructor, sol::base_classes, sol::bases<USceneComponent>(), "IsValid", &IsValidComponent<UCameraComponent>, "GetRelativeLocation", &GetCameraRelativeLocation, "SetRelativeLocation", &UCameraComponent::SetRelativeLocation, "GetRelativeRotation", &GetCameraRelativeRotation, "SetRelativeRotation", &UCameraComponent::SetRelativeRotation, "GetFieldOfView", &UCameraComponent::GetFieldOfView, "SetFieldOfView", &UCameraComponent::SetFieldOfView, "CanSeeActor", &UCameraComponent::CanSeeActor);
}

FVector GetActorNavigationExtent(const AActor& Actor)
{
	const USceneComponent* RootComponent{ Actor.GetRootComponent() };
	if (!RootComponent)
	{
		return {};
	}

	const FMatrix WorldToRoot{ RootComponent->GetWorldMatrix().Inverse() };
	const FVector Scale{ RootComponent->GetWorldScale3D() };
	FVector Extent{};
	for (UActorComponent* Component : Actor.GetComponents())
	{
		const UPrimitiveComponent* Primitive{ Cast<UPrimitiveComponent>(Component) };
		const FStaticMeshData* Mesh{ Primitive && Primitive->IsVisible() ? Primitive->GetMeshData() : nullptr };
		if (!Mesh)
		{
			continue;
		}

		const FMatrix ComponentToRoot{ Primitive->GetWorldMatrix() * WorldToRoot };
		for (const FVertex& Vertex : Mesh->Vertices)
		{
			const FVector Position{ ComponentToRoot.TransformPosition(Vertex.Position) };
			Extent.X = std::max(Extent.X, std::abs(Position.X * Scale.X));
			Extent.Y = std::max(Extent.Y, std::abs(Position.Y * Scale.Y));
			Extent.Z = std::max(Extent.Z, std::abs(Position.Z * Scale.Z));
		}
	}
	return Extent;
}

ASequenceActor* ResolveActorSequence(const AActor* Actor)
{
	for (UObject* Object : GUObjectArray)
	{
		if (Object == Actor)
		{
			ASequenceActor* Sequence{ Cast<ASequenceActor>(Object) };
			return Sequence && !Sequence->IsActorBeingDestroyed() ? Sequence : nullptr;
		}
	}
	return nullptr;
}

bool PlayActorSequence(const AActor* Actor)
{
	ASequenceActor* Sequence{ ResolveActorSequence(Actor) };
	if (!Sequence)
	{
		return false;
	}
	Sequence->Play();
	return true;
}

bool StopActorSequence(const AActor* Actor)
{
	ASequenceActor* Sequence{ ResolveActorSequence(Actor) };
	if (!Sequence)
	{
		return false;
	}
	Sequence->Stop();
	return true;
}

bool IsActorSequenceFinished(const AActor* Actor)
{
	const ASequenceActor* Sequence{ ResolveActorSequence(Actor) };
	return Sequence && Sequence->GetSequencePlayer().HasFinished();
}

bool EvaluateCameraSequence(const AActor* Actor, double Time, UCameraComponent* Camera, const FVector& Origin, float Yaw, float Side, bool Relative, bool PositionOnly)
{
	ASequenceActor* Sequence{ ResolveActorSequence(Actor) };
	if (!Sequence || !Camera || !std::isfinite(Yaw) || (Side != -1.0f && Side != 1.0f))
	{
		return false;
	}
	bool Found{};
	for (UObject* Object : GUObjectArray)
	{
		if (Object && Object == Camera)
		{
			Found = true;
			break;
		}
	}
	if (!Found || !Camera->GetOwner() || Camera->GetOwner()->IsActorBeingDestroyed() || Camera->GetWorld() != Sequence->GetWorld() || !Sequence->GetSequencePlayer().EvaluateAtTime(Time))
	{
		return false;
	}
	CameraKey Key{};
	if (!Sequence->GetSequencePlayer().GetCurrentCameraKey(Key))
	{
		return false;
	}
	const float Angle{ FMath::DegreesToRadians(Yaw) };
	const float Cosine{ std::cos(Angle) };
	const float Sine{ std::sin(Angle) };
	const FVector Location{ Origin.X + Key.Location.X * Cosine - Key.Location.Y * Side * Sine, Origin.Y + Key.Location.X * Sine + Key.Location.Y * Side * Cosine, Origin.Z + Key.Location.Z };
	if (Relative)
	{
		Camera->SetRelativeLocation(Location);
	}
	else
	{
		Camera->SetWorldLocation(Location, false);
	}
	if (!PositionOnly)
	{
		const FRotator Rotation{ Key.Rotation.Pitch, Yaw + Key.Rotation.Yaw * Side, Key.Rotation.Roll };
		if (Relative)
		{
			Camera->SetRelativeRotation(Rotation);
		}
		else
		{
			Camera->SetWorldRotation(Rotation, false);
		}
		Camera->SetFieldOfView(Key.FieldOfView);
	}
	return true;
}

void BindSequenceAPI(sol::state& LuaState)
{
	sol::usertype<AActor> ActorType{ LuaState.get<sol::usertype<AActor>>("AActor") };
	ActorType.set_function("PlaySequence", &PlayActorSequence);
	ActorType.set_function("StopSequence", &StopActorSequence);
	ActorType.set_function("IsSequenceFinished", &IsActorSequenceFinished);
	ActorType.set_function("EvaluateCameraSequence", &EvaluateCameraSequence);
}

bool SetActorFogOverride(const AActor& Actor, float Density, float HeightFalloff, float StartDistance, float MaxOpacity, const FVector& Color, float Height)
{
	UWorld* World{ Actor.GetWorld() };
	if (!World)
	{
		return false;
	}
	FFogInfo FogInfo{};
	FogInfo.FogDensity = std::max(0.0f, Density);
	FogInfo.FogHeightFalloff = std::max(0.0f, HeightFalloff);
	FogInfo.StartDistance = std::max(0.0f, StartDistance);
	FogInfo.FogCutoffDistance = 0.0f;
	FogInfo.FogMaxOpacity = std::clamp(MaxOpacity, 0.0f, 1.0f);
	FogInfo.FogColor = FVector4{ Color, 1.0f };
	FogInfo.fogHeight = Height;
	World->GetScene().SetFogOverride(FogInfo);
	return true;
}

bool ResetActorFogOverride(const AActor& Actor)
{
	UWorld* World{ Actor.GetWorld() };
	if (!World)
	{
		return false;
	}
	World->GetScene().ResetFogOverride();
	return true;
}

void BindFogAPI(sol::state& LuaState)
{
	sol::usertype<AActor> ActorType{ LuaState.get<sol::usertype<AActor>>("AActor") };
	ActorType.set_function("SetFogOverride", &SetActorFogOverride);
	ActorType.set_function("ResetFogOverride", &ResetActorFogOverride);
}

UPointLightComponent* GetActorPointLight(const AActor& Actor)
{
	for (UActorComponent* Component : Actor.GetComponents())
	{
		UPointLightComponent* Light{ Cast<UPointLightComponent>(Component) };
		if (Light && !Cast<USpotLightComponent>(Light))
		{
			return Light;
		}
	}
	return nullptr;
}

void BindActorAPI(sol::state& LuaState)
{
	LuaState.new_usertype<AActor>(
		"AActor",
		sol::no_constructor,
		"IsValid", &IsValidActor,
		"GetNavigationExtent", &GetActorNavigationExtent,
		"GetPointLight", &GetActorPointLight,
		"GetName", [](const AActor& Actor)
		{ return Actor.GetName(); },
		"GetActorLocation", [](const AActor& Actor)
		{ return Actor.GetActorLocation(); },
		"SetActorLocation", [](AActor& Actor, const FVector& NewLocation)
		{
				USceneComponent* RootComponent = Actor.GetRootComponent();
				if (!RootComponent)
				{
					return false;
				}

				RootComponent->SetWorldLocation(NewLocation, false);
				return true; },
		"Move", [](AActor& Actor, const FVector& Delta, const FRotator& NewRotation)
		{
				USceneComponent* RootComponent = Actor.GetRootComponent();
				if (!RootComponent)
				{
					return false;
				}

				RootComponent->MoveComponent(Delta, NewRotation, true);
				return true; },
		"GetActorRotation", [](const AActor& Actor)
		{ return Actor.GetActorRotation(); },
		"SetActorRotation", [](AActor& Actor, const FRotator& NewRotation)
		{
				USceneComponent* RootComponent = Actor.GetRootComponent();
				if (!RootComponent)
				{
					return false;
				}

				RootComponent->SetWorldRotation(NewRotation, false);
				return true; },
		"GetActorScale3D", [](const AActor& Actor)
		{ return Actor.GetActorScale3D(); },
		"SetActorScale3D", [](AActor& Actor, const FVector& NewScale)
		{
				USceneComponent* RootComponent = Actor.GetRootComponent();
				if (!RootComponent)
				{
					return false;
				}

				RootComponent->SetWorldScale3D(NewScale);
				return true; },
		"Destroy", [](AActor& Actor)
		{ return Actor.Destroy(); },
		"SetBoneRotation", [](AActor& Actor, const FString& BoneName, const FRotator& Rotation)
		{
				for (UActorComponent* Component : Actor.GetComponents())
				{
					if (USkeletalMeshComponent* SkeletalMesh = Cast<USkeletalMeshComponent>(Component))
					{
						SkeletalMesh->SetBoneRotation(SkeletalMesh->FindBoneIndex(BoneName), Rotation);
						return true;
					}
				}
				return false; },
		"GetCamera", [](const AActor& Actor) -> UCameraComponent*
		{ return FindCameraComponent(Actor.GetRootComponent()); },
		"GetSpotLight", [](const AActor& Actor) -> USpotLightComponent*
		{
				for (UActorComponent* Component : Actor.GetComponents())
				{
					if (USpotLightComponent* SpotLight = Cast<USpotLightComponent>(Component))
					{
						return SpotLight;
					}
				}
				return nullptr; });
}

void BindLightAPI(sol::state& LuaState)
{
	sol::usertype<UPointLightComponent> PointLightType{ LuaState.new_usertype<UPointLightComponent>("UPointLightComponent", sol::no_constructor) };
	PointLightType.set_function("IsValid", &IsValidComponent<UPointLightComponent>);
	PointLightType.set_function("SetIntensity", [](UPointLightComponent& Light, float Intensity)
		{ Light.SetIntensity(Intensity); });
	PointLightType.set_function("GetIntensity", [](const UPointLightComponent& Light)
		{ return Light.GetIntensity(); });
	PointLightType.set_function("IsVisible", [](const UPointLightComponent& Light)
		{ return Light.IsVisible(); });
	PointLightType.set_function("GetWorldLocation", [](const UPointLightComponent& Light)
		{ return Light.GetWorldLocation(); });

	LuaState.new_usertype<USpotLightComponent>(
		"USpotLightComponent",
		sol::no_constructor,
		"IsValid", &IsValidComponent<USpotLightComponent>,
		"SetVisibility", [](USpotLightComponent& Light, bool bVisible)
		{ Light.SetVisibility(bVisible); },
		"IsVisible", [](const USpotLightComponent& Light)
		{ return Light.IsVisible(); },
		"SetIntensity", [](USpotLightComponent& Light, float Intensity)
		{ Light.SetIntensity(Intensity); },
		"GetIntensity", [](const USpotLightComponent& Light)
		{ return Light.GetIntensity(); },
		"SetInnerConeAngle", [](USpotLightComponent& Light, float Angle)
		{ Light.SetInnerConeAngle(Angle); },
		"GetInnerConeAngle", [](const USpotLightComponent& Light)
		{ return Light.GetInnerConeAngle(); },
		"SetOuterConeAngle", [](USpotLightComponent& Light, float Angle)
		{ Light.SetOuterConeAngle(Angle); },
		"GetOuterConeAngle", [](const USpotLightComponent& Light)
		{ return Light.GetOuterConeAngle(); },
		"SetAttenuationRadius", [](USpotLightComponent& Light, float Radius)
		{ Light.SetAttenuationRadius(Radius); },
		"GetAttenuationRadius", [](const USpotLightComponent& Light)
		{ return Light.GetAttenuationRadius(); });
}
}

FLuaVM::~FLuaVM()
{
	Shutdown();
}

bool FLuaVM::Initialize()
{
	if (State)
	{
		return true;
	}

	try
	{
		State = MakeUnique<sol::state>();

		// 게임 스크립트에 필요한 표준 라이브러리만 엽니다.
		// os, io, debug는 파일 및 시스템 접근이 가능하므로 우선 제외합니다.
		State->open_libraries(
			sol::lib::base,
			sol::lib::package,
			sol::lib::coroutine,
			sol::lib::string,
			sol::lib::table,
			sol::lib::math);

		ConfigurePackagePath(*State);
		BindEngineAPI();

		HTR_LOG(
			Info,
			"[Lua] VM initialized with {}",
			LUAJIT_VERSION);

		return true;
	}
	catch (const std::exception& Exception)
	{
		HTR_LOG(
			Error,
			"[Lua] VM initialization failed: {}",
			Exception.what());

		State.reset();
		return false;
	}
}

void FLuaVM::Shutdown()
{
	// Lua 참조는 lua_State를 닫기 전에 해제한다.
	ResetCoroutines();
	if (!State)
	{
		return;
	}

	// sol::state의 소멸자가 내부 lua_State에 lua_close를 호출합니다.
	State.reset();
}

void FLuaVM::UpdateCoroutine(float DeltaTime)
{
	if (State)
	{
		FCoroutineScheduler.Update(DeltaTime);
	}
}

void FLuaVM::ResetCoroutines()
{
	FCoroutineScheduler.Reset();
}

void FLuaVM::BindEngineAPI()
{
	if (!State)
	{
		return;
	}

	BindLoggingAPI(*State);
	BindVectorAPI(*State);
	BindRotatorAPI(*State);
	BindCoroutineAPI(*State, FCoroutineScheduler);
	BindInputAPI(*State);
	BindUIAPI(*State);
	State->new_usertype<FHitResult>(
		"FHitResult",
		sol::no_constructor,
		"bBlockingHit", &FHitResult::bBlockingHit,
		"bStartPenetrating", &FHitResult::bStartPenetrating,
		"Time", &FHitResult::Time,
		"Distance", &FHitResult::Distance,
		"Location", &FHitResult::Location,
		"ImpactPoint", &FHitResult::ImpactPoint,
		"Normal", &FHitResult::Normal,
		"ImpactNormal", &FHitResult::ImpactNormal,
		"PenetrationDepth", &FHitResult::PenetrationDepth);
	BindLightAPI(*State);
	BindCameraAPI(*State);
	BindActorAPI(*State);
	BindSequenceAPI(*State);
	BindFogAPI(*State);
	BindPlayerCameraManagerAPI(*State);
	BindAudioAPI(*State);
}

bool FLuaVM::ExecuteString(
	const FString& Source,
	const FString& ChunkName)
{
	if (!State)
	{
		HTR_LOG(
			Error,
			"[Lua] Cannot execute '{}': VM is not initialized",
			ChunkName);
		return false;
	}

	sol::protected_function_result Result =
		State->safe_script(
			Source,
			sol::script_pass_on_error,
			ChunkName);

	return CheckLuaResult(Result, ChunkName);
}

bool FLuaVM::ExecuteFile(const FString& ScriptPath)
{
	if (!State)
	{
		HTR_LOG(
			Error,
			"[Lua] Cannot execute '{}': VM is not initialized",
			ScriptPath);
		return false;
	}

	if (!std::filesystem::exists(ScriptPath))
	{
		HTR_LOG(
			Error,
			"[Lua] Script file not found: {}",
			ScriptPath);
		return false;
	}

	sol::protected_function_result Result =
		State->safe_script_file(
			ScriptPath,
			sol::script_pass_on_error);

	return CheckLuaResult(Result, ScriptPath);
}
