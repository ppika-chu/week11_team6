#include "EnginePCH.h"

#include "Editor/AnimationEditor/AnimationEditor.h"
#include "Editor/EditorUI/EditorUI.h"
#include "Editor/EditorUI/EditorUIScale.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Asset/FbxImporter/FbxAssetLoader.h"
#include "Asset/FbxImporter/FbxAssetBake.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "UObject/UObjectHash.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/Samples/AnimNotifyDemoInstance.h"
#include "Animation/Skeleton.h"
#include "Asset/AssetManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "Editor/Persona/DebugSkelMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "Render/Texture2D.h"
#include "ThirdParty/ImNeoSequencer/imgui_neo_sequencer.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <format>

namespace
{
	// 저장된 배치가 없을 때의 기본 배치: 왼쪽 트리 | 가운데 뷰포트 위, 타임라인 아래
	void BuildDefaultPanelLayout(ImGuiID DockId, const ImVec2& Size, const char* TreePanel, const char* ViewportPanel, const char* TimelinePanel, const char* NotifyPanel)
	{
		const float TreeWidth = EditorUIScale::Px(250.0f);
		const float TimelineHeight = EditorUIScale::Px(180.0f);
		const float TreeRatio = std::clamp(TreeWidth / Size.x, 0.1f, 0.4f);
		const float TimelineRatio = std::clamp(TimelineHeight / Size.y, 0.15f, 0.5f);

		ImGui::DockBuilderAddNode(DockId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(DockId, Size);

		ImGuiID CenterId = DockId;
		const ImGuiID TreeId = ImGui::DockBuilderSplitNode(CenterId, ImGuiDir_Left, TreeRatio, nullptr, &CenterId);
		const ImGuiID NotifyId = ImGui::DockBuilderSplitNode(CenterId, ImGuiDir_Right, 0.3f, nullptr, &CenterId);
		const ImGuiID TimelineId = ImGui::DockBuilderSplitNode(CenterId, ImGuiDir_Down, TimelineRatio, nullptr, &CenterId);

		ImGui::DockBuilderDockWindow(TreePanel, TreeId);
		ImGui::DockBuilderDockWindow(NotifyPanel, NotifyId);
		ImGui::DockBuilderDockWindow(ViewportPanel, CenterId);
		ImGui::DockBuilderDockWindow(TimelinePanel, TimelineId);
		ImGui::DockBuilderFinish(DockId);
	}

	// 불러오지 못한 아이콘은 글자 버튼으로 대신한다
	bool IsIconReady(const UTexture2D* Icon)
	{
		return Icon && Icon->GetResource();
	}

	// 배경 없는 아이콘 버튼 (언리얼 Animation.PlayControlsButton). 아이콘이 없으면 글자 버튼
	bool DrawTransportButton(const char* Id, UTexture2D* Icon, const char* FallbackLabel, const char* Tooltip)
	{
		bool bClicked = false;
		if (IsIconReady(Icon))
		{
			const float IconSize = ImGui::GetTextLineHeight();
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
			bClicked = ImGui::ImageButton(Id, (ImTextureID)Icon->GetResource()->GetSRV(), ImVec2(IconSize, IconSize));
			ImGui::PopStyleColor();
		}
		else
		{
			ImGui::PushID(Id);
			bClicked = ImGui::Button(FallbackLabel);
			ImGui::PopID();
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("%s", Tooltip);
		}
		return bClicked;
	}

	// 속도 메뉴에 보이는 글자. 언리얼 "x1.0"
	FString FormatPlaybackSpeed(float Speed)
	{
		return std::format("x{:.1f}", Speed);
	}

	// 재생 중에는 프레임 사이 시각이라 내림한다. 키 위치의 float 오차로 한 칸 덜 나오지 않게 여유를 둔다
	int32 TimeToFrame(float Time, float FrameRate)
	{
		return static_cast<int32>(std::floor(Time * FrameRate + 1.0e-3f));
	}
}

FAnimationEditor::FAnimationEditor(UAnimSequence* InSequence, USkeletalMesh* InPreviewMesh, int32 InEditorId)
	: FPersonaAssetEditor(InPreviewMesh, InEditorId)
	, Sequence(InSequence)
	// ### 뒤가 ImGui 창 ID. 에셋 키로 두어 imgui.ini에 에셋별 마지막 위치 / 도킹이 저장되게 한다
	, WindowTitle(std::format("{} - Animation Editor###AnimationEditor_{}", GetDisplayName(InSequence->GetPath()), InSequence->GetPath()))
	, TreePanelTitle(std::format("Skeleton Tree###AnimSkeletonTree_{}", InSequence->GetPath()))
	, ViewportPanelTitle(std::format("Viewport###AnimViewport_{}", InSequence->GetPath()))
	, TimelinePanelTitle(std::format("Timeline###AnimTimeline_{}", InSequence->GetPath()))
	, NotifyPanelTitle(std::format("Notifies###AnimNotifies_{}", InSequence->GetPath()))
{
	// 시각 진행과 포즈 계산은 프리뷰 월드의 TickComponent가 한다
	PreviewScene->GetPreviewComponent()->PlayAnimation(Sequence, true);

	// Resources는 스캔하지 않으므로 경로로 불러온다. AssetMap에 캐시된다
	UAssetManager& AssetManager = UAssetManager::Get();
	ToFrontIcon = AssetManager.LoadTexture(EditorResourcePaths::AnimToFrontIcon);
	ToPreviousIcon = AssetManager.LoadTexture(EditorResourcePaths::AnimToPreviousIcon);
	PlayReverseIcon = AssetManager.LoadTexture(EditorResourcePaths::AnimPlayReverseIcon);
	PlayForwardIcon = AssetManager.LoadTexture(EditorResourcePaths::AnimPlayForwardIcon);
	PauseIcon = AssetManager.LoadTexture(EditorResourcePaths::AnimPauseIcon);
	ToNextIcon = AssetManager.LoadTexture(EditorResourcePaths::AnimToNextIcon);
	ToEndIcon = AssetManager.LoadTexture(EditorResourcePaths::AnimToEndIcon);
	LoopingIcon = AssetManager.LoadTexture(EditorResourcePaths::AnimLoopingIcon);
	NoLoopingIcon = AssetManager.LoadTexture(EditorResourcePaths::AnimNoLoopingIcon);
}

URenderAsset* FAnimationEditor::GetEditedAsset() const
{
	return Sequence;
}

TUniquePtr<FPersonaAssetEditor> FAnimationEditor::CreateReplacementEditor(int32 NewEditorId) const
{
	// 키로 찾으면 retired는 건너뛰므로 새 객체가 나온다. 없으면 옛 객체를 계속 보여 준다
	UAnimSequence* NewSequence = UAssetManager::GetAssetByPath<UAnimSequence>(Sequence->GetPath());
	USkeletalMesh* NewMesh = NewSequence && NewSequence->GetSkeleton() ? NewSequence->GetSkeleton()->FindCompatibleMesh() : nullptr;
	if (!NewMesh)
	{
		return nullptr;
	}
	// 선택한 본은 이름이 남아 있으면 유지한다
	auto NewEditor = MakeUnique<FAnimationEditor>(NewSequence, NewMesh, NewEditorId);
	NewEditor->SelectBoneByName(GetSelectedBoneName());
	return NewEditor;
}

UAnimSingleNodeInstance* FAnimationEditor::GetPreviewInstance() const
{
	UAnimSingleNodeInstance* Instance = PreviewScene->GetPreviewComponent()->GetSingleNodeInstance();
	return Instance && Instance->GetCurrentAsset() ? Instance : nullptr;
}

void FAnimationEditor::OnRender()
{
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		DraggedNotifyIndex = -1;
	}
	if (bFocusRequested)
	{
		ImGui::SetNextWindowFocus();
		bFocusRequested = false;
	}

	ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(1100.0f), EditorUIScale::Px(700.0f)), ImGuiCond_FirstUseEver);
	// 스켈레탈 에디터처럼 루트 창이라 Level Editor나 다른 뷰어와 붙을 수 있고, 레벨 패널과는 섞이지 않는다
	ImGui::SetNextWindowClass(&FEditorUI::GetRootWindowClass());
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool bHostVisible = ImGui::Begin(WindowTitle.c_str(), &bIsOpen,
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | (bNotifyDirty ? ImGuiWindowFlags_UnsavedDocument : 0));
	ImGui::PopStyleVar();
	if (!bIsOpen && bNotifyDirty)
	{
		bIsOpen = true;
		ImGui::OpenPopup("Unsaved Notifies");
	}
	if (ImGui::BeginPopupModal("Unsaved Notifies", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Save notify changes before closing?");
		if (ImGui::Button("Save and Close") && SaveNotifies())
		{
			bIsOpen = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		const UEditorEngine* Editor = Cast<UEditorEngine>(GEngine);
		const bool bPlayingPIE = Editor && Editor->IsPlaying();
		ImGui::BeginDisabled(bPlayingPIE);
		if (ImGui::Button("Discard and Close") && DiscardNotifyChanges())
		{
			bIsOpen = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		if (bPlayingPIE && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("Stop PIE to discard notify changes.");
		}
		ImGui::SameLine();
		if (ImGui::Button("Keep Editing"))
		{
			ImGui::CloseCurrentPopup();
		}
		if (!SaveMessage.empty())
		{
			ImGui::TextWrapped("%s", SaveMessage.c_str());
		}
		ImGui::EndPopup();
	}
	bool bAnyPanelFocused = bHostVisible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	// 이 에셋의 패널끼리만 붙는 도킹 공간. ID가 창 ID에서 나오므로 재임포트해도 배치가 유지된다
	const ImGuiID PanelDockId = ImGui::GetID("PanelDockSpace");
	ImGuiWindowClass PanelClass;
	PanelClass.ClassId = PanelDockId;
	PanelClass.DockingAllowUnclassed = false;
	if (!ImGui::DockBuilderGetNode(PanelDockId))
	{
		BuildDefaultPanelLayout(PanelDockId, ImGui::GetWindowSize(), TreePanelTitle.c_str(), ViewportPanelTitle.c_str(), TimelinePanelTitle.c_str(), NotifyPanelTitle.c_str());
	}
	// 탭이 가려져 Begin이 false여도 호출해야 패널의 도킹이 풀리지 않는다
	ImGui::DockSpace(PanelDockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None, &PanelClass);
	ImGui::End();

	const auto BeginPanel = [&](const FString& Title, ImGuiWindowFlags Flags)
	{
		ImGui::SetNextWindowClass(&PanelClass);
		const bool bPanelVisible = ImGui::Begin(Title.c_str(), nullptr, Flags | ImGuiWindowFlags_NoCollapse);
		bAnyPanelFocused |= bPanelVisible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		return bPanelVisible;
	};

	// 깊은 본 이름이 칸 끝에서 잘리지 않도록 가로로 스크롤할 수 있게 한다
	if (BeginPanel(TreePanelTitle, ImGuiWindowFlags_HorizontalScrollbar))
	{
		DrawSkeletonTree();
	}
	ImGui::End();

	// 휠은 줌에 쓰므로 스크롤을 끈다
	bIsVisible = BeginPanel(ViewportPanelTitle, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	if (bIsVisible)
	{
		DrawViewport();
	}
	ImGui::End();

	if (BeginPanel(TimelinePanelTitle, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
	{
		DrawTimeline();
	}
	ImGui::End();

	if (BeginPanel(NotifyPanelTitle, ImGuiWindowFlags_None))
	{
		DrawNotifyEditor();
	}
	ImGui::End();

	bIsFocused = bAnyPanelFocused;

	// 언리얼 Persona처럼 스페이스로 재생 / 일시정지. 패널 중 하나가 포커스일 때만, 글자 입력 중에는 받지 않는다
	if (bIsFocused && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space, false))
	{
		OnClick_Forward();
	}
}

void FAnimationEditor::DrawTimeline()
{
	const UAnimDataModel* DataModel = Sequence->DataModel;
	const float FrameRate = DataModel && DataModel->FrameRate > 0.0f ? DataModel->FrameRate : 30.0f;
	const int32 NumFrames = DataModel ? DataModel->NumberOfFrames : 0;
	UAnimSingleNodeInstance* Instance = GetPreviewInstance();

	// 위: 레벨 시퀀서와 같은 ImNeoSequencer 타임라인. 아래 한 줄은 재생 버튼
	// 라이브러리가 크기 없는 자식 창을 열어 남은 높이를 다 쓰므로, 높이를 정한 자식 창으로 감싼다 (SequencePanel과 같다)
	const float TimelineHeight = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing();
	// 라이브러리는 시작 < 끝 프레임이어야 한다
	if (NumFrames > 0 && TimelineHeight > 0.0f)
	{
		if (ImGui::BeginChild("##AnimTimelineArea", ImVec2(0.0f, TimelineHeight), ImGuiChildFlags_None,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			// 라이브러리는 정수 프레임만 다룬다. 재생 중 프레임 사이 시각은 내림해서 보여 준다
			int32 CurrentFrame = Instance ? TimeToFrame(Instance->GetCurrentTime(), FrameRate) : 0;
			const int32 PreviousFrame = CurrentFrame;
			// 길이 변경 플래그를 주지 않아 라이브러리가 바꾸지 않는다. 클립 길이는 고치지 않으므로 복사본을 넘긴다
			int32 StartFrame = 0;
			int32 EndFrame = NumFrames;
			// 줌 바(보이는 범위 "0 - 79")는 숨긴다
			if (ImGui::BeginNeoSequencer("##AnimTimeline", &CurrentFrame, &StartFrame, &EndFrame, ImGui::GetContentRegionAvail(),
				ImGuiNeoSequencerFlags_HideZoom))
			{
				// 언리얼처럼 손잡이 옆에 현재 프레임 (재생 시간) (진행률)
				// 이번 프레임에 손잡이를 옮겼으면 옮긴 프레임의 시간을 보여 준다
				const float CurrentTime = CurrentFrame != PreviousFrame ? static_cast<float>(CurrentFrame) / FrameRate
					: (Instance ? Instance->GetCurrentTime() : 0.0f);
				const float PlayLength = Sequence->GetPlayLength();
				const float Percent = PlayLength > 0.0f ? CurrentTime / PlayLength * 100.0f : 0.0f;
				const FString FrameLabel = std::format("{} ({:.2f}) ({:.2f}%)", CurrentFrame, CurrentTime, Percent);
				ImGui::NeoSetCurrentFrameLabel(FrameLabel.c_str());

				DrawNotifiesTrack(FrameRate);
				ImGui::EndNeoSequencer();
			}

			// 손잡이를 끌거나 눈금 줄을 누르면 그 프레임으로 이동한다 (SequencePanel과 같은 방식)
			if (CurrentFrame != PreviousFrame && Instance)
			{
				HandleScrubPositionChanged(static_cast<float>(CurrentFrame) / FrameRate);
			}
		}
		// Begin 결과와 관계없이 닫는다
		ImGui::EndChild();
	}

	DrawTransportControls();
}

void FAnimationEditor::DrawNotifiesTrack(float FrameRate)
{
	if (!ImGui::BeginNeoTimelineEx("Notifies"))
	{
		return;
	}

	const UEditorEngine* Editor = Cast<UEditorEngine>(GEngine);
	const bool bCanEdit = !Editor || !Editor->IsPlaying();
	if (!bCanEdit)
	{
		DraggedNotifyIndex = -1;
	}
	if (DraggedNotifyIndex >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		FAnimNotifyEvent& Event = Sequence->Notifies[DraggedNotifyIndex];
		// 아이콘을 그릴 때와 같은 프레임 폭을 써서 패널 너비가 달라도 이동량이 일치한다.
		const float FrameDelta = (ImGui::GetMousePos().x - NotifyDragStartX) / ImGui::NeoGetPixelsPerFrame();
		const float NewTime = std::round(NotifyDragStartTime * FrameRate + FrameDelta) / FrameRate;
		const float MaxTime = FMath::Max(0.0f, Sequence->GetPlayLength() - Event.GetDuration());
		const float ClampedTime = FMath::Clamp(NewTime, 0.0f, MaxTime);
		if (Event.GetTime() != ClampedTime)
		{
			PrepareNotifyEdit();
			SelectedNotifyIndex = DraggedNotifyIndex;
			Event.SetTime(ClampedTime);
			RefreshNotifyOffsets();
		}
	}

	// 표시용 프레임 배열. 드래그 상태는 배열 주소 대신 이벤트 index로 보관한다.
	TArray<int32> NotifyFrames;
	for (const FAnimNotifyEvent& Notify : Sequence->Notifies)
	{
		NotifyFrames.Add(TimeToFrame(Notify.GetTime(), FrameRate));
	}
	for (int32 i = 0; i < NotifyFrames.Num(); ++i)
	{
		const FAnimNotifyEvent& Event = Sequence->Notifies[i];
		if (Event.NotifyStateClass)
		{
			ImGui::NeoDrawTimelineRect(NotifyFrames[i], TimeToFrame(Event.GetTime() + Event.GetDuration(), FrameRate), ImVec4(0.3f, 0.7f, 0.9f, 0.3f));
		}
		ImGui::NeoKeyframe(&NotifyFrames[i]);
		if (ImGui::IsNeoKeyframeHovered())
		{
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				SelectedNotifyIndex = i;
				if (bCanEdit)
				{
					DraggedNotifyIndex = i;
					NotifyDragStartX = ImGui::GetMousePos().x;
					NotifyDragStartTime = Event.GetTime();
				}
			}
			const FAnimNotifyEvent& Notify = Sequence->Notifies[i];
			ImGui::SetTooltip("%s (%.2f s)", Notify.NotifyName.ToString().c_str(), Notify.GetTime());
		}
	}
	ImGui::EndNeoTimeLine();
}

// 배열 재배치 전에 프리뷰가 보관한 Event 참조와 활성 State를 정리한다.
void FAnimationEditor::PrepareNotifyEdit()
{
	if (UAnimSingleNodeInstance* Instance = GetPreviewInstance())
	{
		Instance->SetPlaying(false);
		Instance->EndNotifyStates();
	}
	bNotifyDirty = true;
	SaveMessage.clear();
}

bool FAnimationEditor::SaveNotifies()
{
	FString Error;
	if (!FFbxAssetLoader::SaveAnimationAsset(Sequence, Error))
	{
		SaveMessage = "Save failed: " + Error;
		return false;
	}
	bNotifyDirty = false;
	SaveMessage = "Saved";
	return true;
}

bool FAnimationEditor::DiscardNotifyChanges()
{
	// AssetManager가 같은 Sequence를 보관하므로 창만 닫지 않고 디스크의 Notify를 복원한다.
	FString Path, Error;
	FFbxAssetData Data;
	if (!FbxPath::ResolveKey(Sequence->GetPath(), Path, Error) || !FFbxAssetBake::ReadBaked(Path, Data, Error))
	{
		SaveMessage = "Discard failed: " + Error;
		return false;
	}
	if (Data.Type != EFbxAssetType::AnimSequence)
	{
		SaveMessage = "Discard failed: Asset is not an Animation Sequence";
		return false;
	}
	try
	{
		json NotifyData = json::parse(Data.Animation.NotifyData);
		PrepareNotifyEdit();
		Sequence->Serialize(NotifyData, true);
	}
	catch (const std::exception& Exception)
	{
		SaveMessage = "Discard failed: " + FString(Exception.what());
		return false;
	}
	SelectedNotifyIndex = -1;
	DraggedNotifyIndex = -1;
	bNotifyDirty = false;
	SaveMessage.clear();
	return true;
}

void FAnimationEditor::RefreshNotifyOffsets()
{
	FAnimNotifyEvent& Event = Sequence->Notifies[SelectedNotifyIndex];
	Event.TriggerTimeOffset = GetTriggerTimeOffsetForType(Sequence->CalculateOffsetForNotify(Event.GetTime()));
	Event.EndTriggerTimeOffset = GetTriggerTimeOffsetForType(Sequence->CalculateOffsetForNotify(Event.GetTime() + Event.GetDuration()));
}

void FAnimationEditor::CreateNewNotify(UClass* NotifyClass)
{
	DraggedNotifyIndex = -1;
	PrepareNotifyEdit();
	FAnimNotifyEvent Event;
	Event.NotifyName = FName(NotifyClass ? NotifyClass->Name : "NewNotify");
	if (NotifyClass)
	{
		UObject* Object = Sequence->CreateNotifyObject(NotifyClass);
		Event.Notify = Cast<UAnimNotify>(Object);
		Event.NotifyStateClass = Cast<UAnimNotifyState>(Object);
	}
	const float FrameRate = Sequence->DataModel ? Sequence->DataModel->FrameRate : 30.0f;
	const float FrameDuration = FrameRate > 0.0f ? 1.0f / FrameRate : 1.0f / 30.0f;
	const float Duration = Event.NotifyStateClass ? FMath::Min(FrameDuration, Sequence->GetPlayLength()) : 0.0f;
	Event.SetDuration(Duration);
	const UAnimSingleNodeInstance* Instance = GetPreviewInstance();
	Event.SetTime(FMath::Clamp(Instance ? Instance->GetCurrentTime() : 0.0f, 0.0f, Sequence->GetPlayLength() - Duration));
	Sequence->Notifies.Add(Event);
	SelectedNotifyIndex = Sequence->Notifies.Num() - 1;
	RefreshNotifyOffsets();
}

void FAnimationEditor::SetNotifyDemoEnabled(bool bEnabled)
{
	UAnimSingleNodeInstance* Instance = GetPreviewInstance();
	const float Time = Instance->GetCurrentTime();
	const float Rate = Instance->GetPlayRate();
	const bool bPlaying = Instance->IsPlaying();
	const bool bLooping = Instance->IsLooping();
	const bool bReverse = Instance->IsReverse();
	USkeletalMeshComponent* Component = PreviewScene->GetPreviewComponent();
	if (bEnabled)
	{
		Component->SetAnimInstanceClass(UAnimNotifyDemoInstance::StaticClass());
	}
	else
	{
		Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	}
	Instance = Component->GetSingleNodeInstance();
	Instance->SetAnimationAsset(Sequence, bLooping, Rate);
	Instance->SetPosition(Time, false);
	Instance->SetReverse(bReverse);
	Instance->SetPlaying(bPlaying);
}

void FAnimationEditor::DrawNotifyEditor()
{
	const UEditorEngine* Editor = Cast<UEditorEngine>(GEngine);
	const bool bPlayingPIE = Editor && Editor->IsPlaying();
	// PIE는 같은 Sequence의 Event 주소를 참조한다. 실행 중에는 배치를 수정하지 않는다.
	if (bPlayingPIE)
	{
		ImGui::TextWrapped("Stop PIE to edit notifies.");
	}
	ImGui::BeginDisabled(bPlayingPIE);
	UAnimSingleNodeInstance* PreviewInstance = GetPreviewInstance();
	bool bNotifyDemo = Cast<UAnimNotifyDemoInstance>(PreviewInstance) != nullptr;
	ImGui::BeginDisabled(PreviewInstance == nullptr);
	if (ImGui::Checkbox("Named Notify Demo", &bNotifyDemo))
	{
		SetNotifyDemoEnabled(bNotifyDemo);
	}
	ImGui::EndDisabled();
	if (ImGui::Button("Add Notify"))
	{
		ImGui::OpenPopup("AddNotify");
	}
	if (ImGui::BeginPopup("AddNotify"))
	{
		if (ImGui::MenuItem("Named Notify"))
		{
			CreateNewNotify(nullptr);
		}
		const auto DrawClasses = [&](const char* Label, UClass* BaseClass)
		{
			if (ImGui::BeginMenu(Label))
			{
				TArray<UClass*> Classes;
				GetDerivedClasses(BaseClass, Classes);
				for (UClass* Class : Classes)
				{
					if (Class->Constructor && ImGui::MenuItem(Class->Name.c_str()))
					{
						CreateNewNotify(Class);
					}
				}
				if (Classes.IsEmpty())
				{
					ImGui::TextDisabled("No registered classes");
				}
				ImGui::EndMenu();
			}
		};
		DrawClasses("Notify", UAnimNotify::StaticClass());
		DrawClasses("Notify State", UAnimNotifyState::StaticClass());
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Save"))
	{
		SaveNotifies();
	}
	ImGui::SameLine();
	ImGui::TextUnformatted(bNotifyDirty ? "Unsaved changes" : "");
	if (!SaveMessage.empty())
	{
		ImGui::TextWrapped("%s", SaveMessage.c_str());
	}

	ImGui::Separator();
	if (ImGui::BeginChild("NotifyList", ImVec2(0.0f, EditorUIScale::Px(140.0f)), ImGuiChildFlags_Borders))
	{
		for (int32 Index = 0; Index < Sequence->Notifies.Num(); ++Index)
		{
			const FAnimNotifyEvent& Event = Sequence->Notifies[Index];
			ImGui::PushID(Index);
			const FString Label = std::format("{}  {:.3f}s{}", Event.NotifyName.ToString(), Event.GetTime(), Event.NotifyStateClass ? " [State]" : "");
			if (ImGui::Selectable(Label.c_str(), SelectedNotifyIndex == Index))
			{
				SelectedNotifyIndex = Index;
			}
			ImGui::PopID();
		}
	}
	ImGui::EndChild();

	if (Sequence->Notifies.IsValidIndex(SelectedNotifyIndex))
	{
		FAnimNotifyEvent& Event = Sequence->Notifies[SelectedNotifyIndex];
		char Name[256];
		strncpy_s(Name, Event.NotifyName.ToString().c_str(), _TRUNCATE);
		if (ImGui::InputText("Name", Name, sizeof(Name)))
		{
			PrepareNotifyEdit();
			Event.NotifyName = FName(Name);
		}
		float Time = Event.GetTime();
		float Duration = Event.GetDuration();
		bool bTimingChanged = ImGui::DragFloat("Time (s)", &Time, 0.01f, 0.0f, Sequence->GetPlayLength(), "%.3f", ImGuiSliderFlags_AlwaysClamp);
		if (Event.NotifyStateClass)
		{
			bTimingChanged |= ImGui::DragFloat("Duration (s)", &Duration, 0.01f, 0.0f, Sequence->GetPlayLength(), "%.3f", ImGuiSliderFlags_AlwaysClamp);
		}
		if (bTimingChanged)
		{
			PrepareNotifyEdit();
			Time = FMath::Clamp(Time, 0.0f, Sequence->GetPlayLength());
			Event.SetTime(Time);
			Event.SetDuration(FMath::Clamp(Duration, 0.0f, Sequence->GetPlayLength() - Time));
			RefreshNotifyOffsets();
		}
		if (Event.GetTime() + Event.GetDuration() > Sequence->GetPlayLength())
		{
			ImGui::TextWrapped("Outside clip length. Adjust Time / Duration to move inside.");
		}
		float Weight = Event.TriggerWeightThreshold;
		if (ImGui::DragFloat("Trigger Weight", &Weight, 0.01f, 0.0f, 1.0f, "%.5f", ImGuiSliderFlags_AlwaysClamp))
		{
			PrepareNotifyEdit();
			Event.TriggerWeightThreshold = Weight;
		}
		UObject* NotifyObject = Event.Notify ? static_cast<UObject*>(Event.Notify) : Event.NotifyStateClass;
		if (NotifyObject)
		{
			ImGui::Text("Class: %s", NotifyObject->GetClass()->Name.c_str());
			if (DrawObjectProperties(NotifyObject, nullptr))
			{
				PrepareNotifyEdit();
			}
		}
		if (ImGui::Button("Delete Notify"))
		{
			PrepareNotifyEdit();
			DraggedNotifyIndex = -1;
			Sequence->Notifies.RemoveAt(SelectedNotifyIndex);
			SelectedNotifyIndex = -1;
		}
	}
	ImGui::EndDisabled();
}

void FAnimationEditor::DrawTransportControls()
{
	UAnimSingleNodeInstance* Instance = GetPreviewInstance();
	const bool bPlaying = Instance && Instance->IsPlaying();
	const bool bReverse = Instance && Instance->IsReverse();
	const bool bLooping = Instance && Instance->IsLooping();

	// 키보드 내비게이션을 끈다. 켜 두면 누른 버튼이 포커스를 받아 스페이스가 그 버튼도 눌러 재생이 두 번 바뀐다
	ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);

	// 스켈레톤이 달라 클립을 못 받았으면 버튼을 막는다
	ImGui::BeginDisabled(Instance == nullptr);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(EditorUIScale::Px(2.0f), ImGui::GetStyle().ItemSpacing.y));

	if (DrawTransportButton("##ToFront", ToFrontIcon, "|<", "To Front"))
	{
		OnClick_Backward_End();
	}
	ImGui::SameLine();
	if (DrawTransportButton("##ToPrevious", ToPreviousIcon, "<|", "To Previous"))
	{
		OnClick_Backward_Step();
	}
	// 재생 중인 방향의 버튼이 일시정지 아이콘으로 바뀐다
	ImGui::SameLine();
	const bool bPlayingReverse = bPlaying && bReverse;
	if (DrawTransportButton("##PlayReverse", bPlayingReverse ? PauseIcon : PlayReverseIcon, bPlayingReverse ? "||" : "<", bPlayingReverse ? "Pause" : "Play Reverse"))
	{
		OnClick_Backward();
	}
	ImGui::SameLine();
	const bool bPlayingForward = bPlaying && !bReverse;
	if (DrawTransportButton("##PlayForward", bPlayingForward ? PauseIcon : PlayForwardIcon, bPlayingForward ? "||" : ">", bPlayingForward ? "Pause" : "Play Forward"))
	{
		OnClick_Forward();
	}
	ImGui::SameLine();
	if (DrawTransportButton("##ToNext", ToNextIcon, "|>", "To Next"))
	{
		OnClick_Forward_Step();
	}
	ImGui::SameLine();
	if (DrawTransportButton("##ToEnd", ToEndIcon, ">|", "To End"))
	{
		OnClick_Forward_End();
	}
	ImGui::SameLine();
	if (DrawTransportButton("##Loop", bLooping ? LoopingIcon : NoLoopingIcon, bLooping ? "Loop" : "No Loop", bLooping ? "Looping" : "No Looping"))
	{
		OnClick_ToggleLoop();
	}

	ImGui::PopStyleVar();
	ImGui::EndDisabled();

	ImGui::SameLine();
	DrawPlaybackSpeedMenu();

	ImGui::PopItemFlag();
}

void FAnimationEditor::DrawPlaybackSpeedMenu()
{
	const EAnimationPlaybackSpeeds::Type Mode = PreviewScene->GetAnimationPlaybackSpeedMode();
	const float CurrentSpeed = Mode == EAnimationPlaybackSpeeds::Custom ? PreviewScene->GetCustomAnimationSpeed() : EAnimationPlaybackSpeeds::Values[Mode];
	const FString Label = FormatPlaybackSpeed(CurrentSpeed) + "##PlaybackSpeed";

	// 줄 오른쪽 끝에 붙인다
	const float ButtonWidth = ImGui::CalcTextSize("x10.0").x + ImGui::GetStyle().FramePadding.x * 2.0f;
	const float RemainingWidth = ImGui::GetContentRegionAvail().x;
	if (RemainingWidth > ButtonWidth)
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + RemainingWidth - ButtonWidth);
	}
	if (ImGui::Button(Label.c_str(), ImVec2(ButtonWidth, 0.0f)))
	{
		ImGui::OpenPopup("##PlaybackSpeedMenu");
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Playback Speed");
	}

	if (ImGui::BeginPopup("##PlaybackSpeedMenu"))
	{
		for (int32 i = 0; i < EAnimationPlaybackSpeeds::Custom; ++i)
		{
			const auto ItemMode = static_cast<EAnimationPlaybackSpeeds::Type>(i);
			const FString ItemLabel = FormatPlaybackSpeed(EAnimationPlaybackSpeeds::Values[i]);
			if (ImGui::MenuItem(ItemLabel.c_str(), nullptr, Mode == ItemMode))
			{
				PreviewScene->SetAnimationPlaybackSpeedMode(ItemMode);
			}
		}

		// 언리얼 xCustom. 값을 고치면 바로 Custom 모드가 된다 (프리뷰 월드 TimeScale이 0 ~ 10배라 그 안에서)
		ImGui::Separator();
		if (ImGui::MenuItem("Custom", nullptr, Mode == EAnimationPlaybackSpeeds::Custom, true))
		{
			PreviewScene->SetAnimationPlaybackSpeedMode(EAnimationPlaybackSpeeds::Custom);
		}
		float CustomSpeed = PreviewScene->GetCustomAnimationSpeed();
		ImGui::SetNextItemWidth(EditorUIScale::Px(120.0f));
		if (ImGui::DragFloat("##CustomSpeed", &CustomSpeed, 0.01f, 0.01f, 10.0f, "x%.2f", ImGuiSliderFlags_AlwaysClamp))
		{
			PreviewScene->SetCustomAnimationSpeed(CustomSpeed);
		}
		ImGui::EndPopup();
	}
}

FString FAnimationEditor::GetDisplayInfo() const
{
	// 언리얼 뷰포트의 "Framerate: 30 fps"
	const UAnimDataModel* DataModel = Sequence->DataModel;
	const float FrameRate = DataModel ? DataModel->FrameRate : 0.0f;
	return std::format("Frame Rate: {:g} fps", FrameRate);
}

void FAnimationEditor::OnClick_Backward_End()
{
	if (UAnimSingleNodeInstance* Instance = GetPreviewInstance())
	{
		Instance->SetPlaying(false);
		Instance->SetPosition(0.0f, false);
	}
}

void FAnimationEditor::OnClick_Backward_Step()
{
	if (UAnimSingleNodeInstance* Instance = GetPreviewInstance())
	{
		Instance->SetPlaying(false);
		Instance->StepBackward();
	}
}

void FAnimationEditor::OnClick_Backward()
{
	UAnimSingleNodeInstance* Instance = GetPreviewInstance();
	if (!Instance)
	{
		return;
	}

	const bool bIsReverse = Instance->IsReverse();
	const bool bIsPlaying = Instance->IsPlaying();
	// 앞으로 재생 중이면 방향만 거꾸로 바꾼다
	if (!bIsReverse && bIsPlaying)
	{
		Instance->SetReverse(true);
	}
	// 거꾸로 재생 중이면 멈춘다
	else if (bIsPlaying)
	{
		Instance->SetPlaying(false);
	}
	// 멈춰 있으면 거꾸로 재생. 처음에 있으면 끝으로 가서 재생한다
	else
	{
		if (Instance->GetCurrentTime() <= 0.0f)
		{
			Instance->SetPosition(Sequence->GetPlayLength(), false);
		}
		Instance->SetPlaying(true);
		Instance->SetReverse(true);
	}
}

void FAnimationEditor::OnClick_Forward()
{
	UAnimSingleNodeInstance* Instance = GetPreviewInstance();
	if (!Instance)
	{
		return;
	}

	const bool bIsReverse = Instance->IsReverse();
	const bool bIsPlaying = Instance->IsPlaying();
	// 거꾸로 재생 중이면 방향만 앞으로 바꾼다
	if (bIsReverse && bIsPlaying)
	{
		Instance->SetReverse(false);
	}
	// 재생 중이면 멈춘다
	else if (bIsPlaying)
	{
		Instance->SetPlaying(false);
	}
	// 멈춰 있으면 앞으로 재생. 끝에 있으면 처음으로 돌아가서 재생한다
	else
	{
		if (Instance->GetCurrentTime() >= Sequence->GetPlayLength())
		{
			Instance->SetPosition(0.0f, false);
		}
		Instance->SetReverse(false);
		Instance->SetPlaying(true);
	}
}

void FAnimationEditor::OnClick_Forward_Step()
{
	if (UAnimSingleNodeInstance* Instance = GetPreviewInstance())
	{
		Instance->SetPlaying(false);
		Instance->StepForward();
	}
}

void FAnimationEditor::OnClick_Forward_End()
{
	if (UAnimSingleNodeInstance* Instance = GetPreviewInstance())
	{
		Instance->SetPlaying(false);
		Instance->SetPosition(Instance->GetLength(), false);
	}
}

void FAnimationEditor::OnClick_ToggleLoop()
{
	if (UAnimSingleNodeInstance* Instance = GetPreviewInstance())
	{
		Instance->SetLooping(!Instance->IsLooping());
	}
}

void FAnimationEditor::HandleScrubPositionChanged(float NewTime)
{
	if (UAnimSingleNodeInstance* Instance = GetPreviewInstance())
	{
		if (Instance->IsPlaying())
		{
			Instance->SetPlaying(false);
		}
		Instance->SetPosition(NewTime);
	}
}
