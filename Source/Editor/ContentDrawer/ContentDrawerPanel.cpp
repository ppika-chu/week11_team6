#include "EnginePCH.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"

#include "Animation/AnimSequence.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Asset/AssetManager.h"
#include "Text/Font.h"
#include "Editor/HitoriEd/EditorDragDrop.h"

namespace
{
std::string ToUTF8String(const fs::path& Path)
{
	const std::u8string UTF8Path = Path.generic_u8string();
	return std::string{ UTF8Path.begin(), UTF8Path.end() };
}

constexpr double FolderRefreshSeconds = 1.0;
constexpr int MaxAssetNameLines = 2;

// 이름이 Width 폭으로 줄바꿈했을 때 MaxLines 안에 들어가는지 본다. 파일명의 ##을 숨기지 않도록 그대로 잰다.
bool FitsInLines(const std::string& Text, float Width, int MaxLines)
{
	const float Height = ImGui::CalcTextSize(Text.c_str(), nullptr, false, Width).y;
	return Height <= ImGui::GetTextLineHeight() * MaxLines + 0.5f;
}

// 다 들어가면 그대로 돌려주고, 넘치면 들어가는 만큼만 남기고 ...을 붙인다.
std::string FitNameToLines(const std::string& Name, float Width, int MaxLines)
{
	if (FitsInLines(Name, Width, MaxLines))
	{
		return Name;
	}

	// UTF-8 글자 중간에서 자르지 않도록 글자 시작 위치만 후보로 삼는다.
	std::vector<size_t> Boundaries;
	for (size_t Index = 0; Index < Name.size(); ++Index)
	{
		if ((static_cast<unsigned char>(Name[Index]) & 0xC0) != 0x80)
		{
			Boundaries.push_back(Index);
		}
	}

	size_t Low = 0;
	size_t High = Boundaries.size();
	while (Low + 1 < High)
	{
		const size_t Mid = (Low + High) / 2;
		if (FitsInLines(Name.substr(0, Boundaries[Mid]) + "...", Width, MaxLines))
		{
			Low = Mid;
		}
		else
		{
			High = Mid;
		}
	}
	return Name.substr(0, Boundaries.empty() ? 0 : Boundaries[Low]) + "...";
}

// 툴팁과 드래그 미리보기에서 전체 이름을 그린다. 긴 이름이 상자를 화면 밖까지 늘리지 않게 줄바꿈한다.
void DrawFullName(const std::string& Name)
{
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + EditorUIScale::Px(400.0f));
	ImGui::TextUnformatted(Name.c_str());
	ImGui::PopTextWrapPos();
}
}

bool FContentDrawerPanel::Init()
{
	return false;
}

void FContentDrawerPanel::Tick(float DeltaTime)
{
}

const FContentDrawerPanel::FFolderListing& FContentDrawerPanel::GetFolderListing(const fs::path& Path)
{
	FFolderListing& Listing = FolderCache[ToUTF8String(Path)];
	const double Now = ImGui::GetTime();
	if (Listing.LastReadTime >= 0.0 && Now - Listing.LastReadTime < FolderRefreshSeconds)
	{
		return Listing;
	}

	Listing.LastReadTime = Now;
	Listing.Entries.Reset();

	std::error_code IterateError;
	fs::directory_iterator It(Path, IterateError);
	const fs::directory_iterator End;
	for (; !IterateError && It != End; It.increment(IterateError))
	{
		std::error_code TypeError;
		const bool bIsDirectory = It->is_directory(TypeError);
		if (!TypeError)
		{
			Listing.Entries.Add({ It->path(), bIsDirectory });
		}
	}
	Listing.bReadFailed = static_cast<bool>(IterateError);
	return Listing;
}

void FContentDrawerPanel::OnRender()
{
	ImGui::Begin("Content Drawer");

	if (GetFolderListing(CurrentPath).bReadFailed)
	{
		CurrentPath = AssetRootPath;
	}

	if (GetFolderListing(AssetRootPath).bReadFailed)
	{
		ImGui::TextUnformatted("Asset folder is unavailable.");
		ImGui::End();
		return;
	}

	float treeViewWidth = EditorUIScale::Px(200.0f);
	ImGui::BeginChild("FolderTreeView", ImVec2(treeViewWidth, 0));
	{
		// 루트 노드
		if (ImGui::TreeNodeEx(AssetRootPath.string().c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | (AssetRootPath == CurrentPath ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_DrawLinesToNodes))
		{
			if (ImGui::IsItemClicked())
			{
				CurrentPath = AssetRootPath;
			}
			RenderFolderTree(AssetRootPath, CurrentPath);
			ImGui::TreePop();
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	ImGui::BeginChild("AssetGridView");
	{
		// 그리드 레이아웃 계산
		// 칸 폭에는 썸네일 외에 버튼 프레임 여백과 테이블 셀 여백이 함께 들어간다.
		const ImGuiStyle& GridStyle = ImGui::GetStyle();
		float padding = GridStyle.FramePadding.x * 2.0f + GridStyle.CellPadding.x * 2.0f;
		float panelWidth = ImGui::GetContentRegionAvail().x;
		float thumbnailSize = std::max(1.0f, std::min(EditorUIScale::Px(80.0f), panelWidth - padding));
		float cellSize = thumbnailSize + padding;
		int ColumnCount = static_cast<int>(panelWidth / cellSize);
		if (ColumnCount < 1)
		{
			ColumnCount = 1;
		}

		// 테이블 기반 그리드 시작
		if (ImGui::BeginTable("AssetGridTable", ColumnCount))
		{
			for (const FFolderEntry& Entry : GetFolderListing(CurrentPath).Entries)
			{
				const fs::path& Path = Entry.Path;
				const bool bIsDirectory = Entry.bIsDirectory;

				ImGui::TableNextColumn();

				std::string PathString = ToUTF8String(Path);
				std::string FilenameString = ToUTF8String(Path.filename());

				ImGui::PushID(PathString.c_str()); // 각 위젯에 고유 ID 부여
				UTexture2D* Thumbnail = nullptr;

				// 실제 에셋일 때만 채워진다.
				// 폴더/파일 아이콘은 끌 수 없어야 하므로 썸네일과 구분해서 들고 있는다.
				UTexture2D* TextureAsset = nullptr;
				UFont* FontAsset = nullptr;
				URenderAsset* FileAsset = nullptr;	// 더블클릭으로 열 에셋
				UAnimSequence* AnimationAsset = nullptr;

				// 폴더 또는 파일 아이콘 표시
				if (bIsDirectory)
				{
					// Resources는 스캔하지 않으므로 경로로 불러온다. 한 번 불러오면 AssetMap에 캐시된다.
					Thumbnail = UAssetManager::Get().LoadTexture(EditorResourcePaths::FolderIcon);
				}
				else // 파일인 경우
				{
					URenderAsset* Asset = UAssetManager::GetAssetByPath<URenderAsset>(PathString);
					// 기존 텍스처 키는 그대로 두고, 새 FBX 바이너리는 Assets 상대 파일 키로 조회한다.
					if (!Asset && Path.extension() == ".fbxbin")
					{
						FString Key, Error;
						if (FbxPath::GetAssetKey(PathString, Key, Error)) Asset = UAssetManager::GetAssetByPath<URenderAsset>(Key);
					}
					FileAsset = Asset;
					if (Asset && Asset->IsA<UTexture2D>())
					{
						TextureAsset = Cast<UTexture2D>(Asset);
						Thumbnail = TextureAsset;
					}
					else if (Asset && Asset->IsA<UFont>())
					{
						FontAsset = Cast<UFont>(Asset);

						// 폰트는 글리프 아틀라스를 그대로 미리보기로 쓴다
						Thumbnail = FontAsset->AtlasTexture;
					}

					AnimationAsset = Cast<UAnimSequence>(Asset);

					if (Thumbnail == nullptr)
					{
						Thumbnail = UAssetManager::Get().LoadTexture(EditorResourcePaths::FileIcon);
					}
				}
				if (Thumbnail && Thumbnail->GetResource())
				{
					ImGui::ImageButton(PathString.c_str(), (ImTextureID)Thumbnail->GetResource()->GetSRV(), { thumbnailSize, thumbnailSize });
				}
				else
				{
					// 썸네일이 없어도 같은 자리에서 클릭과 드래그를 받는다.
					ImGui::Button("##NoThumbnail", { thumbnailSize + GridStyle.FramePadding.x * 2.0f, thumbnailSize + GridStyle.FramePadding.y * 2.0f });
				}
				const bool bThumbnailHovered = ImGui::IsItemHovered();

				// 페이로드에 포인터를 그대로 실어서 받는 쪽이 AssetMap을 다시 뒤지지 않게 한다.
				if (TextureAsset && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload(EditorDragDrop::Texture, &TextureAsset, sizeof(UTexture2D*));

					// 끌고 다니는 동안 커서를 따라다니는 미리보기
					if (TextureAsset->GetResource())
					{
						ImGui::Image((ImTextureID)TextureAsset->GetResource()->GetSRV(), { thumbnailSize, thumbnailSize });
					}
					DrawFullName(FilenameString);

					ImGui::EndDragDropSource();
				}
				if (FontAsset && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload(EditorDragDrop::Font, &FontAsset, sizeof(UFont*));

					DrawFullName(FilenameString);

					ImGui::EndDragDropSource();
				}
				if (AnimationAsset && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload(EditorDragDrop::AnimSequence, &AnimationAsset, sizeof(UAnimationAsset*));
					DrawFullName(FilenameString);
					ImGui::EndDragDropSource();
				}
				// 스크립트는 에셋 객체가 없으므로 파일 경로를 그대로 싣는다.
				if (!bIsDirectory && Path.extension() == ".lua" && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload(EditorDragDrop::ScriptFile, PathString.c_str(), PathString.size() + 1);

					DrawFullName(FilenameString);

					ImGui::EndDragDropSource();
				}
				// 더블 클릭 이벤트 처리
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					if (bIsDirectory)
					{
						CurrentPath /= Path.filename(); // 하위 폴더로 이동
					}
					else if (FileAsset && OpenAssetCallback)
					{
						OpenAssetCallback(FileAsset);
					}
				}
				// 파일 이름 표시. 긴 이름이 행 높이를 늘리지 않게 줄 수를 제한하고, 전체 이름은 툴팁으로 보여 준다.
				const std::string DisplayName = FitNameToLines(FilenameString, ImGui::GetContentRegionAvail().x, MaxAssetNameLines);
				ImGui::TextWrapped("%s", DisplayName.c_str());
				if (DisplayName != FilenameString && (bThumbnailHovered || ImGui::IsItemHovered()) && !ImGui::IsDragDropActive())
				{
					ImGui::BeginTooltip();
					DrawFullName(FilenameString);
					ImGui::EndTooltip();
				}

				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}
	ImGui::EndChild();
	ImGui::End();
}

void FContentDrawerPanel::RenderFolderTree(const fs::path& Path, fs::path& SelectedPath)
{
	const FFolderListing& Listing = GetFolderListing(Path);

	for (const FFolderEntry& Child : Listing.Entries)
	{
		if (!Child.bIsDirectory)
		{
			continue;
		}

		const fs::path& Entry = Child.Path;

		const FString FilenameString =
			ToUTF8String(Entry.filename());

		ImGuiTreeNodeFlags Flags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_DrawLinesToNodes;

		if (Entry == SelectedPath)
		{
			Flags |= ImGuiTreeNodeFlags_Selected;
		}

		const bool bIsOpen =
			ImGui::TreeNodeEx(FilenameString.c_str(), Flags);

		if (ImGui::IsItemClicked())
		{
			SelectedPath = Entry;
		}

		if (bIsOpen)
		{
			RenderFolderTree(Entry, SelectedPath);
			ImGui::TreePop();
		}
	}

	if (Listing.bReadFailed)
	{
		ImGui::TextUnformatted("Could not read this folder.");
	}
}
