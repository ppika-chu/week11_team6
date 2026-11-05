local function Shell(Command)
	return '"' .. Command .. '"'
end

local function FindVcpkg()
	local Root = os.getenv("VCPKG_ROOT")
	if Root and os.isfile(Root .. "/vcpkg.exe") then
		return Root .. "/vcpkg.exe"
	end

	local VsWhere = os.getenv("ProgramFiles(x86)") .. "/Microsoft Visual Studio/Installer/vswhere.exe"
	if os.isfile(VsWhere) then
		local VsPath = os.outputof(Shell('"' .. VsWhere .. '" -latest -property installationPath'))
		if VsPath and os.isfile(VsPath .. "/VC/vcpkg/vcpkg.exe") then
			return VsPath .. "/VC/vcpkg/vcpkg.exe"
		end
	end
end

-- 이 스크립트는 Setup/에 있지만 아래의 모든 경로는 저장소 루트 기준이다.
-- premake는 상대 경로를 현재 디렉터리 기준으로 절대 경로화하므로 루트로 옮겨 둔다.
local RepoRoot = path.getabsolute(path.join(_MAIN_SCRIPT_DIR, ".."))
os.chdir(RepoRoot)

-- SDK는 소스 임포트에만 필요하다. 바이너리 애셋 로더/게임 빌드는 SDK 없이도 사용할 수 있다.
local FbxRoot = os.getenv("FBX_SDK_ROOT") or (RepoRoot .. "/Source/ThirdParty/FBX/2020.2")
local HasFbx = os.isfile(FbxRoot .. "/include/fbxsdk.h")
newoption { trigger = "fbx-tests", description = "Generate the FBX importer integration test project (requires FBX_SDK_ROOT)" }

-- vcpkg 매니페스트는 Setup/vcpkg, 설치 결과는 생성물이므로 Intermediate 아래에 둔다.
VcpkgDir = "Intermediate/vcpkg_installed/x64-windows"
if _ACTION and _ACTION:match("^vs") and not os.isdir(RepoRoot .. "/" .. VcpkgDir .. "/lib") then
	local Vcpkg = FindVcpkg()
	if Vcpkg then
		os.execute(Shell('"' .. Vcpkg .. '" install --triplet x64-windows'
			.. ' --x-manifest-root="' .. RepoRoot .. '/Setup/vcpkg"'
			.. ' --x-install-root="' .. RepoRoot .. '/Intermediate/vcpkg_installed"'))
	else
		print("vcpkg를 찾지 못했습니다. VCPKG_ROOT를 설정하거나 Visual Studio의 vcpkg 구성 요소를 설치하세요.")
	end
end

workspace "Hitori"
	location(RepoRoot)   -- 솔루션은 루트에 둔다. 기본값은 이 스크립트가 있는 Setup/이다.
	architecture "x64"
	startproject "HitoriEditor"

	configurations
	{
		"Debug",
		"Release",
		"Executable",
	}

	multiprocessorcompile "On"

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

IncludeDir = {}
IncludeDir["ImGui"] = "Source/ThirdParty/ImGui"
IncludeDir["stb"]   = "Source/ThirdParty/stb"
IncludeDir["json"]  = "Source/ThirdParty/json"
IncludeDir["DDSLoader"] = "Source/ThirdParty/DDSLoader"
IncludeDir["miniaudio"] = "Source/ThirdParty/miniaudio"
IncludeDir["Sol2"] = VcpkgDir .. "/include"
IncludeDir["LuaJIT"] = VcpkgDir .. "/include/luajit"

-- premake의 filter는 project()를 만나면 초기화된다.
-- 두 프로젝트가 같은 런타임(/MDd vs /MD)으로 컴파일되지 않으면 링크가 실패하므로
-- 공통 설정을 함수로 묶어 각 프로젝트에서 호출한다.
function CommonSettings()
	language   "C++"
	cppdialect "C++20"
	staticruntime "off"
	characterset  "Unicode"

	targetdir ("Build/Bin/" .. outputdir)
	objdir    ("Build/Intermediate/" .. outputdir)
	-- 에셋·쉐이더를 상대 경로로 읽으므로 작업 디렉터리는 저장소 루트다.
	debugdir  "%{wks.location}"

	defines
	{
		"WIN32_LEAN_AND_MEAN",
		"NOMINMAX",
		"UNICODE",
		"_UNICODE",
	}

	filter "files:**.hlsl"
		excludefrombuild "On"

	filter "system:windows"
		systemversion "latest"
		buildoptions { "/utf-8" }
		defines { "ENGINE_PLATFORM_WINDOWS" }

	filter "configurations:Debug"
		defines { "ENGINE_DEBUG", "_DEBUG" }
		runtime  "Debug"
		symbols  "on"

	filter "configurations:Release"
		defines  { "ENGINE_RELEASE", "NDEBUG" }
		runtime  "Release"
		optimize "on"
		symbols  "on"

	-- 에디터 없이 게임만 실행하는 패키징 빌드
	filter "configurations:Executable"
		defines  { "ENGINE_RELEASE", "NDEBUG", "EXECUTABLE" }
		runtime  "Release"
		optimize "on"
		symbols  "on"

	filter {}

	if HasFbx then
		filter "configurations:Debug or Release"
			defines { "WITH_FBX_SDK=1" }
			includedirs { FbxRoot .. "/include" }
	else
		filter "configurations:Debug or Release"
			defines { "WITH_FBX_SDK=0" }
	end
	filter "configurations:Executable"
		defines { "WITH_FBX_SDK=0" }
	filter {}
end


-- 프로젝트 구성
--   HitoriEngine : 런타임 lib. 엔진만 쓰는 서드파티(DDSLoader, miniaudio)를 함께 컴파일한다.
--   HitoriEditor : 에디터·게임 exe. 에디터만 쓰는 서드파티(ImGui, ImNeoSequencer)를 함께 컴파일한다.
--   Shaders      : 셰이더 보기 전용
-- 각 프로젝트의 필터는 소스 폴더 구조를 그대로 따르고, 프로젝트 폴더 밖 파일만 vpaths로 묶는다.

-- 런타임. 에디터를 모른다. include 경로에 Source/Editor가 없는 것이 그 방벽이다.
-- location이 소스 루트와 같아야 "모든 파일 표시"에서 폴더가 보인다.
project "HitoriEngine"
	location "Source/Runtime"
	kind     "StaticLib"
	CommonSettings()

	pchheader "EnginePCH.h"
	pchsource "Source/Runtime/EnginePCH.cpp"

	files
	{
		"Source/Runtime/**.h",
		"Source/Runtime/**.hpp",
		"Source/Runtime/**.cpp",
		"%{IncludeDir.DDSLoader}/*.h",
		"%{IncludeDir.DDSLoader}/*.cpp",
		"%{IncludeDir.miniaudio}/*.h",
		"%{IncludeDir.miniaudio}/*.cpp",
	}

	-- Source/Runtime 밖에 있는 외부 소스
	vpaths
	{
		["ThirdParty/DDSLoader"] = "%{IncludeDir.DDSLoader}/*",
		["ThirdParty/miniaudio"] = "%{IncludeDir.miniaudio}/*",
	}

	includedirs
	{
		"Source/Runtime",
		"%{IncludeDir.LuaJIT}",
    	"%{IncludeDir.Sol2}",
		"%{IncludeDir.stb}",
		"%{IncludeDir.json}",
		"%{IncludeDir.DDSLoader}",
		"%{IncludeDir.miniaudio}",
	}

	-- D3D 시스템 lib는 exe(HitoriEditor)에서 링크한다. 정적 lib에 넣으면 import lib가 엔진 lib 안에 합쳐져
	-- /WHOLEARCHIVE로 강제 링크할 때 __NULL_IMPORT_DESCRIPTOR가 중복 정의된다.

	filter "files:Source/ThirdParty/DDSLoader/**.cpp"
		enablepch "Off"
	filter "files:Source/ThirdParty/miniaudio/**.cpp"
		enablepch "Off"
	filter "files:Source/Runtime/Scripting/**.cpp"
		buildoptions { "/bigobj" }
	filter {}

-- 에디터 애플리케이션. Executable 구성에서는 같은 exe가 에디터 없는 게임으로 빌드된다.
project "HitoriEditor"
	location "Source/Editor"
	kind     "WindowedApp"
	CommonSettings()

	pchheader "EnginePCH.h"
	pchsource "Source/Editor/EditorPCH.cpp"

	links { "HitoriEngine" }
	if HasFbx then
		filter "configurations:Debug"
			libdirs { FbxRoot .. "/lib/vs2017/x64/debug" }
		filter "configurations:Release"
			libdirs { FbxRoot .. "/lib/vs2017/x64/release" }
		filter "configurations:Debug or Release"
			links { "libfbxsdk-md", "libxml2-md", "zlib-md", "wininet", "ws2_32" }
		filter {}
	end

	-- 엔진은 정적 라이브러리라 exe가 참조하지 않는 .obj는 링크에서 빠지고,
	-- 그 안의 클래스 자동 등록(DECLARE_CLASS의 FAutoRegister)도 실행되지 않는다. 엔진 전체를 강제로 링크한다.
	linkoptions { "/WHOLEARCHIVE:HitoriEngine.lib" }

	files
	{
		"Source/Editor/**.h",
		"Source/Editor/**.cpp",
		"%{IncludeDir.ImGui}/*.h",
		"%{IncludeDir.ImGui}/*.cpp",
		"%{IncludeDir.ImGui}/backends/imgui_impl_win32.*",
		"%{IncludeDir.ImGui}/backends/imgui_impl_dx11.*",
		"Source/ThirdParty/ImNeoSequencer/*.h",
		"Source/ThirdParty/ImNeoSequencer/*.cpp",
		"Source/Editor/**.rc",   -- 창·exe 아이콘
	}

	-- Source/Editor 밖에 있는 파일이 ".." 필터로 잡히지 않게 묶는다.
	vpaths
	{
		["ThirdParty/ImGui/*"] = "%{IncludeDir.ImGui}/**",
		["ThirdParty/ImNeoSequencer"] = "Source/ThirdParty/ImNeoSequencer/*",
	}

	includedirs
	{
		"Source",             -- "Editor/OutputLog/ConsolePanel.h"처럼 Source 기준 include를 쓴다
		"Source/Runtime",
		"%{IncludeDir.LuaJIT}",
    	"%{IncludeDir.Sol2}",
		"%{IncludeDir.ImGui}",
		"%{IncludeDir.ImGui}/backends",
		"%{IncludeDir.stb}",
		"%{IncludeDir.json}",
	}

	links
	{
		"lua51",
		"d3d11.lib",
		"dxgi.lib",
		"dxguid.lib",
		"d3dcompiler.lib",
	}

	filter "files:Source/ThirdParty/ImGui/**.cpp or files:Source/ThirdParty/ImNeoSequencer/*.cpp"
		enablepch "Off"
	filter {}

	filter "configurations:Debug"
		libdirs
		{
			VcpkgDir .. "/debug/lib",
		}

		postbuildcommands
		{
			"{COPYFILE} %[" .. VcpkgDir .. "/debug/bin/lua51.dll] %[%{!cfg.targetdir}/lua51.dll]",
		}

	filter "configurations:Release or Executable"
		libdirs
		{
			VcpkgDir .. "/lib",
		}

		postbuildcommands
		{
			"{COPYFILE} %[" .. VcpkgDir .. "/bin/lua51.dll] %[%{!cfg.targetdir}/lua51.dll]",
		}

	filter {}

	-- 게임 exe에는 에디터 코드가 필요 없다. Source/Editor 루트의 엔트리 포인트·PCH·아이콘(.rc)만 남긴다.
	filter "configurations:Executable"
		targetname "Executable"
		removefiles
		{
			"Source/Editor/*/**",
			"%{IncludeDir.ImGui}/**",
			"Source/ThirdParty/ImNeoSequencer/**",
		}

	filter {}


if _OPTIONS["fbx-tests"] and HasFbx then
	project "FbxImporterTests"
		location "Intermediate/ProjectFiles"
		kind "ConsoleApp"
		CommonSettings()
		files { "Tests/FbxImporterTests.cpp" }
		includedirs { "Source/Runtime", "Source/ThirdParty/json", "Source/ThirdParty/stb" }
		links { "HitoriEngine", "d3d11", "dxgi", "dxguid", "d3dcompiler", "lua51", "libfbxsdk-md", "libxml2-md", "zlib-md", "wininet", "ws2_32" }
		filter "configurations:Debug"
			libdirs { FbxRoot .. "/lib/vs2017/x64/debug", VcpkgDir .. "/debug/lib" }
		filter "configurations:Release or Executable"
			libdirs { FbxRoot .. "/lib/vs2017/x64/release", VcpkgDir .. "/lib" }
		filter {}
end

-- 셰이더를 VS에서 편집하기 위한 보기 전용 프로젝트 (언리얼의 Shaders와 같은 역할).
-- 에셋(Assets)은 에디터 Content Drawer에서 다루므로 솔루션에 넣지 않는다.
-- 새 셰이더 파일은 SlnGenerator.bat을 다시 돌려야 목록에 반영된다.
project "Shaders"
	location "Intermediate/ProjectFiles"
	kind     "Utility"

	files { "Resources/Shader/**.hlsl" }
	files { "Resources/Shader/**.hlsli" }
	files { "Assets/Shaders/**.hlsl" }   -- 게임 셰이더 (UI 셰이더 등). 처음 쓸 때 컴파일된다.
	-- 공용 include(.hlsli)는 따로 모으고, 엔진 셰이더 본체는 루트에, 게임 셰이더는 Game 아래에 둔다.
	vpaths
	{
		["Include"] = "Resources/Shader/**.hlsli",
		["Game/*"] = "Assets/Shaders/**.hlsl",
		["*"] = "Resources/Shader/**.hlsl",
	}

	filter "files:**.hlsl"
		excludefrombuild "On"

	filter "files:**.hlsli"
		excludefrombuild "On"

	filter {}
