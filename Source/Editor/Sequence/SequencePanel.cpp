#include "EnginePCH.h"
#include "SequencePanel.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Sequence/SequenceActor.h"
#include <type_traits>
#include <cmath>

namespace
{
void AlignCameraRotation(FCameraSection& Section)
{
	if (Section.KeyCount < 2)
	{
		return;
	}
	for (int Axis = 0; Axis < 3; ++Axis)
	{
		float Start = Section.KeyCam[0].Rotation[Axis];
		float Delta = std::fmod(Section.KeyCam[1].Rotation[Axis] - Start, 360.0f);
		if (Delta > 180.0f)
		{
			Delta -= 360.0f;
		}
		else if (Delta < -180.0f)
		{
			Delta += 360.0f;
		}
		Section.KeyCam[1].Rotation[Axis] = Start + Delta;
	}
}
}

bool FSequencePanel::IsFadeLayoutValid(const FFadeTimelineTrack& Track)
{
	// 키가 하나인 Section은 한 시점으로, 두 개인 Section은 두 프레임 사이의 구간으로 검사한다.
	const auto& Sections = Track.FadeSections;
	for (size_t Index = 0; Index < Sections.size(); ++Index)
	{
		const auto& Section = Sections[Index];
		if (Section.KeyCount == 0)
		{
			continue;
		}
		const int32 First = Section.KeyFrames[0];
		const int32 Second = Section.KeyCount == 2 ? Section.KeyFrames[1] : First;
		if (Section.KeyCount == 2 && First == Second)
		{
			return false;
		}
		const int32 From = FMath::Min(First, Second);
		const int32 To = FMath::Max(First, Second);
		for (size_t OtherIndex = 0; OtherIndex < Index; ++OtherIndex)
		{
			const auto& Other = Sections[OtherIndex];
			if (Other.KeyCount == 0)
			{
				continue;
			}
			const int32 OtherFirst = Other.KeyFrames[0];
			const int32 OtherSecond = Other.KeyCount == 2 ? Other.KeyFrames[1] : OtherFirst;
			// 경계를 맞대는 것은 허용하고, 구간 내부의 겹침은 금지한다.
			if (From < FMath::Max(OtherFirst, OtherSecond) && FMath::Min(OtherFirst, OtherSecond) < To)
			{
				return false;
			}
		}
	}
	return true;
}

bool FSequencePanel::Init()
{
	return true;
}

void FSequencePanel::Tick(float DeltaTime)
{
	// EditorEngine이 제공하는 편집 대상 Actor의 Player를 진행시킨다.
	// 게임 월드의 Player는 SequenceActor::Tick에서 갱신하므로 패널은 에디터 Actor만 받는다.
	if (ASequenceActor* Actor = SequenceActorProvider ? SequenceActorProvider() : nullptr)
	{
		Actor->GetSequencePlayer().Tick(DeltaTime);
	}
}

void FSequencePanel::OnRender()
{
	// 패널 자체에는 연출 데이터를 보관하지 않는다. 매번 선택된 Actor를 조회해 편집한다.
	ASequenceActor* Actor = SequenceActorProvider ? SequenceActorProvider() : nullptr;
	if (!Actor)
	{
		EditedActorUUID = 0;
		bool bOpen = IsOpen();
		if (!bOpen)
		{
			return;
		}
		ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(900.0f), EditorUIScale::Px(300.0f)), ImGuiCond_FirstUseEver);
		if (ImGui::Begin(GetPanelName(), &bOpen))
		{
			ImGui::TextWrapped("%s", "Select a Sequence Actor in the Outliner to edit its sequence.");
			ImGui::BeginDisabled(!CreateSequenceActorCallback);
			if (ImGui::Button("Add Sequence Actor") && CreateSequenceActorCallback)
			{
				CreateSequenceActorCallback();
			}
			ImGui::EndDisabled();
		}
		ImGui::End();
		SetOpen(bOpen);
		return;
	}
	FSequencePlayer& PreviewPlayer = Actor->GetSequencePlayer();
	// 다른 Actor를 선택하면 이전 Actor에서 쓰던 입력값과 키 선택 상태를 초기화한다.
	if (EditedActorUUID != Actor->GetUUID())
	{
		EditedActorUUID = Actor->GetUUID();
		SelectedTrack = 0;
		NewFadeKeyAlpha = 0.0f;
		bFadeValueEdited = false;
		bResetSequenceSelection = true;
	}
	// 참조로 가져오므로 아래에서 수정한 Section/키 값은 Actor의 데이터에 바로 반영된다.
	// 이 데이터는 나중에 씬 저장 시 SequenceActor::Serialize를 통해 함께 저장된다.
	auto& Sequence = PreviewPlayer.GetSequenceData();
	auto& Rows = Sequence.Rows;
	// UI에서 잠시 편집할 재생 설정/현재 프레임의 복사본. 실제 Player 반영은 아래에서 처리한다.
	int32 StartFrame = Sequence.StartFrame;
	int32 EndFrame = Sequence.GetEndFrame();
	int PlaybackFPS = Sequence.PlaybackFPS;
	int32 EndFrameSecond = Sequence.EndFrameSecond;
	float PlaybackFrame = PreviewPlayer.GetPlaybackFrame();
	bool bOpen = IsOpen();
	if (!bOpen)
	{
		return;
	}

	// Fade 입력란은 기본적으로 현재 프레임의 보간값을 표시한다.
	// 사용자가 직접 입력한 값은 같은 프레임에서 유지해 Add Key로 기록할 수 있게 한다.
	// 재생하거나 프레임을 옮기면 직접 입력 상태를 해제하고 다시 계산값을 표시한다.
	if (PreviewPlayer.IsPlaying() || PlaybackFrame != LastFadeValueFrame)
	{
		bFadeValueEdited = false;
	}
	if (!bFadeValueEdited)
	{
		PreviewPlayer.GetCurrentFadeAlpha(NewFadeKeyAlpha);
	}
	LastFadeValueFrame = PlaybackFrame;

	ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(900.0f), EditorUIScale::Px(300.0f)), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(GetPanelName(), &bOpen))
	{
		ImGui::Text("Sequence: %s", Actor->GetName().c_str());
		if (ImGui::Button(PreviewPlayer.IsPlaying() ? "Pause" : "Play")) //	Play, Pause를 통해 타임라인 재생
		{
			if (PreviewPlayer.IsPlaying())
			{
				PreviewPlayer.Pause();
			}
			else
			{
				PreviewPlayer.Play();
			}
			PlaybackFrame = PreviewPlayer.GetPlaybackFrame();
		}
		EditorUIScale::SameLineIfFits(EditorUIScale::Px(100.0f) + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize("FPS").x);

		static constexpr int FPSValues[]{ 30, 45, 60, 90, 144, 240, 480 };
		static const char* FPSLabels[]{ "30", "45", "60", "90", "144", "240", "480" };
		int FPSIndex = 0;
		for (int Index = 0; Index < IM_ARRAYSIZE(FPSValues); ++Index) // 현재 적용한 FPS 값으로 FPSIndex 초기화
		{
			if (FPSValues[Index] == PlaybackFPS)
			{
				FPSIndex = Index;
				break;
			}
		}
		ImGui::SetNextItemWidth(EditorUIScale::Px(100.0f));
		bool bRangeChanged = ImGui::Combo("FPS", &FPSIndex, FPSLabels, IM_ARRAYSIZE(FPSLabels));
		if (bRangeChanged) // ComboBox에 따른 FPSIndex 세팅
		{
			PlaybackFPS = FPSValues[FPSIndex]; // FPSIndex 기반으로 값 호출
		}

		EditorUIScale::SameLineIfFits(ImGui::CalcTextSize("Frame: 00000.00").x);
		ImGui::Text("Frame: %.2f", PlaybackFrame);

		if (Rows.empty())
		{
			ImGui::TextDisabled("No sequence tracks");
			ImGui::End();
			SetOpen(bOpen);
			return;
		}
		SelectedTrack = FMath::Clamp(SelectedTrack, 0, static_cast<int>(Rows.size()) - 1);
		ImGui::SetNextItemWidth(EditorUIScale::Px(140.0f));
		if (ImGui::BeginCombo("Track", Rows[SelectedTrack].Name.c_str()))
		{
			for (int Index = 0; Index < static_cast<int>(Rows.size()); ++Index)
			{
				if (ImGui::Selectable(Rows[Index].Name.c_str(), SelectedTrack == Index))
				{
					SelectedTrack = Index;
				}
			}
			ImGui::EndCombo();
		}
		FTimeline& SelectedRow = Rows[SelectedTrack];
		// Row.Rows는 카메라/Fade 중 하나를 담는 variant다. 선택한 종류에 해당하는 포인터만 얻는다.
		auto* SelectedFadeTrack = std::get_if<FFadeTimelineTrack>(&SelectedRow.Rows);
		auto* SelectedCameraTrack{ std::get_if<FCameraTimelineTrack>(&SelectedRow.Rows) };
		if (SelectedCameraTrack)
		{
			EditorUIScale::SameLineIfFits(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize("Catmull-Rom").x);
			if (ImGui::Checkbox("Catmull-Rom", &SelectedCameraTrack->mUseCatmullRom))
			{
				PreviewPlayer.Pause();
			}
		}
		CameraKey CameraSnapshot{};
		// 카메라 키에 기록할 현재 뷰포트의 위치/회전/FOV를 EditorEngine의 콜백으로 읽는다.
		// 아직 저장된 키를 바꾸지는 않고, Add Key가 요청되면 이 스냅샷을 복사한다.
		const bool bHasCameraSnapshot = SelectedCameraTrack && ViewportCameraProvider &&
			ViewportCameraProvider(CameraSnapshot);
		auto WithSections = [](FTimeline& Row, auto&& Action)
		{
			// 카메라와 Fade의 Section 컨테이너 이름이 달라 공통 작업을 람다로 전달한다.
			if (auto* CameraRow = std::get_if<FCameraTimelineTrack>(&Row.Rows))
			{
				Action(CameraRow->CamSections);
			}
			else if (auto* FadeRow = std::get_if<FFadeTimelineTrack>(&Row.Rows))
			{
				Action(FadeRow->FadeSections);
			}
		};
		// 재생 프레임은 실수지만 기록할 키는 정수 프레임이다. 소수점은 버리고 범위를 제한한다.
		const int32 KeyFrame = FMath::Clamp(static_cast<int32>(PlaybackFrame), StartFrame, EndFrame);
		bool bCanAddKey = false;
		bool bHasSections = false;
		int TotalKeys = 0;
		WithSections(SelectedRow, [&](auto& Sections)
			{
			// 마지막 Section에 키가 두 개면 다음 Section을 만들 수 있다.
			// 키가 하나라면 첫 키와 다른 프레임일 때만 두 번째 키를 추가할 수 있다.
			bHasSections = !Sections.empty();
			bCanAddKey = Sections.empty() || Sections.back().KeyCount == 2 ||
				Sections.back().KeyFrames[0] != KeyFrame;
			for (const auto& Section : Sections){
				TotalKeys += Section.KeyCount;
} });
		if (SelectedCameraTrack && !bHasCameraSnapshot)
		{
			bCanAddKey = false;
		}
		bool bFadeOverlap = false;
		if (SelectedFadeTrack && bCanAddKey)
		{
			// 원본을 바꾸기 전에 복사본에 키를 넣어 Fade 구간 겹침 여부를 미리 검사한다.
			// 여기서는 추가 가능 여부만 판단하고, 실제 Alpha 기록은 아래 Add Key 처리에서 한다.
			auto Candidate = *SelectedFadeTrack;
			if (Candidate.FadeSections.empty() || Candidate.FadeSections.back().KeyCount == 2)
			{
				Candidate.FadeSections.emplace_back();
			}
			auto& Section = Candidate.FadeSections.back();
			Section.KeyFrames[Section.KeyCount++] = KeyFrame;
			bFadeOverlap = !IsFadeLayoutValid(Candidate);
			bCanAddKey = !bFadeOverlap;
		}
		EditorUIScale::SameLineIfFits(EditorUIScale::ButtonWidth("Add Key"));
		ImGui::BeginDisabled(!bCanAddKey);
		// 버튼은 요청만 기록한다. 컨테이너 변경은 타임라인 진입 후 안전한 시점에 처리한다.
		const bool bAddKeyRequested = ImGui::Button("Add Key");
		ImGui::EndDisabled();
		EditorUIScale::SameLineIfFits(EditorUIScale::ButtonWidth("Clear Keys"));
		ImGui::BeginDisabled(!bHasSections);
		const bool bClearKeysRequested = ImGui::Button("Clear Keys");
		ImGui::EndDisabled();
		EditorUIScale::SameLineIfFits(ImGui::CalcTextSize("Keys: 0000").x);
		ImGui::Text("Keys: %d", TotalKeys);
		if (bFadeOverlap)
		{
			ImGui::TextDisabled("Fade sections cannot overlap");
		}
		if (SelectedCameraTrack && !bHasCameraSnapshot)
		{
			ImGui::TextDisabled("No active viewport camera");
		}

		const ImVec2 BodyAvailable = ImGui::GetContentRegionAvail();
		const float FooterY = ImGui::GetCursorPosY() + ImMax(0.0f, BodyAvailable.y - ImGui::GetFrameHeight());
		const float TimelineHeight = BodyAvailable.y - ImGui::GetFrameHeightWithSpacing();
		if (BodyAvailable.x > EditorUIScale::Px(240.0f) && TimelineHeight > EditorUIScale::Px(80.0f))
		{
			if (ImGui::BeginChild("SequenceTimelineArea", ImVec2(0.0f, TimelineHeight)))
			{
				int32 DisplayFrame = static_cast<int32>(PlaybackFrame);
				const int32 PreviousDisplayFrame = DisplayFrame;
				const ImGuiNeoSequencerFlags Flags = ImGuiNeoSequencerFlags_EnableSelection |
					ImGuiNeoSequencerFlags_Selection_EnableDragging;
				if (ImGui::BeginNeoSequencer("SequenceTimeline", &DisplayFrame, &StartFrame, &EndFrame,
						ImGui::GetContentRegionAvail(), Flags))
				{
					if (bResetSequenceSelection)
					{
						// 다른 Actor의 키를 이전 선택으로 취급하지 않도록 라이브러리의 선택 상태도 지운다.
						ImGui::NeoClearSelection();
						bResetSequenceSelection = false;
					}
					if (!ImGui::NeoIsDraggingSelection() && (bAddKeyRequested || bClearKeysRequested))
					{
						// 키 드래그 중에는 vector를 변경하지 않는다. 추가/삭제 전 기존 키 선택도 해제한다.
						ImGui::NeoClearSelection();
						WithSections(SelectedRow, [&](auto& Sections)
							{
							if (bClearKeysRequested){
								// 현재 선택한 트랙의 Section 전체를 제거한다.
								Sections.clear();
}
							else if (bCanAddKey)
							{
								if (Sections.empty() || Sections.back().KeyCount == 2){
									Sections.emplace_back();
}
								// 첫 키는 새 Section의 0번 칸에, 다음 키는 같은 Section의 1번 칸에 저장한다.
								// 따라서 KeyFrames[Index]와 KeyCam[Index]는 항상 같은 키를 가리킨다.
								auto& Section = Sections.back();
								const int KeyIndex = Section.KeyCount;
								Section.KeyFrames[KeyIndex] = KeyFrame;
								if constexpr (std::is_same_v<std::decay_t<decltype(Section)>, FCameraSection>)
								{
									// 카메라 트랙: 위에서 조회한 뷰포트 시점을 이 키의 값으로 기록한다.
									Section.KeyCam[KeyIndex] = CameraSnapshot;
								}
								else
								{
									// Fade 트랙: 트랙 옆 숫자 입력값을 첫 키/둘째 키의 Alpha에 기록한다.
									float& Alpha = KeyIndex == 0 ? Section.KeyFade.StartAlpha : Section.KeyFade.EndAlpha;
									Alpha = NewFadeKeyAlpha;
								}
								++Section.KeyCount;
								if constexpr (std::is_same_v<std::decay_t<decltype(Section)>, FCameraSection>)
								{
									if (Section.bUseShortestRotation){ AlignCameraRotation(Section);
}
								}
							} });
						PreviewPlayer.Pause();
						bFadeValueEdited = false;
					}
					for (FTimeline& Row : Rows)
					{
						auto* FadeTrack = std::get_if<FFadeTimelineTrack>(&Row.Rows);
						// 드래그 중 여러 키가 함께 이동해도 트랙 전체를 검증한 뒤 승인한다.
						const auto PreviousFadeSections = FadeTrack ? FadeTrack->FadeSections : std::vector<FFadeSection>{};
						const float PreviousFadeAlpha = NewFadeKeyAlpha;
						// Fade Row에만 숫자 입력란을 연결한다. 이 입력란은 키를 자동 생성하지 않는다.
						if (ImGui::BeginNeoTimelineEx(Row.Name.c_str(), nullptr, ImGuiNeoTimelineFlags_None,
								FadeTrack ? &NewFadeKeyAlpha : nullptr))
						{
							if (FadeTrack && NewFadeKeyAlpha != PreviousFadeAlpha)
							{
								// 직접 입력한 값은 유지하고 Fade 트랙을 선택해 다음 Add Key의 대상으로 만든다.
								bFadeValueEdited = true;
								SelectedTrack = static_cast<int>(&Row - Rows.data());
								PreviewPlayer.Pause();
							}
							WithSections(Row, [&](auto& Sections)
								{
								for (auto& Section : Sections)
								{
									ImGui::PushID(&Section);
									// 키 두 개가 있는 Section만 시작~끝 구간을 색칠한다.
									if (Section.KeyCount == 2)
									{
										const int32 From = FMath::Min(Section.KeyFrames[0], Section.KeyFrames[1]);
										const int32 To = FMath::Max(Section.KeyFrames[0], Section.KeyFrames[1]);
										ImGui::NeoDrawTimelineRect(From, To, ImVec4(Row.Color.R, Row.Color.G, Row.Color.B, Row.Color.A));
									}
									for (int Index = 0; Index < Section.KeyCount; ++Index)
									{
										const int32 PreviousKeyFrame = Section.KeyFrames[Index];
										// 포인터를 넘기므로 드래그 결과가 키의 프레임에 직접 반영된다.
										// 드래그는 키의 시간만 변경하며 카메라 값이나 Fade Alpha는 유지한다.
										ImGui::NeoKeyframe(&Section.KeyFrames[Index]);
										if (ImGui::IsNeoKeyframeRightClicked()){
											ImGui::OpenPopup("SectionCurve");
}
										if (ImGui::IsNeoKeyframeHovered())
										{
											ImGui::BeginTooltip();
											ImGui::Text("Frame: %d", Section.KeyFrames[Index]);
											if constexpr (std::is_same_v<std::decay_t<decltype(Section)>, FCameraSection>)
											{
												const CameraKey& Key = Section.KeyCam[Index];
												ImGui::Text("Location: %.2f, %.2f, %.2f", Key.Location.X, Key.Location.Y, Key.Location.Z);
												ImGui::Text("Rotation: %.2f, %.2f, %.2f", Key.Rotation.Pitch, Key.Rotation.Yaw, Key.Rotation.Roll);
												ImGui::Text("FOV: %.2f", Key.FieldOfView);
											}
											else
											{
												ImGui::Text("Alpha: %.2f", Index == 0 ? Section.KeyFade.StartAlpha : Section.KeyFade.EndAlpha);
											}
											ImGui::EndTooltip();
										}
										Section.KeyFrames[Index] = FMath::Clamp(Section.KeyFrames[Index], StartFrame, EndFrame);
										if (Section.KeyFrames[Index] != PreviousKeyFrame)
										{
											if (FadeTrack){
												bFadeValueEdited = false;
}
											PreviewPlayer.Pause();
										}
									}
									if (ImGui::BeginPopup("SectionCurve"))
									{
										// 두 키 중 어느 쪽을 우클릭해도 같은 Section의 Curve와 Exp를 편집한다.
										ImGui::TextUnformatted("Section Curve");
										ImGui::Separator();
										if constexpr (std::is_same_v<std::decay_t<decltype(Section)>, FCameraSection>)
										{
											if (ImGui::Checkbox("Shortest Rotation", &Section.bUseShortestRotation))
											{
												if (Section.bUseShortestRotation){ AlignCameraRotation(Section);
}
												PreviewPlayer.Pause();
											}
											if (ImGui::Checkbox("Camera Cut", &Section.mCutAtStart)) {
												PreviewPlayer.Pause();
											}
											ImGui::TextDisabled("Edit rotation to use the entered angles (e.g. 0 -> 720).");
											for (int KeyIndex = 0; KeyIndex < Section.KeyCount; ++KeyIndex)
											{
												ImGui::PushID(KeyIndex);
												ImGui::Separator();
												ImGui::Text("Camera Key %d | Frame %d", KeyIndex + 1, Section.KeyFrames[KeyIndex]);
												auto& Key = Section.KeyCam[KeyIndex];
												ImGui::SetNextItemWidth(EditorUIScale::Px(260.0f));
												bool bChanged = ImGui::DragFloat3("Location (X/Y/Z)", Key.Location.V, 0.1f);
												ImGui::SetNextItemWidth(EditorUIScale::Px(260.0f));
												if (ImGui::DragFloat3("Rotation (Pitch/Yaw/Roll)", Key.Rotation.V, 0.1f))
												{
													// 직접 입력한 회전량은 자동 보정으로 덮어쓰지 않는다.
													Section.bUseShortestRotation = false;
													bChanged = true;
												}
												ImGui::SetNextItemWidth(EditorUIScale::Px(120.0f));
												bChanged |= ImGui::DragFloat("FOV", &Key.FieldOfView, 0.1f, 1.0f, 179.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
												if (bChanged){ PreviewPlayer.Pause();
}
												ImGui::PopID();
											}
											ImGui::Separator();
										}
										const CurveType Types[] = { CurveType::Linear, CurveType::EaseIn, CurveType::EaseOut, CurveType::Bezier };
										const char* Labels[] = { "Linear", "Ease In", "Ease Out", "Bezier" };
										for (int CurveIndex = 0; CurveIndex < IM_ARRAYSIZE(Types); ++CurveIndex)
										{
											if (ImGui::MenuItem(Labels[CurveIndex], nullptr, Section.Curve == Types[CurveIndex]))
											{
												Section.Curve = Types[CurveIndex];
												if (FadeTrack){
													bFadeValueEdited = false;
}
												PreviewPlayer.Pause();
											}
										}
										if (Section.Curve == CurveType::EaseIn || Section.Curve == CurveType::EaseOut)
										{
											ImGui::Separator();
											ImGui::SetNextItemWidth(EditorUIScale::Px(120.0f));
											if (ImGui::DragFloat("Ease Exp", &Section.EaseExp, 0.05f, 1.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
											{
												if (FadeTrack){
													bFadeValueEdited = false;
}
												PreviewPlayer.Pause();
											}
										}
										ImGui::EndPopup();
									}
									ImGui::PopID();
								} });
							ImGui::EndNeoTimeLine();
						}
						if (FadeTrack && !IsFadeLayoutValid(*FadeTrack))
						{
							// 드래그 결과가 겹치는 구간/길이 0인 구간을 만들면 프레임만 이전 값으로 되돌린다.
							for (size_t Index = 0; Index < PreviousFadeSections.size(); ++Index)
							{
								FadeTrack->FadeSections[Index].KeyFrames[0] = PreviousFadeSections[Index].KeyFrames[0];
								FadeTrack->FadeSections[Index].KeyFrames[1] = PreviousFadeSections[Index].KeyFrames[1];
							}
							if (ImGui::NeoIsDraggingSelection())
							{
								ImGui::SetTooltip("Fade sections cannot overlap or have zero duration");
							}
						}
					}
					ImGui::EndNeoSequencer();
				}
				if (DisplayFrame != PreviousDisplayFrame)
				{
					// 타임라인 커서를 옮겼을 때 Player의 조회 기준 프레임도 변경한다. 내부에서 일시 정지한다.
					PreviewPlayer.SetPlaybackFrame(static_cast<float>(DisplayFrame));
				}
			}
			ImGui::EndChild();
		}

		ImGui::SetCursorPosY(FooterY);
		const float DurationWidth = EditorUIScale::Px(100.0f);
		const float DurationTotalWidth = DurationWidth + ImGui::GetStyle().ItemInnerSpacing.x +
			ImGui::CalcTextSize("Duration (s)").x;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
			ImMax(0.0f, ImGui::GetContentRegionAvail().x - DurationTotalWidth));
		ImGui::SetNextItemWidth(DurationWidth);
		bRangeChanged |= ImGui::InputInt("Duration (s)", &EndFrameSecond, 0, 0);
		if (bRangeChanged)
		{
			// UI 복사본의 FPS/길이를 실제 데이터에 반영하고 현재 재생 프레임 범위도 보정한다.
			PreviewPlayer.SetPlaybackSettings(PlaybackFPS, EndFrameSecond);
		}
	}
	ImGui::End();

	SetOpen(bOpen);
}
