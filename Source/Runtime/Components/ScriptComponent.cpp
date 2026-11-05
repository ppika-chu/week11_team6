#include "EnginePCH.h"

#include "ScriptComponent.h"

#include "Engine/Engine.h"
#include "Scripting/LuaVM.h"
#include "Scripting/CameraManagerBindings.h"
#include "Scripting/ScriptEnvironmentBindings.h"
#include "Components/ShapeComponent.h"
#include "GameFramework/Actor.h"

#include <algorithm>
#include <cctype>

namespace
{
constexpr const char* ScriptRoot = "Assets/Scripts";

FString ToLower(FString Text)
{
	std::transform(Text.begin(), Text.end(), Text.begin(),
		[](unsigned char Character)
		{ return static_cast<char>(std::tolower(Character)); });
	return Text;
}

TArray<std::filesystem::path> FindScriptsByFileName(const std::filesystem::path& FileName)
{
	TArray<std::filesystem::path> Matches;
	const FString Target = ToLower(FileName.string());

	std::error_code ErrorCode;
	for (std::filesystem::recursive_directory_iterator It(ScriptRoot, ErrorCode), End; !ErrorCode && It != End; It.increment(ErrorCode))
	{
		if (It->is_regular_file() && ToLower(It->path().filename().string()) == Target)
		{
			Matches.Add(It->path().lexically_normal());
		}
	}

	return Matches;
}
}

UScriptComponent::~UScriptComponent()
{
	UnbindCollisionEvents();
}

void UScriptComponent::Serialize(FStructuredArchive::FRecord Record)
{
	ScriptPath = std::filesystem::path(ScriptPath).generic_string();
	Super::Serialize(Record);
	ScriptPath = std::filesystem::path(ScriptPath).generic_string();
}

void UScriptComponent::InitializeComponent()
{
	Super::InitializeComponent();

	LuaVM = GEngine->GetLuaVM();
	State = LuaVM->GetState();

	PrimaryComponentTick.bCanEverTick = true;
}

void UScriptComponent::BeginPlay()
{
	Super::BeginPlay();

	if (ScriptPath.empty())
	{
		AActor* Owner = GetOwner();
		HTR_LOG(Warning, "[Lua] No script assigned to {}", Owner ? Owner->GetName() : FString("<no owner>"));
		return;
	}

	LoadScript();
	// 함수 객체 대신 this를 통해 현재 Lua 함수를 조회하므로 Hot Reload에도 유지된다.
	BindCollisionEvents();

	std::error_code ErrorCode;

	LastWriteTime = std::filesystem::last_write_time(ScriptPath, ErrorCode);

	if (ErrorCode)
	{
		HTR_LOG(Warning, "[Lua] Cannot read script timestamp: {}", ScriptPath);
	}

	if (!BeginPlayFunction.valid())
	{
		return;
	}

	sol::protected_function_result Result = BeginPlayFunction();

	if (!Result.valid())
	{
		sol::error Error = Result;
		HTR_LOG(Error, "[Lua] {}", Error.what());
	}
}

void UScriptComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	// 파일 변경 감지 체크
	CheckHotReload(DeltaTime);

	if (!TickFunction.valid())
	{
		return;
	}

	sol::protected_function_result Result = TickFunction(DeltaTime);

	if (!Result.valid())
	{
		sol::error Error = Result;
		HTR_LOG(Error, "[Lua] {}", Error.what());
	}
}

void UScriptComponent::EndPlay()
{
	UnbindCollisionEvents();
	Super::EndPlay();

	if (!EndPlayFunction.valid())
	{
		return;
	}

	sol::protected_function Function = EndPlayFunction;
	sol::protected_function_result Result = Function();

	if (!Result.valid())
	{
		sol::error Error = Result;
		HTR_LOG(Error, "[Lua] {}", Error.what());
	}
}

void UScriptComponent::OnOverlap(AActor* OtherActor)
{
	if (!OverlapFunction.valid())
	{
		return;
	}

	// Lua에서 DestroySelf()를 호출해도 실행 중인 함수 참조는 살아 있어야 한다.
	sol::protected_function Function = OverlapFunction;
	sol::protected_function_result Result = Function(OtherActor);

	if (!Result.valid())
	{
		sol::error Error = Result;
		HTR_LOG(Error, "[Lua] {}", Error.what());
	}
}

void UScriptComponent::OnEndOverlap(AActor* OtherActor)
{
	if (!EndOverlapFunction.valid())
	{
		return;
	}

	sol::protected_function Function = EndOverlapFunction;
	sol::protected_function_result Result = Function(OtherActor);
	if (!Result.valid())
	{
		sol::error Error = Result;
		HTR_LOG(Error, "[Lua] OnEndOverlap: {}", Error.what());
	}
}

void UScriptComponent::OnHit(AActor* OtherActor, const FHitResult& Hit)
{
	if (!HitFunction.valid())
	{
		return;
	}

	sol::protected_function Function = HitFunction;
	// Sweep의 임시 HitResult를 Lua에 참조로 남기지 않는다.
	sol::protected_function_result Result = Function(OtherActor, FHitResult(Hit));
	if (!Result.valid())
	{
		sol::error Error = Result;
		HTR_LOG(Error, "[Lua] OnHit: {}", Error.what());
	}
}

void UScriptComponent::BindCollisionEvents()
{
	UnbindCollisionEvents();
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	CollisionCallbackLifetime = MakeShared<bool>(true);
	TWeakPtr<bool> Lifetime = CollisionCallbackLifetime;
	for (UActorComponent* Component : Owner->GetComponents())
	{
		UShapeComponent* Shape = Cast<UShapeComponent>(Component);
		if (!Shape)
		{
			continue;
		}

		FCollisionBinding Binding{};
		Binding.ShapeUUID = Shape->GetUUID();
		Binding.BeginOverlapHandle = Shape->OnComponentBeginOverlap.Add(
			[this, Lifetime](UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent*)
			{
				if (!Lifetime.expired())
				{
					OnOverlap(OtherActor);
				}
			});
		Binding.EndOverlapHandle = Shape->OnComponentEndOverlap.Add(
			[this, Lifetime](UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent*)
			{
				if (!Lifetime.expired())
				{
					OnEndOverlap(OtherActor);
				}
			});
		Binding.HitHandle = Shape->OnComponentHit.Add(
			[this, Lifetime](UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent*, const FHitResult& Hit)
			{
				if (!Lifetime.expired())
				{
					OnHit(OtherActor, Hit);
				}
			});
		CollisionBindings.Add(Binding);
	}
}

void UScriptComponent::UnbindCollisionEvents()
{
	CollisionCallbackLifetime.reset();
	for (const FCollisionBinding& Binding : CollisionBindings)
	{
		// Shape가 먼저 삭제되었거나 Actor의 소멸자가 Components를 비운 경우도 처리한다.
		for (UObject* Object : GUObjectArray)
		{
			if (Object->GetUUID() != Binding.ShapeUUID)
			{
				continue;
			}

			if (UShapeComponent* Shape = Cast<UShapeComponent>(Object))
			{
				Shape->OnComponentBeginOverlap.RemoveHandler(Binding.BeginOverlapHandle);
				Shape->OnComponentEndOverlap.RemoveHandler(Binding.EndOverlapHandle);
				Shape->OnComponentHit.RemoveHandler(Binding.HitHandle);
			}
			break;
		}
	}
	CollisionBindings.Reset();
}

bool UScriptComponent::CreateScriptFromTemplate(const std::filesystem::path& NewScriptPath)
{
	const std::filesystem::path TemplatePath = std::filesystem::path(ScriptRoot) / "Template.lua";

	if (!std::filesystem::exists(TemplatePath))
	{
		HTR_LOG(Error, "[Lua] Template not found: {}", TemplatePath.string());
		return false;
	}

	// 기존 스크립트는 절대 덮어쓰지 않습니다.
	if (!std::filesystem::exists(NewScriptPath))
	{
		std::error_code ErrorCode;

		std::filesystem::copy_file(
			TemplatePath,
			NewScriptPath,
			std::filesystem::copy_options::none,
			ErrorCode);

		if (ErrorCode)
		{
			HTR_LOG(
				Error,
				"[Lua] Failed to create script '{}': {}",
				NewScriptPath.string(),
				ErrorCode.message());

			return false;
		}

		HTR_LOG(Info, "[Lua] Created script from template: {}", NewScriptPath.generic_string());
	}

	ScriptPath = NewScriptPath.generic_string();
	return true;
}

bool UScriptComponent::LoadScript()
{
	if (!LuaVM)
	{
		return false;
	}

	if (!State)
	{
		return false;
	}

	Environment = sol::environment(*State, sol::create, State->globals());

	if (!Environment)
	{
		return false;
	}

	BindScriptContext(Environment);

	sol::protected_function_result Result = State->safe_script_file(
		ScriptPath,
		Environment,
		sol::script_pass_on_error);

	if (!Result.valid())
	{
		sol::error Error = Result;
		HTR_LOG(Error, "[Lua] Failed to load '{}': {}", ScriptPath, Error.what());
		return false;
	}

	BeginPlayFunction = Environment["BeginPlay"];
	TickFunction = Environment["Tick"];
	EndPlayFunction = Environment["EndPlay"];
	OverlapFunction = Environment["OnOverlap"];
	EndOverlapFunction = Environment["OnEndOverlap"];
	HitFunction = Environment["OnHit"];

	return true;
}

void UScriptComponent::BindScriptContext(sol::environment& TargetEnvironment)
{
	AActor* Owner = GetOwner();
	BindPlayerCameraManagerEnvironment(TargetEnvironment, Owner);

	TargetEnvironment["self"] = Owner;
	TargetEnvironment["scriptPath"] = ScriptPath;

	if (Owner)
	{
		if (UWorld* World = Owner->GetWorld())
		{
			TargetEnvironment["sceneName"] =
				World->GetSceneName();
		}
	}

	BindScriptWorldEnvironment(TargetEnvironment, this);
}

void UScriptComponent::CheckHotReload(float DeltaTime)
{
	HotReloadCheckTimer += DeltaTime;

	if (HotReloadCheckTimer < 0.25f)
	{
		return;
	}

	HotReloadCheckTimer = 0.0f;

	std::error_code ErrorCode;

	const auto CurrentWriteTime =
		std::filesystem::last_write_time(
			ScriptPath,
			ErrorCode);

	if (ErrorCode)
	{
		return;
	}

	if (CurrentWriteTime == LastWriteTime)
	{
		return;
	}

	// 같은 변경을 매 프레임 다시 감지하지 않도록 갱신
	LastWriteTime = CurrentWriteTime;

	ReloadScript();
}

bool UScriptComponent::ReloadScript()
{
	FLuaVM* LuaVM = GEngine->GetLuaVM();

	if (!LuaVM || !LuaVM->IsInitialized())
	{
		return false;
	}

	sol::state* State = LuaVM->GetState();

	if (!State)
	{
		return false;
	}

	// 기존 Environment는 아직 유지
	sol::environment NewEnvironment(*State, sol::create, State->globals());

	// 현재 액터 컨텍스트를 새 환경에도 다시 넣어야 함
	BindScriptContext(NewEnvironment);

	sol::protected_function_result Result =
		State->safe_script_file(ScriptPath, NewEnvironment, sol::script_pass_on_error);

	if (!Result.valid())
	{
		sol::error Error = Result;
		HTR_LOG(Error, "[Lua] {}", Error.what());
		return false;
	}

	// 새 스크립트가 정상일 때만 기존 환경 교체
	Environment = std::move(NewEnvironment);

	BeginPlayFunction = Environment["BeginPlay"];
	TickFunction = Environment["Tick"];
	EndPlayFunction = Environment["EndPlay"];
	OverlapFunction = Environment["OnOverlap"];
	EndOverlapFunction = Environment["OnEndOverlap"];
	HitFunction = Environment["OnHit"];

	sol::protected_function ReloadFunction = Environment["OnReload"];

	if (ReloadFunction.valid())
	{
		sol::protected_function_result Result = ReloadFunction();

		if (!Result.valid())
		{
			sol::error Error = Result;
			HTR_LOG(Error, "[Lua] {}", Error.what());
		}
	}

	PrimaryComponentTick.bCanEverTick = true;

	HTR_LOG(Info, "[Lua] Hot reload succeeded: {}", ScriptPath);

	return true;
}

bool UScriptComponent::EnsureScriptFile()
{
	if (!ScriptPath.empty())
	{
		return std::filesystem::exists(ScriptPath) || CreateScriptFromTemplate(ScriptPath);
	}

	AActor* Owner = GetOwner();
	if (!Owner || !Owner->GetWorld())
	{
		return false;
	}

	return CreateScriptFromTemplate(
		std::filesystem::path(ScriptRoot) /
		std::format("{}_{}.lua", Owner->GetWorld()->GetSceneName(), Owner->GetName()));
}

bool UScriptComponent::SetScriptPath(
	const FString& NewFileName)
{
	if (NewFileName.empty())
	{
		HTR_LOG(Error, "[Lua] Script file name cannot be empty");
		return false;
	}

	std::filesystem::path RequestedPath = NewFileName;
	if (RequestedPath.extension() != ".lua")
	{
		RequestedPath.replace_extension(".lua");
	}
	RequestedPath = RequestedPath.lexically_normal();

	std::filesystem::path NewPath;

	if (!RequestedPath.has_parent_path())
	{
		const TArray<std::filesystem::path> Matches = FindScriptsByFileName(RequestedPath.filename());

		if (Matches.Num() > 1)
		{
			FString Candidates;
			for (const std::filesystem::path& Match : Matches)
			{
				Candidates += "\n  " + Match.generic_string();
			}
			HTR_LOG(Error, "[Lua] '{}' matches several scripts, enter the full path:{}", NewFileName, Candidates);
			return false;
		}

		NewPath = Matches.Num() == 1 ? Matches[0] : std::filesystem::path(ScriptRoot) / RequestedPath;
	}
	else if (std::filesystem::exists(RequestedPath))
	{
		NewPath = RequestedPath;
	}
	else if (std::filesystem::exists(std::filesystem::path(ScriptRoot) / RequestedPath))
	{
		NewPath = std::filesystem::path(ScriptRoot) / RequestedPath;
	}
	else
	{
		NewPath = *RequestedPath.begin() == "Assets" ? RequestedPath : std::filesystem::path(ScriptRoot) / RequestedPath;
	}

	NewPath = NewPath.lexically_normal();

	if (!std::filesystem::exists(NewPath))
	{
		if (!std::filesystem::is_directory(NewPath.parent_path()))
		{
			HTR_LOG(Error, "[Lua] Folder does not exist: {}", NewPath.parent_path().generic_string());
			return false;
		}

		if (!CreateScriptFromTemplate(NewPath))
		{
			return false;
		}
	}

	ScriptPath = NewPath.generic_string();

	std::error_code ErrorCode;
	LastWriteTime = std::filesystem::last_write_time(ScriptPath, ErrorCode);
	if (ErrorCode)
	{
		HTR_LOG(Warning, "[Lua] Cannot read script timestamp: {}", ScriptPath);
	}

	return true;
}
