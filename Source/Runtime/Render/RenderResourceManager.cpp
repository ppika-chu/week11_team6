#include "EnginePCH.h"
#include "RenderResourceManager.h"

#include "RenderCommand.h"

#include "RenderUtil.h"

namespace
{
FString MakeShaderKey(const FString& Path, const D3D_SHADER_MACRO* Macros)
{
	FString Key = Path;
	while (Macros && Macros->Name)
	{
		Key += "_" + FString(Macros->Name);
		++Macros;
	}
	return Key;
}
}


void FRenderResourceManager::ScanShaders(const fs::path& ShaderRoot)
{
	std::error_code ErrorCode;

	if (!fs::exists(ShaderRoot, ErrorCode))
	{
		HTR_LOG(Error, "[Shader] Scan Root Not Found : {}", ShaderRoot.generic_string());
		return;
	}

	for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(ShaderRoot))
	{
		if (!Entry.is_regular_file())
		{
			continue;
		}
		if (Entry.path().extension() != ".hlsl")
		{
			continue;
		}
		if (Entry.path().stem() == "UberLit")
		{
			continue;
		}

		FString Path = Entry.path().generic_string();
		LoadOrCompileShader(Path);
	}
	LoadUberLit(ShaderRoot);
}

void FRenderResourceManager::LoadUberLit(const fs::path& ShaderRoot)
{
	const FString Path = (ShaderRoot / "UberLit.hlsl").generic_string();

	// 같은 UberLit.hlsl을 매크로 조합별로 따로 컴파일한다.
	// 캐시 키 / CSO 파일명은 "경로_매크로이름" 형태로 구분된다.
	// 매크로 없는 기본 키("Resources/Shader/UberLit.hlsl")는 기존 머티리얼이 쓰므로 Lambert로 남겨 둔다. (셰이더 쪽 기본값)

	static const D3D_SHADER_MACRO Unlit[] = { { "LIGHTING_MODEL_UNLIT", "1" }, { nullptr, nullptr } };
	static const D3D_SHADER_MACRO Gouraud[] = { { "LIGHTING_MODEL_GOURAUD", "1" }, { nullptr, nullptr } };
	static const D3D_SHADER_MACRO Lambert[] = { { "LIGHTING_MODEL_LAMBERT", "1" }, { nullptr, nullptr } };
	static const D3D_SHADER_MACRO BlinnPhong[] = { { "LIGHTING_MODEL_BLINN_PHONG", "1" }, { nullptr, nullptr } };
	static const D3D_SHADER_MACRO WorldNormal[] = { { "WORLD_NORMAL", "1" }, { nullptr, nullptr } };
	static const D3D_SHADER_MACRO UnlitSkin[] = { { "LIGHTING_MODEL_UNLIT", "1" }, { "GPU_SKINNING", "1" }, { nullptr, nullptr } };
	static const D3D_SHADER_MACRO GouraudSkin[] = { { "LIGHTING_MODEL_GOURAUD", "1" }, { "GPU_SKINNING", "1" }, { nullptr, nullptr } };
	static const D3D_SHADER_MACRO LambertSkin[] = { { "LIGHTING_MODEL_LAMBERT", "1" }, { "GPU_SKINNING", "1" }, { nullptr, nullptr } };
	static const D3D_SHADER_MACRO BlinnPhongSkin[] = { { "LIGHTING_MODEL_BLINN_PHONG", "1" }, { "GPU_SKINNING", "1" }, { nullptr, nullptr } };
	static const D3D_SHADER_MACRO WorldNormalSkin[] = { { "WORLD_NORMAL", "1" }, { "GPU_SKINNING", "1" }, { nullptr, nullptr } };

	LoadOrCompileShader(Path); // 기본 (Lambert)
	LoadOrCompileShader(Path, Unlit);
	LoadOrCompileShader(Path, Gouraud);
	LoadOrCompileShader(Path, Lambert);
	LoadOrCompileShader(Path, BlinnPhong);
	LoadOrCompileShader(Path, WorldNormal);
	LoadOrCompileShader(Path, UnlitSkin);
	LoadOrCompileShader(Path, GouraudSkin);
	LoadOrCompileShader(Path, LambertSkin);
	LoadOrCompileShader(Path, BlinnPhongSkin);
	LoadOrCompileShader(Path, WorldNormalSkin);

	// 뷰모드 -> 변형 테이블. 컴파일에 실패한 키는 nullptr로 남는다(GetShaderProgram의 폴백을 쓰지 않으려고 맵을 직접 찾는다).
	auto FindProgram = [this](const FString& Key) -> FShaderProgram*
	{
		TUniquePtr<FShaderProgram>* Found = ShaderProgramMap.Find(Key);
		return Found ? Found->get() : nullptr;
	};

	UberLitDefault = FindProgram(MakeShaderKey(Path, nullptr));
	UberLitPrograms[static_cast<int32>(EViewMode::Lit_Gouraud)] = FindProgram(MakeShaderKey(Path, Gouraud));
	UberLitPrograms[static_cast<int32>(EViewMode::Lit_Lambert)] = FindProgram(MakeShaderKey(Path, Lambert));
	UberLitPrograms[static_cast<int32>(EViewMode::Lit_BlinnPhong)] = FindProgram(MakeShaderKey(Path, BlinnPhong));
	UberLitPrograms[static_cast<int32>(EViewMode::Lit)] = UberLitPrograms[static_cast<int32>(EViewMode::Lit_BlinnPhong)];
	UberLitPrograms[static_cast<int32>(EViewMode::Unlit)] = FindProgram(MakeShaderKey(Path, Unlit));
	UberLitPrograms[static_cast<int32>(EViewMode::WorldNormal)] = FindProgram(MakeShaderKey(Path, WorldNormal));
	// Wireframe은 선 색만 보이면 되고, Scene Depth는 깊이만 쓰므로 둘 다 조명 없는 Unlit을 쓴다.
	UberLitPrograms[static_cast<int32>(EViewMode::Wireframe)] = UberLitPrograms[static_cast<int32>(EViewMode::Unlit)];
	UberLitPrograms[static_cast<int32>(EViewMode::SceneDepth)] = UberLitPrograms[static_cast<int32>(EViewMode::Unlit)];
	UberLitSkinnedPrograms[static_cast<int32>(EViewMode::Lit_Gouraud)] = FindProgram(MakeShaderKey(Path, GouraudSkin));
	UberLitSkinnedPrograms[static_cast<int32>(EViewMode::Lit_Lambert)] = FindProgram(MakeShaderKey(Path, LambertSkin));
	UberLitSkinnedPrograms[static_cast<int32>(EViewMode::Lit_BlinnPhong)] = FindProgram(MakeShaderKey(Path, BlinnPhongSkin));
	UberLitSkinnedPrograms[static_cast<int32>(EViewMode::Lit)] = UberLitSkinnedPrograms[static_cast<int32>(EViewMode::Lit_BlinnPhong)];
	UberLitSkinnedPrograms[static_cast<int32>(EViewMode::Unlit)] = FindProgram(MakeShaderKey(Path, UnlitSkin));
	UberLitSkinnedPrograms[static_cast<int32>(EViewMode::WorldNormal)] = FindProgram(MakeShaderKey(Path, WorldNormalSkin));
	UberLitSkinnedPrograms[static_cast<int32>(EViewMode::Wireframe)] = UberLitSkinnedPrograms[static_cast<int32>(EViewMode::Unlit)];
	UberLitSkinnedPrograms[static_cast<int32>(EViewMode::SceneDepth)] = UberLitSkinnedPrograms[static_cast<int32>(EViewMode::Unlit)];
}

FShaderProgram* FRenderResourceManager::GetUberLitProgram(EViewMode Mode)
{
	const int32 Index = static_cast<int32>(Mode);
	if (Index < 0 || Index >= static_cast<int32>(EViewMode::Count))
	{
		return nullptr;
	}
	return Get().UberLitPrograms[Index];
}

FShaderProgram* FRenderResourceManager::GetUberLitSkinnedProgram(EViewMode Mode)
{
	const int32 Index = static_cast<int32>(Mode);
	if (Index < 0 || Index >= static_cast<int32>(EViewMode::Count))
	{
		return nullptr;
	}
	return Get().UberLitSkinnedPrograms[Index];
}

bool FRenderResourceManager::IsUberLitProgram(const FShaderProgram* Program)
{
	if (!Program)
	{
		return false;
	}

	const FRenderResourceManager& Self = Get();
	if (Program == Self.UberLitDefault)
	{
		return true;
	}
	for (const FShaderProgram* Variant : Self.UberLitPrograms)
	{
		if (Program == Variant)
		{
			return true;
		}
	}
	return false;
}

void FRenderResourceManager::Shutdown()
{
	Get().VertexShaderMap.Empty();
	Get().PixelShaderMap.Empty();
	Get().ComputeShaderMap.Empty();
	Get().ShaderProgramMap.Empty();
	Get().UberLitDefault = nullptr;
	for (FShaderProgram*& Program : Get().UberLitPrograms)
	{
		Program = nullptr;
	}
}

FShaderProgram* FRenderResourceManager::GetShaderProgram(const FString& InPath, const D3D_SHADER_MACRO* Macro)
{
	const FString Key = MakeShaderKey(InPath, Macro);

	if (TUniquePtr<FShaderProgram>* Found = Get().ShaderProgramMap.Find(Key))
	{
		return Found->get();
	}

	HTR_LOG(Error, "[Shader] not found: {}", Key);

	if (TUniquePtr<FShaderProgram>* Fallback =
			Get().ShaderProgramMap.Find("Resources/Shader/DefaultShader.hlsl"))
	{
		return Fallback->get();
	}

	return nullptr;
}

FShaderProgram* FRenderResourceManager::FindOrLoadShaderProgram(const FString& InPath, const D3D_SHADER_MACRO* Macro)
{
	const FString Key = MakeShaderKey(InPath, Macro);
	FRenderResourceManager& Manager = Get();
	if (TUniquePtr<FShaderProgram>* Found = Manager.ShaderProgramMap.Find(Key))
	{
		return Found->get();
	}

	// 실패한 경로는 매 프레임 다시 컴파일하지 않는다.
	if (Manager.FailedShaderPaths.Contains(Key))
	{
		return nullptr;
	}

	Manager.LoadOrCompileShader(InPath, Macro);
	if (TUniquePtr<FShaderProgram>* Loaded = Manager.ShaderProgramMap.Find(Key))
	{
		return Loaded->get();
	}

	Manager.FailedShaderPaths.Add(Key);
	return nullptr;
}

FComputeShader* FRenderResourceManager::GetComputeShader(const FString& InPath)
{
	if (TUniquePtr<FComputeShader>* Found = Get().ComputeShaderMap.Find(InPath))
	{
		return Found->get();
	}
	return nullptr;
}
void FRenderResourceManager::LoadOrCompileShader(const FString& Path, const D3D_SHADER_MACRO* Macro)
{
	std::ifstream In(Path);
	const std::string Source((std::istreambuf_iterator<char>(In)), std::istreambuf_iterator<char>());

	const bool bHasVS = Source.find("mainVS") != std::string::npos;
	const bool bHasPS = Source.find("mainPS") != std::string::npos;
	const bool bHasCS = Source.find("mainCS") != std::string::npos;
	const bool HasGraphicsInclude{ !bHasCS && Source.find("#include") != std::string::npos };

	if (bHasVS || bHasPS || HasGraphicsInclude)
	{
		FString VSCSOPath;
		FString PSCSOPath;
		FShaderByteCode VSCode = RenderUtil::GetOrCompile(Path, "mainVS", EShaderType::Vertex, VSCSOPath, Macro);
		FShaderByteCode PSCode = RenderUtil::GetOrCompile(Path, "mainPS", EShaderType::Pixel, PSCSOPath, Macro);
		const FString Key = MakeShaderKey(Path, Macro);



		if (!VSCode.IsValid() || !PSCode.IsValid())
		{
			HTR_LOG(Error, "[Shader] compile failed: {}", Key);
			return;
		}

		TUniquePtr<FVertexShader> Vs = RenderCommand::CreateVertexShader(VSCode);
		TUniquePtr<FPixelShader> Ps = RenderCommand::CreatePixelShader(PSCode);

		if (!Vs || !Vs->IsValid() || !Ps || !Ps->IsValid())
		{
			HTR_LOG(Error, "[Shader] device create failed: {}", Key);
			return;
		}

		FVertexShader* VsRaw = Vs.get();
		FPixelShader* PsRaw = Ps.get();

		VertexShaderMap[VSCSOPath] = std::move(Vs);
		PixelShaderMap[PSCSOPath] = std::move(Ps);

		ShaderProgramMap[Key] = MakeUnique<FShaderProgram>(VsRaw, PsRaw);

		HTR_LOG(Info, "[Shader] Loaded Shader: {}", Path);
	}

	if (bHasCS)
	{
		FString CSOPath;
		FShaderByteCode CSCode = RenderUtil::GetOrCompile(Path, "mainCS", EShaderType::Compute, CSOPath);
		if (!CSCode.IsValid())
		{
			HTR_LOG(Error, "[Shader] compute compile failed: {}", Path);
			return;
		} // 진짜 에러

		ComputeShaderMap[Path] = RenderCommand::CreateComputeShader(CSCode);

		HTR_LOG(Info, "[Shader] Loaded ComputeShader: {}", Path);
	}
}
