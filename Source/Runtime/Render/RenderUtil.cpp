#include "EnginePCH.h"
#include "RenderUtil.h"

#include <set>

namespace
{
// Debug는 최적화 없이 디버그 정보를 넣어 컴파일하므로, Release와 같은 .cso를 나눠 쓰지 않도록 폴더를 나눈다.
#ifdef _DEBUG
constexpr const char* ShaderCacheDir = "Intermediate/Shaders/Debug";
#else
constexpr const char* ShaderCacheDir = "Intermediate/Shaders/Release";
#endif

const char* GetShaderTarget(EShaderType Type)
{
	switch (Type)
	{
	case EShaderType::Vertex:
		return "vs_5_0";
		break;
	case EShaderType::Pixel:
		return "ps_5_0";
		break;
	case EShaderType::Compute:
		return "cs_5_0";
		break;
	default:
		return "";
		break;
	}
}

const char* GetShaderPrefix(EShaderType Type)
{
	switch (Type)
	{
	case EShaderType::Vertex:
		return "_VS.cso";
		break;
	case EShaderType::Pixel:
		return "_PS.cso";
		break;
	case EShaderType::Compute:
		return "_CS.cso";
		break;
	default:
		return "";
		break;
	}
}
}

FShaderByteCode RenderUtil::CompileShader(FString Path, const char* EntryPoint, EShaderType ShaderType, const D3D_SHADER_MACRO* Macros)
{
	const char* Target = GetShaderTarget(ShaderType);

	uint32 Flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
	Flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	FShaderByteCode ByteCode;

	std::wstring WPath(Path.begin(), Path.end());

	ComPtr<ID3DBlob> ErrorBlob;
	HRESULT hr = D3DCompileFromFile(WPath.c_str(), Macros, D3D_COMPILE_STANDARD_FILE_INCLUDE, EntryPoint, Target, Flags, 0, ByteCode.Blob.GetAddressOf(), ErrorBlob.GetAddressOf());

	if (FAILED(hr))
	{
		if (ErrorBlob)
		{
			OutputDebugStringA((const char*)ErrorBlob->GetBufferPointer());
		}
		else
		{
			OutputDebugStringA(("[Shader] file not found: " + Path + "\n").c_str());
		}

		ByteCode.Blob.Reset();
	}
	return ByteCode;
}

void RenderUtil::SaveByteCode(FString CsoPath, const FShaderByteCode& ByteCode)
{
	fs::create_directories(fs::path(CsoPath).parent_path());
	std::ofstream Out(CsoPath, std::ios::binary);
	if (!Out)
	{
		fs::remove(CsoPath);
		return;
	}
	Out.write(reinterpret_cast<const char*>(ByteCode.GetData()), ByteCode.GetSize());
}

FShaderByteCode RenderUtil::LoadByteCode(const FString& CsoPath)
{
	FShaderByteCode Result;

	std::ifstream In(CsoPath, std::ios::binary | std::ios::ate);
	if (!In)
	{
		return Result;
	}

	const std::streamsize Size = In.tellg();
	In.seekg(0);
	if (Size <= 0)
	{
		return Result;
	}

	ComPtr<ID3DBlob> Blob;
	if (FAILED(D3DCreateBlob(static_cast<SIZE_T>(Size), &Blob)))
	{
		return Result;
	}

	In.read(reinterpret_cast<char*>(Blob->GetBufferPointer()), Size);
	if (In)
	{
		Result.Blob = std::move(Blob);
	}
	return Result;
}

FString RenderUtil::GetCsoPath(const FString& ShaderPath, EShaderType ShaderType, const D3D_SHADER_MACRO* Macros)
{
	const fs::path Rel = fs::relative(ShaderPath, "Resources/Shader");
	const char* Suffix = GetShaderPrefix(ShaderType);
	FString MacroSuffix;
	while (Macros && Macros->Name)
	{
		MacroSuffix += FString("_") + Macros->Name;
		Macros++;
	}

	fs::path Out = fs::path(ShaderCacheDir) / Rel.parent_path();
	Out /= Rel.stem().generic_string() + MacroSuffix.c_str() + Suffix;
	return Out.generic_string();
}

namespace
{
// 셰이더와 그 셰이더가 #include "..."로 끌어오는 파일(중첩 포함) 중 가장 최근 수정 시각을 구한다.
// D3D_COMPILE_STANDARD_FILE_INCLUDE처럼 include 경로는 포함하는 파일의 폴더 기준이다.
// 찾을 수 없는 파일이 있으면 false를 돌려 재컴파일하게 한다.
bool GetNewestSourceTime(const fs::path& Path, std::set<fs::path>& Visited, fs::file_time_type& InOutNewest)
{
	std::error_code Ec;
	const fs::path Normalized = fs::weakly_canonical(Path, Ec);
	if (Ec)
	{
		return false;
	}
	if (!Visited.insert(Normalized).second)
	{
		return true; // 이미 본 파일 (중복·순환 include)
	}

	const fs::file_time_type Time = fs::last_write_time(Normalized, Ec);
	if (Ec)
	{
		return false;
	}
	InOutNewest = std::max(InOutNewest, Time);

	std::ifstream SourceFile{ Normalized };
	if (!SourceFile)
	{
		return false;
	}

	std::string Line;
	while (std::getline(SourceFile, Line))
	{
		const size_t Directive = Line.find("#include");
		if (Directive == std::string::npos)
		{
			continue;
		}
		const size_t Open = Line.find('"', Directive);
		const size_t Close = Open == std::string::npos ? std::string::npos : Line.find('"', Open + 1);
		if (Close == std::string::npos)
		{
			continue; // <...> 형식은 엔진 셰이더에서 쓰지 않는다
		}

		const fs::path Included = Normalized.parent_path() / Line.substr(Open + 1, Close - Open - 1);
		if (!GetNewestSourceTime(Included, Visited, InOutNewest))
		{
			return false;
		}
	}
	return true;
}
}

// .cso 파일이 셰이더와 그 include 파일들보다 나중에 만들어졌는지 비교한다.
bool RenderUtil::IsCsoUpToDate(const FString& ShaderPath, const FString& CsoPath)
{
	std::error_code Ec;
	if (!fs::exists(CsoPath, Ec))
	{
		return false;
	}

	const auto CsoTime = fs::last_write_time(CsoPath, Ec);
	if (Ec)
	{
		return false;
	}

	std::set<fs::path> Visited;
	fs::file_time_type NewestSourceTime = fs::file_time_type::min();
	if (!GetNewestSourceTime(ShaderPath, Visited, NewestSourceTime))
	{
		return false;
	}

	return CsoTime > NewestSourceTime;
}

FShaderByteCode RenderUtil::GetOrCompile(const FString& ShaderPath, const char* EntryPoint, EShaderType ShaderType, FString& OutCSOPath, const D3D_SHADER_MACRO* Macros)
{
	OutCSOPath = GetCsoPath(ShaderPath, ShaderType, Macros);

	if (IsCsoUpToDate(ShaderPath, OutCSOPath))
	{
		FShaderByteCode Cached = LoadByteCode(OutCSOPath);
		if (Cached.IsValid())
		{
			return Cached;
		}
	}

	FShaderByteCode Compiled = CompileShader(ShaderPath, EntryPoint, ShaderType, Macros);
	if (Compiled.IsValid())
	{
		SaveByteCode(OutCSOPath, Compiled);
	}

	return Compiled;
}
