#include "EnginePCH.h"
#include "Scripting/ScriptEnvironmentBindings.h"

#include "Components/ActorComponent.h"
#include "Components/DecalComponent.h"
#include "Engine/Engine.h"
#include "Engine/SceneTransitionManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Actor/DecalActor.h"
#include "GameFramework/PlayerController.h"
#include "Asset/AssetManager.h"
#include "Core/ResourcePaths.h"
#include "Render/RenderResourceManager.h"
#include "Render/Texture2D.h"
#include "Text/Font.h"

#include <algorithm>

#include <sol/sol.hpp>

void BindScriptWorldEnvironment(sol::environment& TargetEnvironment, UActorComponent* Component)
{
	const auto GetViewportInfo = [Component]()
	{
		AActor* Actor = Component->GetOwner();
		return GEngine && Actor ? GEngine->GetGameViewportInfo(Actor->GetWorld()) : FGameViewportInfo{};
	};
	const auto GetOwnerWorld = [Component]() -> UWorld*
	{
		AActor* Actor = Component->GetOwner();
		return Actor ? Actor->GetWorld() : nullptr;
	};

	TargetEnvironment.set_function("GetViewportSize", [GetViewportInfo]()
		{ return GetViewportInfo().Size; });
	TargetEnvironment.set_function("GetMousePosition", [GetViewportInfo]()
		{ return GetViewportInfo().MousePosition; });
	TargetEnvironment.set_function("IsMouseOverGameViewport", [GetViewportInfo]()
		{ return GetViewportInfo().bAcceptsMouseInput; });
	TargetEnvironment.set_function("SetGlobalTimeDilation", [GetOwnerWorld](float TimeDilation)
		{
		if (UWorld* World = GetOwnerWorld()){
			World->SetTimeScale(TimeDilation);
} });
	TargetEnvironment.set_function("GetGlobalTimeDilation", [GetOwnerWorld]()
		{
		if (UWorld* World = GetOwnerWorld()){
			return World->GetTimeScale();
}
		return 1.0f; });
	TargetEnvironment.set_function("GetEffectiveTimeDilation", [GetOwnerWorld]()
		{
		if (UWorld* World = GetOwnerWorld()){
			return World->GetGameTime().GetTimeDilation();
}
		return 1.0f; });
	TargetEnvironment.set_function("GetUnscaledDeltaTime", [GetOwnerWorld]()
		{
		if (UWorld* World = GetOwnerWorld()){
			return World->GetGameTime().GetDeltaRealTimeSeconds();
}
		return 0.0f; });
	TargetEnvironment.set_function("GetWorldDeltaTime", [GetOwnerWorld]()
		{
		if (UWorld* World = GetOwnerWorld()){
			return World->GetGameTime().GetDeltaWorldTimeSeconds();
}
		return 0.0f; });
	TargetEnvironment.set_function("GetRealTimeSeconds", [GetOwnerWorld]()
		{
		if (UWorld* World = GetOwnerWorld()){
			return World->GetGameTime().GetRealTimeSeconds();
}
		return 0.0; });
	TargetEnvironment.set_function("GetWorldTimeSeconds", [GetOwnerWorld]()
		{
		if (UWorld* World = GetOwnerWorld()){
			return World->GetGameTime().GetWorldTimeSeconds();
}
		return 0.0; });
	TargetEnvironment.set_function("ResetTimeEffects", [GetOwnerWorld]()
		{
		if (UWorld* World = GetOwnerWorld()){
			World->ResetTimeEffects();
} });
	TargetEnvironment.set_function("SetVignette",
		[GetOwnerWorld](float Intensity, float Radius, float Softness, const FVector& Color)
		{
			if (UWorld* World = GetOwnerWorld())
			{
				World->SetVignette(Intensity, Radius, Softness, Color);
			}
		});
	TargetEnvironment.set_function("SetVignetteEnabled", [GetOwnerWorld](bool bEnabled)
		{
		if (UWorld* World = GetOwnerWorld()){
			World->SetVignetteEnabled(bEnabled);
} });
	TargetEnvironment.set_function("ResetVignette", [GetOwnerWorld]()
		{
		if (UWorld* World = GetOwnerWorld()){
			World->ResetVignette();
} });
	// 패키징된 게임으로 실행 중인지. PIE에서는 false다.
	TargetEnvironment.set_function("IsExecutableGame", [GetOwnerWorld]()
		{
		UWorld* World = GetOwnerWorld();
		return World && World->GetWorldType() == EWorldType::Game; });
	TargetEnvironment.set_function("SetGameMouseCapture", [GetOwnerWorld](bool Enabled)
		{
		UWorld* World = GetOwnerWorld();
		return GEngine && World && GEngine->SetGameMouseCapture(World, Enabled); });

	// 런타임에 데칼을 찍는다. 페이드가 끝나면 액터까지 스스로 지워지고, 씬에는 저장되지 않는다.
	// Desc 필드: Location, Rotation, Size(반 길이), Texture, NormalTexture, SpecularFactor, FadeInDuration, FadeStartDelay, FadeDuration
	TargetEnvironment.set_function("SpawnDecal", [GetOwnerWorld](const sol::table& Desc)
		{
		UWorld* World = GetOwnerWorld();
		if (!World)
		{
			return false;
		}

		const FTransform Transform{ Desc.get<sol::optional<FRotator>>("Rotation").value_or(FRotator()), Desc.get<sol::optional<FVector>>("Location").value_or(FVector()) };
		ADecalActor* Actor = Cast<ADecalActor>(World->SpawnActor(ADecalActor::StaticClass(), NAME_None, &Transform));
		UDecalComponent* Decal = Actor ? Actor->GetDecal() : nullptr;
		if (!Decal)
		{
			return false;
		}
		Actor->SetFlags(EObjectFlags::RF_Transient);

		Decal->SetDecalSize(Desc.get<sol::optional<FVector>>("Size").value_or(FVector(1.0f, 1.0f, 1.0f)));
		Decal->SetFadeInDuration(Desc.get_or("FadeInDuration", 0.0f));
		Decal->SetFadeStartDelay(Desc.get_or("FadeStartDelay", 0.0f));
		Decal->SetFadeDuration(Desc.get_or("FadeDuration", 0.0f));

		// UDecalComponent 생성자가 데칼마다 만든 머티리얼 인스턴스라서 바로 고쳐 써도 다른 데칼에 번지지 않는다.
		if (UMaterial* Material = Decal->GetDecalMaterial())
		{
			Material->SpecularFactor = Desc.get_or("SpecularFactor", Material->SpecularFactor);
			const FString TexturePath = Desc.get_or("Texture", FString());
			const FString NormalPath = Desc.get_or("NormalTexture", FString());
			if (UTexture2D* Texture = TexturePath.empty() ? nullptr : UAssetManager::Get().LoadTexture(TexturePath))
			{
				Material->Textures[0] = Texture;
			}
			if (UTexture2D* Normal = NormalPath.empty() ? nullptr : UAssetManager::Get().LoadTexture(NormalPath))
			{
				Material->Textures[1] = Normal;
			}
		}
		return true; });

	// 커서 모드와 화면 UI는 스크립트 World의 PlayerController가 가진다. 에디터 World에는 없으므로 아무 일도 하지 않는다.
	const auto GetPlayerController = [GetOwnerWorld]() -> APlayerController*
	{
		UWorld* World = GetOwnerWorld();
		return World ? World->GetPlayerController() : nullptr;
	};
	TargetEnvironment.set_function("SetCursorMode", [GetPlayerController](ECursorMode Mode)
		{
		if (APlayerController* PlayerController = GetPlayerController()){
			PlayerController->SetCursorMode(Mode);
} });
	TargetEnvironment.set_function("GetCursorMode", [GetPlayerController]()
		{
		APlayerController* PlayerController = GetPlayerController();
		return PlayerController ? PlayerController->GetCursorMode() : ECursorMode::Free; });

	TargetEnvironment.set_function("AddUIText", [GetPlayerController](const FString& Text, float TextSize, const FVector2& Anchor, const FVector2& Alignment, sol::optional<FVector2> Offset) -> bool
		{
		APlayerController* PlayerController = GetPlayerController();
		UFont* Font = UAssetManager::GetAssetByPath<UFont>(ResourcePaths::DefaultFont);
		if (!PlayerController || !Font){
			return false;
}

		UITextInfo Info{};
		Info.mTextSize = TextSize;
		Info.mText = Text;
		Info.mAtlas = Font;
		Info.mAnchor = Anchor;
		Info.mAlignment = Alignment;
		Info.mOffset = Offset.value_or(FVector2());
		PlayerController->GetHUD().Texts.Add(Info);
		return true; });

	TargetEnvironment.set_function("AddUIImage", [GetPlayerController](const FString& TexturePath, const FVector2& Size, const FVector2& Anchor, const FVector2& Alignment, sol::optional<FVector2> Offset, sol::optional<float> Ratio) -> bool
		{
		APlayerController* PlayerController = GetPlayerController();
		if (!PlayerController || TexturePath.empty() || Size.X <= 0.0f || Size.Y <= 0.0f){
			return false;
}

		UTexture2D* Texture = UAssetManager::Get().LoadTexture(TexturePath);
		if (!Texture){
			return false;
}

		UIImageInfo Info{};
		Info.mSize = Size;
		Info.mTexture = Texture;
		Info.mRatio = std::clamp(Ratio.value_or(1.0f), 0.0f, 1.0f);
		Info.mAnchor = Anchor;
		Info.mAlignment = Alignment;
		Info.mOffset = Offset.value_or(FVector2());
		PlayerController->GetHUD().Images.Add(Info);
		return true; });

	// Params는 숫자 배열이며 4개씩 묶여 셰이더 b1의 float4 Params[]로 들어간다.
	TargetEnvironment.set_function("AddUIShader", [GetPlayerController](const FString& ShaderPath, const FVector2& Size, const FVector2& Anchor, const FVector2& Alignment, sol::optional<FVector2> Offset, sol::optional<sol::table> Params) -> bool
		{
		APlayerController* PlayerController = GetPlayerController();
		if (!PlayerController || ShaderPath.empty() || Size.X <= 0.0f || Size.Y <= 0.0f){
			return false;
}

		FShaderProgram* Shader = FRenderResourceManager::FindOrLoadShaderProgram(ShaderPath);
		if (!Shader){
			return false;
}

		UIShaderInfo Info{};
		Info.mShader = Shader;
		Info.mSize = Size;
		Info.mAnchor = Anchor;
		Info.mAlignment = Alignment;
		Info.mOffset = Offset.value_or(FVector2());
		float* Values = &Info.mParams[0].X;
		std::fill(Values, Values + UIShaderInfo::MaxParams * 4, 0.0f);
		if (Params)
		{
			const size_t Count = Params->size();
			if (Count > static_cast<size_t>(UIShaderInfo::MaxParams * 4)){
				return false;
}
			for (size_t Index = 0; Index < Count; ++Index){
				Values[Index] = Params->get_or(Index + 1, 0.0f);
}
		}
		PlayerController->GetHUD().Shaders.Add(Info);
		return true; });
	TargetEnvironment.set_function("QuitGame", [Component]()
		{
        if (GEngine && Component->GetOwner()){
            GEngine->RequestQuitGame(Component->GetOwner()->GetWorld());
} });
	TargetEnvironment.set_function(
		"DestroySelf",
		[Component]()
		{
			if (AActor* Owner = Component->GetOwner())
			{
				Owner->Destroy();
			}
		});

	// 실제 로드는 현재 Lua 호출이 끝난 뒤 엔진의 프레임 경계에서 수행한다.
	TargetEnvironment.set_function(
		"LoadScene",
		[Component](const FString& Path)
		{
			AActor* Owner = Component->GetOwner();
			FSceneTransitionManager* Manager = GEngine ? GEngine->GetSceneTransitionManager() : nullptr;
			if (Owner && Manager)
			{
				Manager->RequestLoadScene(Owner->GetWorld(), Path);
			}
		});

	TargetEnvironment.set_function(
		"RestartScene",
		[Component]()
		{
			AActor* Owner = Component->GetOwner();
			FSceneTransitionManager* Manager = GEngine ? GEngine->GetSceneTransitionManager() : nullptr;
			if (Owner && Manager)
			{
				Manager->RequestRestartScene(Owner->GetWorld());
			}
		});
}
