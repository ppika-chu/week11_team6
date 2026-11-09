#pragma once

// 에디터 UI가 쓰는 파일 경로. 읽기 전용 리소스는 Resources, 실행 중 저장하는 파일은 Saved 아래에 둔다.
// 엔진 공용 경로는 Runtime/Core/ResourcePaths.h에 있다.
namespace EditorResourcePaths
{
// ImGui 한글 UI 폰트
constexpr const char* UIFont = "Resources/Fonts/HMKMRHD.TTF";

// Content Drawer가 보여주는 사용자 콘텐츠 루트와 씬 저장 기본 폴더
constexpr const char* AssetRoot = "Assets";
constexpr const char* SceneFolder = "Assets/Scenes";

constexpr const char* FolderIcon = "Resources/Icons/Folder.png";
constexpr const char* FileIcon = "Resources/Icons/File.png";

constexpr const char* ToolbarPlayIcon = "Resources/Icons/Toolbar/Play.png";
constexpr const char* ToolbarPauseIcon = "Resources/Icons/Toolbar/Pause.png";
constexpr const char* ToolbarStepIcon = "Resources/Icons/Toolbar/Step.png";
constexpr const char* ToolbarStopIcon = "Resources/Icons/Toolbar/Stop.png";

// Skeletal Mesh Editor
constexpr const char* BoneIcon = "Resources/Icons/Skeletal/Bone.png";
constexpr const char* BoneNonWeightedIcon = "Resources/Icons/Skeletal/BoneNonWeighted.png";
constexpr const char* ResetToDefaultIcon = "Resources/Icons/Skeletal/ResetToDefault.png";
constexpr const char* GizmoWorldIcon = "Resources/Icons/Gizmo/globe.png";
constexpr const char* GizmoLocalIcon = "Resources/Icons/Gizmo/transform-local.png";
// Animation Editor 재생 버튼. 언리얼 Editor/Slate/Sequencer/PlaybackControls의 SVG를 PNG로 바꾼 것
constexpr const char* AnimToFrontIcon = "Resources/Icons/Animation/PlayControlsToFront.png";
constexpr const char* AnimToPreviousIcon = "Resources/Icons/Animation/PlayControlsToPrevious.png";
constexpr const char* AnimPlayReverseIcon = "Resources/Icons/Animation/PlayControlsPlayReverse.png";
constexpr const char* AnimPlayForwardIcon = "Resources/Icons/Animation/PlayControlsPlayForward.png";
constexpr const char* AnimPauseIcon = "Resources/Icons/Animation/PlayControlsPause.png";
constexpr const char* AnimToNextIcon = "Resources/Icons/Animation/PlayControlsToNext.png";
constexpr const char* AnimToEndIcon = "Resources/Icons/Animation/PlayControlsToEnd.png";
constexpr const char* AnimLoopingIcon = "Resources/Icons/Animation/PlayControlsLooping.png";
constexpr const char* AnimNoLoopingIcon = "Resources/Icons/Animation/PlayControlsNoLooping.png";
// 프리뷰 배경. 언리얼 에셋 뷰어 기본 큐브맵(EpicQuadPanorama_CC+EV1)을 내보낸 것
constexpr const char* AssetViewerSkyPanorama = "Resources/SkySphere/AssetViewerSky.hdr";

// 실행 중 만들어지는 사용자별 설정. 저장소에 올리지 않는다(.gitignore의 /Saved/).
constexpr const char* SavedFolder = "Saved";
constexpr const char* EditorSettingsFile = "Saved/editor.ini";
constexpr const char* ImGuiLayoutFile = "Saved/imgui.ini";
}
