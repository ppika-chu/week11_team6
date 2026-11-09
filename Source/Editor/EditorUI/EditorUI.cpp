#include "EnginePCH.h"
#include "Editor/EditorUI/EditorUI.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Asset/FbxImporter/FbxAssetLoader.h"
#include "Asset/FbxImporter/FbxImporter.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Asset/AssetManager.h"
#include "Animation/Skeleton.h"
#include <commdlg.h>
#include <shobjidl.h>
#include <cstring>
#include <thread>
#include <algorithm>

namespace
{
constexpr ImGuiID RootWindowClassId = 0x524F4F54;
// 예전 메인 도킹 공간의 ID 그대로. 저장된 imgui.ini의 패널 배치를 유지한다
constexpr ImGuiID LevelDockSpaceId = 0xF5B08511;

HRESULT ChooseFbxOutputFolder(HWND Owner, const fs::path& InitialDirectory, FString& Selected)
{
	HRESULT Result = E_FAIL;
	// 오디오 등이 메인 스레드를 MTA로 초기화할 수 있어 Shell UI는 별도의 STA에서 연다.
	std::thread Browser([&]
	{
		const HRESULT Init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		if (FAILED(Init)) { Result = Init; return; }
		{
			try
			{
				ComPtr<IFileOpenDialog> Dialog;
				Result = CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Dialog));
				if (SUCCEEDED(Result))
				{
					FILEOPENDIALOGOPTIONS Flags{};
					Result = Dialog->GetOptions(&Flags);
					if (SUCCEEDED(Result)) Result = Dialog->SetOptions(Flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
					Dialog->SetTitle(L"Select FBX output folder inside Assets");
					fs::path Initial = InitialDirectory;
					while (!Initial.empty() && !fs::is_directory(Initial))
					{
						const fs::path Parent = Initial.parent_path();
						if (Parent == Initial) { Initial.clear(); break; }
						Initial = Parent;
					}
					ComPtr<IShellItem> Start;
					if (!Initial.empty() && SUCCEEDED(SHCreateItemFromParsingName(Initial.c_str(), nullptr, IID_PPV_ARGS(&Start))))
						Dialog->SetFolder(Start.Get());
					if (SUCCEEDED(Result)) Result = Dialog->Show(Owner);
					if (SUCCEEDED(Result))
					{
						ComPtr<IShellItem> Item;
						Result = Dialog->GetResult(&Item);
						PWSTR Path = nullptr;
						if (SUCCEEDED(Result)) Result = Item->GetDisplayName(SIGDN_FILESYSPATH, &Path);
						if (SUCCEEDED(Result)) Selected = FbxPath::ToString(fs::path(Path));
						CoTaskMemFree(Path);
					}
				}
			}
			catch (...) { Result = E_FAIL; }
		}
		CoUninitialize();
	});
	// 소유 창으로 오는 메시지도 처리해야 다른 스레드의 모달 대화상자가 정상 동작한다.
	const HANDLE Thread = Browser.native_handle();
	bool Quit = false;
	int QuitCode = 0;
	while (MsgWaitForMultipleObjects(1, &Thread, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
	{
		MSG Message;
		while (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
		{
			if (Message.message == WM_QUIT) { Quit = true; QuitCode = static_cast<int>(Message.wParam); }
			else { TranslateMessage(&Message); DispatchMessageW(&Message); }
		}
	}
	Browser.join();
	if (Quit) PostQuitMessage(QuitCode);
	return Result;
}
}

const ImGuiWindowClass& FEditorUI::GetRootWindowClass()
{
	static const ImGuiWindowClass RootClass = []
	{
		ImGuiWindowClass Class;
		Class.ClassId = RootWindowClassId;
		Class.DockingAllowUnclassed = false;
		return Class;
	}();
	return RootClass;
}

bool FEditorUI::Init(bool bInUseDockSpace, bool bInPassthruCentralNode)
{
	bUseDockSpace = bInUseDockSpace;
	bPassthruCentralNode = bInPassthruCentralNode;

	Toolbar = MakeUnique<FLevelEditorToolbar>();

	return true;
}

void FEditorUI::Tick(float DeltaTime)
{
	for (auto& Panel : Panels)
	{

		Panel->Tick(DeltaTime);
	}
}

void FEditorUI::OnRender()
{
	DrawMainMenuBar();

	if (bUseDockSpace)
	{
		static bool dockspaceOpen = true;
		static bool optFullscreen = true;
		static bool optPadding = false;
		static ImGuiDockNodeFlags dockspaceFlags = bPassthruCentralNode
			? ImGuiDockNodeFlags_PassthruCentralNode
			: ImGuiDockNodeFlags_None;

		// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
		// because it would be confusing to have two docking targets within each others.
		// ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking;
		if (optFullscreen)
		{
			const ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->WorkPos);
			ImGui::SetNextWindowSize(viewport->WorkSize);
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
			windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		}
		else
		{
			dockspaceFlags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
		}

		// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
		// and handle the pass-thru hole, so we ask Begin() to not render a background.
		if (dockspaceFlags & ImGuiDockNodeFlags_PassthruCentralNode)
		{
			windowFlags |= ImGuiWindowFlags_NoBackground;
		}

		// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
		// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
		// all active windows docked into it will lose their parent and become undocked.
		// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
		// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
		if (!optPadding)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		}

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
		ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });

		ImGui::Begin("DockingSpace", &dockspaceOpen, windowFlags);
		ImGui::PopStyleColor(); // MenuBarBg
		ImGui::PopStyleVar(2);

		if (!optPadding)
		{
			ImGui::PopStyleVar();
		}

		if (optFullscreen)
		{
			ImGui::PopStyleVar(2);
		}

		// Submit the DockSpace
		ImGuiIO& io = ImGui::GetIO();
		const bool bDockingEnabled = (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) != 0;
		const ImGuiWindowClass& RootClass = GetRootWindowClass();
		ImGuiID RootDockId = 0;
		if (bDockingEnabled)
		{
			// 루트 창끼리만 붙는 바깥 도킹 공간. 루트 창의 탭은 하나뿐이어도 항상 보인다
			RootDockId = ImGui::GetID("MainWindowDockSpace");
			ImGui::DockSpace(RootDockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None, &RootClass);
		}

		ImGui::End();

		if (bDockingEnabled)
		{
			// 레벨 패널이 붙는 루트 창. 패널은 클래스가 없어서 바깥 도킹 공간과 FBX 뷰어에 붙지 않는다
			ImGui::SetNextWindowClass(&RootClass);
			ImGui::SetNextWindowDockID(RootDockId, ImGuiCond_FirstUseEver);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			ImGui::Begin("Level Editor###LevelEditorRootWindow", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			ImGui::PopStyleVar();
			if (Toolbar)
			{
				Toolbar->Draw();
			}
			// 탭이 가려져 Begin이 false여도 호출해야 패널의 도킹이 풀리지 않는다
			ImGui::DockSpace(LevelDockSpaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
			ImGui::End();
		}
	}

	for (auto& Panel : Panels)
	{
		if (Panel->IsOpen())
		{
			Panel->OnRender();
		}
	}
	DrawFbxImportDialog();
}

// 파일 메뉴의 요청을 애플리케이션 콜백으로 전달한다.
void FEditorUI::DrawMainMenuBar()
{
	// 메뉴바 높이만 글자 크기에 맞춰 키운다. 간격을 함께 바꾸면 펼친 메뉴까지 물려받는다.
	ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, ImGui::GetFontSize() * 0.5f);

	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Scene"))
			{
				if (OnNewScene)
				{
					OnNewScene();
				}
			}
			if (ImGui::MenuItem("Open Scene..."))
			{
				if (OnOpenScene)
				{
					OnOpenScene();
				}
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Import FBX..."))
			{
				bRequestFbxImportDialog = true;
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Save"))
			{
				if (OnSaveScene)
				{
					OnSaveScene();
				}
			}
			if (ImGui::MenuItem("Save As..."))
			{
				if (OnSaveSceneAs)
				{
					OnSaveSceneAs();
				}
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Window"))
		{
			for (auto& Panel : Panels)
			{
				bool bOpen = Panel->IsOpen();

				if (ImGui::MenuItem(Panel->GetPanelName(), nullptr, bOpen))
				{
					Panel->SetOpen(!bOpen);
				}
			}
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}
	ImGui::PopStyleVar();
}

void FEditorUI::BrowseFbxSource()
{
	wchar_t Path[32768]{};
	MultiByteToWideChar(CP_UTF8, 0, FbxSourcePath, -1, Path, static_cast<int>(std::size(Path)));
	const std::wstring InitialDirectory = fs::absolute("Assets").wstring();
	OPENFILENAMEW Dialog{};
	Dialog.lStructSize = sizeof(Dialog);
	Dialog.hwndOwner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
	Dialog.lpstrFilter = L"FBX files (*.fbx)\0*.fbx\0\0";
	Dialog.lpstrDefExt = L"fbx";
	Dialog.lpstrFile = Path;
	Dialog.nMaxFile = static_cast<DWORD>(std::size(Path));
	Dialog.lpstrInitialDir = InitialDirectory.c_str();
	Dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (GetOpenFileNameW(&Dialog))
	{
		char SelectedPath[sizeof(FbxSourcePath)]{};
		if (WideCharToMultiByte(CP_UTF8, 0, Path, -1, SelectedPath, sizeof(SelectedPath), nullptr, nullptr))
		{
			std::memcpy(FbxSourcePath, SelectedPath, sizeof(FbxSourcePath));
			UpdateFbxOutputDirectory();
			FbxResult = {};
		}
		else FbxResult.Error = "The selected file path is too long.";
	}
	else if (const DWORD Error = CommDlgExtendedError())
	{
		FbxResult.Error = "Could not open the file browser.";
		HTR_LOG(Error, "FBX file dialog failed: {}", Error);
	}
}

void FEditorUI::UpdateFbxOutputDirectory()
{
	if (!bFbxOutputDirectoryEdited)
	{
		const FString Default = FbxPath::DefaultOutputDirectory(FbxSourcePath);
		strcpy_s(FbxOutputDirectory, Default.c_str());
	}
}

void FEditorUI::BrowseFbxOutputDirectory()
{
	FString Selected;
	fs::path Initial;
	FString Ignored;
	if (!FbxPath::GetOutputDirectory(FbxOutputDirectory, Initial, Ignored)) Initial = fs::absolute("Assets");
	const HRESULT Result = ChooseFbxOutputFolder(static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw), Initial, Selected);
	if (Result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return;
	if (FAILED(Result))
	{
		FbxResult.Error = "Could not open the output folder browser.";
		HTR_LOG(Error, "FBX output folder dialog failed: {}", static_cast<uint32>(Result));
		return;
	}
	fs::path Directory;
	FString Error;
	if (!FbxPath::GetOutputDirectory(Selected, Directory, Error)) { FbxResult.Error = Error; return; }
	const FString Path = FbxPath::ToString(Directory);
	if (Path.size() >= sizeof(FbxOutputDirectory)) { FbxResult.Error = "The selected folder path is too long."; return; }
	strcpy_s(FbxOutputDirectory, Path.c_str());
	bFbxOutputDirectoryEdited = true;
	FbxResult = {};
}

void FEditorUI::DrawFbxImportDialog()
{
	// File 메뉴의 ID 스택을 벗어난 뒤 같은 위치에서 팝업을 열고 그린다.
	if (bRequestFbxImportDialog)
	{
		bRequestFbxImportDialog = false;
		bFbxImportDialogOpen = true;
		FbxResult = {};
		UpdateFbxOutputDirectory();
		ImGui::OpenPopup("Import FBX");
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
	ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(600.0f), 0), ImGuiCond_Appearing);
	if (ImGui::BeginPopupModal("Import FBX", &bFbxImportDialogOpen, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
	{
		ImGui::TextUnformatted("Source FBX");
		const float PathWidth = ImGui::GetContentRegionAvail().x - EditorUIScale::ButtonWidth("Browse...") - ImGui::GetStyle().ItemSpacing.x;
		ImGui::SetNextItemWidth(PathWidth);
		if (ImGui::InputText("##FbxSource", FbxSourcePath, sizeof(FbxSourcePath)))
		{
			UpdateFbxOutputDirectory();
			FbxResult = {};
		}
		ImGui::SameLine();
		if (ImGui::Button("Browse...")) BrowseFbxSource();
		ImGui::TextUnformatted("Output folder");
		ImGui::SetNextItemWidth(PathWidth);
		if (ImGui::InputText("##FbxOutput", FbxOutputDirectory, sizeof(FbxOutputDirectory)))
		{
			bFbxOutputDirectoryEdited = FbxOutputDirectory[0] != '\0';
			FbxResult = {};
		}
		ImGui::SameLine();
		if (ImGui::Button("Browse...##FbxOutput")) BrowseFbxOutputDirectory();

		ImGui::SeparatorText("FBX Translator");
		ImGui::Checkbox("Keep FBX Namespace", &FbxOptions.bKeepFbxNamespace);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off: remove the namespace from names (mixamorig:Hips -> Hips).\nOn: keep it with ':' replaced by '_' (mixamorig:Hips -> mixamorig_Hips).\nApplies to bone, mesh, material, texture and animation names.");

		ImGui::SeparatorText("Static Meshes");
		ImGui::PushID("StaticFbx");
		ImGui::Checkbox("Bake Meshes", &FbxOptions.bBakeStaticMeshes);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: bake the mesh node and all parent transforms into geometry.\nOff: import local geometry without the mesh node transform.\nCombine does not restore relative placement when Bake is off.");
		ImGui::Checkbox("Combine Meshes", &FbxOptions.bCombineStaticMeshes);
		ImGui::PopID();

		ImGui::SeparatorText("Skeletal Meshes");
		ImGui::PushID("SkeletalFbx");
		// 언리얼 임포트 옵션 Skeleton. None이면 새 SKEL_을 만들고, 고르면 그 스켈레톤을 공유하며 없는 본은 합친다
		ImGui::TextUnformatted("Skeleton");
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##FbxSkeleton", FbxOptions.Skeleton.empty() ? "None (create new)" : FbxOptions.Skeleton.c_str()))
		{
			if (ImGui::Selectable("None (create new)", FbxOptions.Skeleton.empty()))
			{
				FbxOptions.Skeleton.clear();
			}
			TArray<FString> SkeletonKeys;
			UAssetManager::ForEachAsset([&SkeletonKeys](URenderAsset& Asset)
				{
					if (Cast<USkeleton>(&Asset) && !Asset.IsRetired())
					{
						SkeletonKeys.Add(Asset.GetPath());
					}
				});
			std::sort(SkeletonKeys.begin(), SkeletonKeys.end());
			for (const FString& Key : SkeletonKeys)
			{
				if (ImGui::Selectable(Key.c_str(), FbxOptions.Skeleton == Key))
				{
					FbxOptions.Skeleton = Key;
				}
			}
			ImGui::EndCombo();
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("None: create a new skeleton for this FBX.\nSelected: share that skeleton. Bones it lacks are added to it.\nFor an animation-only FBX, pick the character's skeleton to play it on that character.");
		ImGui::Checkbox("Bake Meshes", &FbxOptions.bBakeSkeletalMeshes);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: include placement outside the rig in geometry and bind transforms.\nOff: remove the root bone parent's global bind transform; retain the root bone's own transform.");
		ImGui::Checkbox("Combine Meshes", &FbxOptions.bCombineSkeletalMeshes);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Combine meshes using the same source rig root. An LOD group remains one asset when disabled.");
		ImGui::Checkbox("Use T0 As Ref Pose", &FbxOptions.bUseT0AsRefPose);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: use the time 0 pose as the reference pose.\nAn invalid FBX BindPose always uses time 0.");
		ImGui::PopID();

		ImGui::SeparatorText("Animation");
		ImGui::PushID("AnimationFbx");
		ImGui::Checkbox("Import Animations", &FbxOptions.bImportAnimations);
		ImGui::BeginDisabled(!FbxOptions.bImportAnimations);
		int AnimationRange = static_cast<int>(FbxOptions.AnimationRange);
		if (ImGui::Combo("Animation Length", &AnimationRange, "Source Timeline\0Animated Time\0Set Range\0"))
			FbxOptions.AnimationRange = static_cast<EInterchangeAnimationRange>(AnimationRange);
		ImGui::BeginDisabled(FbxOptions.AnimationRange != EInterchangeAnimationRange::SetRange);
		int FrameImportRange[2]{ FbxOptions.FrameImportRangeMin, FbxOptions.FrameImportRangeMax };
		if (ImGui::InputInt2("Frame Import Range", FrameImportRange))
		{
			FbxOptions.FrameImportRangeMin = std::max(FrameImportRange[0], 0);
			FbxOptions.FrameImportRangeMax = std::max(FrameImportRange[1], 0);
		}
		ImGui::EndDisabled();
		int FrameAlignment = static_cast<int>(FbxOptions.FrameAlignment);
		if (ImGui::Combo("Frame Alignment", &FrameAlignment, "None\0Snap To Floor\0Snap To Closest Frame Boundary\0Snap To Ceiling\0"))
			FbxOptions.FrameAlignment = static_cast<EInterchangeFrameAlignment>(FrameAlignment);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("None: clips that do not end on a frame border are not imported.");
		ImGui::EndDisabled();
		ImGui::PopID();

		if (!FFbxImporter::IsAvailable()) ImGui::TextWrapped("Run SlnGenerator.bat and rebuild the editor to enable FBX import.");
		if (!FbxResult.Error.empty())
		{
			ImGui::Separator();
			ImGui::TextWrapped("Import failed: %s", FbxResult.Error.c_str());
		}

		ImGui::Separator();
		ImGui::BeginDisabled(!FFbxImporter::IsAvailable() || FbxSourcePath[0] == '\0' || FbxOutputDirectory[0] == '\0');
		if (ImGui::Button("Import", ImVec2(EditorUIScale::Px(100.0f), 0)))
		{
			FbxResult = FFbxAssetLoader::Import(FbxSourcePath, FbxOptions, FbxOutputDirectory);
			if (FbxResult.bSuccess)
			{
				HTR_LOG(Info, "FBX import completed: {} assets", FbxResult.AssetKeys.Num());
				bFbxImportDialogOpen = false;
				ImGui::CloseCurrentPopup();
			}
			else HTR_LOG(Error, "FBX import failed: {}", FbxResult.Error);
			for (const auto& Warning : FbxResult.Warnings) HTR_LOG(Warning, "FBX: {}", Warning);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(EditorUIScale::Px(100.0f), 0)))
		{
			bFbxImportDialogOpen = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}
