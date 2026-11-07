#include "EnginePCH.h"
#include "RenderResourceManager.h"

#include "RenderCommand.h"

#include "RenderUtil.h"
#include "VertexFactory.h"

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

struct FLightingVariant
{
	EViewMode Mode;
	const char* Define;
};

static constexpr FLightingVariant LightingVariants[] = {
	{ EViewMode::Unlit, "LIGHTING_MODEL_UNLIT" },
	{ EViewMode::Lit_Gouraud, "LIGHTING_MODEL_GOURAUD" },
	{ EViewMode::Lit_Lambert, "LIGHTING_MODEL_LAMBERT" },
	{ EViewMode::Lit_BlinnPhong, "LIGHTING_MODEL_BLINN_PHONG" },
	{ EViewMode::WorldNormal, "WORLD_NORMAL" },
};
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

	// 뷰모드 -> 변형 테이블. 컴파일에 실패한 키는 nullptr로 남는다(GetShaderProgram의 폴백을 쓰지 않으려고 맵을 직접 찾는다).
	auto FindProgram = [this](const FString& Key) -> FShaderProgram*
	{
		TUniquePtr<FShaderProgram>* Found = ShaderProgramMap.Find(Key);
		return Found ? Found->get() : nullptr;
	};

	LoadOrCompileShader(Path); // 기본 (Lambert)
	UberLitDefault = FindProgram(MakeShaderKey(Path, nullptr));

	for (const FVertexFactoryTypeInfo& VF : GVertexFactoryTypes)
	{
		FShaderProgram** Row = UberLitPrograms[static_cast<int32>(VF.Type)];
		for (const FLightingVariant& L : LightingVariants)
		{
			D3D_SHADER_MACRO Macros[3]{};
			int32 N = 0;
			Macros[N++] = { L.Define, "1" };
			if (VF.Define)
			{
				Macros[N++] = { VF.Define, "1" };
			}

			LoadOrCompileShader(Path, Macros);
			Row[static_cast<int32>(L.Mode)] = FindProgram(MakeShaderKey(Path, Macros));
		}

		Row[static_cast<int32>(EViewMode::Lit)] = Row[static_cast<int32>(EViewMode::Lit_BlinnPhong)];
		Row[static_cast<int32>(EViewMode::Wireframe)] = Row[static_cast<int32>(EViewMode::Unlit)];
		Row[static_cast<int32>(EViewMode::SceneDepth)] = Row[static_cast<int32>(EViewMode::Unlit)];
	}
}

FShaderProgram* FRenderResourceManager::GetUberLitProgram(EVertexFactoryType VFType, EViewMode Mode)
{
	const int32 VFIndex = static_cast<int32>(VFType);
	const int32 ViewModeIndex = static_cast<int32>(Mode);
	if (VFIndex < 0 || VFIndex >= static_cast<int32>(EVertexFactoryType::Count) ||
		ViewModeIndex < 0 || ViewModeIndex >= static_cast<int32>(EViewMode::Count))
	{
		return nullptr;
	}
	return Get().UberLitPrograms[VFIndex][ViewModeIndex];
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
	for (const auto& Row : Self.UberLitPrograms)
	{
		for (const FShaderProgram* Variant : Row)
		{
			if (Program == Variant)
			{
				return true;
			}
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
	for (auto& Row : Get().UberLitPrograms)
	{
		for (FShaderProgram*& Variant : Row)
		{
			Variant = nullptr;
		}
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

FShaderProgram* FRenderResourceManager::FindOrLoadShaderProgram(const FString& Path, const char* Define, EVertexFactoryType VertexFactoryType)
{
	D3D_SHADER_MACRO Macros[3]{};
	int32 N = 0;
	if (Define)
	{
		Macros[N++] = { Define, "1" };
	}
	if (const char* VertexFactoryDefine = GVertexFactoryTypes[static_cast<int32>(VertexFactoryType)].Define)
	{
		Macros[N++] = { VertexFactoryDefine, "1" };
	}
	return FindOrLoadShaderProgram(Path, N ? Macros : nullptr);
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
