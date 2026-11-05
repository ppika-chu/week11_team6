#pragma once

#include "Shader.h"
#include "ViewMode.h"

class FRenderResourceManager
{
public:
	static FRenderResourceManager& Get()
	{
		static FRenderResourceManager* Instance = new FRenderResourceManager();
		return *Instance;
	}

	static void Init()
	{
		Get().ScanShaders("Resources/Shader");
	}

	void ScanShaders(const fs::path& ShaderRoot);
	void LoadUberLit(const fs::path& ShaderRoot);

	static void Shutdown();

	static FComputeShader* GetComputeShader(const FString& InPath);
	static FShaderProgram* GetShaderProgram(const FString& InPath, const D3D_SHADER_MACRO* Macro = nullptr);
	// 시작 시 스캔하지 않는 셰이더(게임 에셋 등)나 매크로 변형을 처음 요청할 때 컴파일해 캐시한다. 실패하면 nullptr.
	static FShaderProgram* FindOrLoadShaderProgram(const FString& InPath, const D3D_SHADER_MACRO* Macro = nullptr);

	// 뷰모드에 맞는 UberLit 변형. 컴파일에 실패했으면 nullptr.
	static FShaderProgram* GetUberLitProgram(EViewMode Mode);
	// 머티리얼 셰이더가 UberLit(기본 키 또는 변형)인지. 아니면 뷰모드로 바꾸지 않는다.
	static bool IsUberLitProgram(const FShaderProgram* Program);

private:
	void LoadOrCompileShader(const FString& Path, const D3D_SHADER_MACRO* Macro = nullptr);

	TMap<FString, TUniquePtr<FVertexShader>> VertexShaderMap;
	TMap<FString, TUniquePtr<FPixelShader>> PixelShaderMap;
	TMap<FString, TUniquePtr<FComputeShader>> ComputeShaderMap;
	TMap<FString, TUniquePtr<FShaderProgram>> ShaderProgramMap;
	TSet<FString> FailedShaderPaths;

	// LoadUberLit이 채운다. 맵 안의 프로그램을 가리키기만 하고 소유하지 않는다.
	FShaderProgram* UberLitDefault = nullptr;
	FShaderProgram* UberLitPrograms[static_cast<int32>(EViewMode::Count)]{};
};
