#include "EnginePCH.h"
#include "Editor/Settings/SettingsPanel.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Render/Renderer.h"
#include "Render/ShadowSettings.h"
#include "Engine/World.h"

// 종료 시 렌더·에디터·뷰포트 설정을 함께 저장한다.
FSettingsPanel::~FSettingsPanel()
{
	// 종료 시 다른 객체를 참조하지 않고 보존한 설정만 저장한다.
	ViewportsPanel = nullptr;
	SaveSettings();
}

// 설정 파일을 읽어 패널 상태를 초기화한다.
bool FSettingsPanel::Init()
{
	LoadSettings();
	return true;
}


// 설정 패널의 프레임 갱신 진입점이다.
void FSettingsPanel::Tick(float DeltaTime)
{
}

// ImGui 조작으로 렌더·카메라 설정과 저장·복원 요청을 처리한다.
void FSettingsPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(400.0f), EditorUIScale::Px(300.0f)), ImGuiCond_FirstUseEver);
	ImGui::Begin("Settings");
	// 안내문이 패널 끝에서 잘리지 않도록 이 창의 텍스트는 줄바꿈한다.
	ImGui::PushTextWrapPos(0.0f);

	// 입력칸 오른쪽에 가장 긴 라벨만큼 자리를 남겨 라벨이 창 밖으로 잘리지 않게 한다.
	ImGui::PushItemWidth(-(ImGui::CalcTextSize("Camera Rotate Sensitivity").x + ImGui::GetStyle().ItemInnerSpacing.x));

	//////////////////////////////////////////////////////////

	// 렌더링 옵션 (Toggles)
	ImGui::SeparatorText("Rendering");

	ImGui::TextDisabled("Show flags are configured per viewport.");
	ImGui::SliderFloat("Exposure", &Settings.Exposure, 0.1f, 5.0f, "%.2f");
	if (World)
	{
		const FVignetteSettings& Vignette = World->GetVignetteSettings();
		bool bVignetteEnabled = Vignette.bEnabled;

		if (ImGui::Checkbox("Vignette", &bVignetteEnabled))
		{
			World->SetVignetteEnabled(bVignetteEnabled);
		}

		float VignetteIntensity = Vignette.Intensity;
		ImGui::BeginDisabled(!bVignetteEnabled);
		if (ImGui::SliderFloat("Vignette Intensity", &VignetteIntensity, 0.0f, 1.0f, "%.2f"))
		{
			World->SetVignette(
				VignetteIntensity,
				Vignette.Radius,
				Vignette.Softness,
				Vignette.Color);
		}
		ImGui::EndDisabled();
	}
	ImGui::SliderFloat("Min Shadow Caster Radius", &GMinScreenRadiusForShadowCaster, 0.0f, 0.05f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::TextDisabled("0 disables small caster culling.");
	int ShadowCullMode = static_cast<int>(FShadowSettings::GetCullMode());
	if (ImGui::Combo("Shadow Cull Mode", &ShadowCullMode, "Back\0Front\0"))
	{
		FShadowSettings::SetCullMode(static_cast<EShadowCullMode>(ShadowCullMode));
	}
	ImGui::TextDisabled("Front: fewer acne, single-sided meshes cast no shadow.");
	ImGui::Combo("Light Debug", &Settings.LightDebugMode, "Off\0Cell Heatmap\0Brute Force\0Slices\0");
	ImGui::Combo("Light Culling", &Settings.LightCullingMode, "Tiled\0Clustered\0Tiled 2.5D\0");
	const char* Models[] = { "Gouraud", "Lambert", "Blinn-Phong" };
	ImGui::Combo("Lighting Model", &Settings.LightingModel, Models, IM_ARRAYSIZE(Models));
	//////////////////////////////////////////////////////////

	ImGui::Dummy(ImVec2(0.0f, SectionGap));
	ImGui::SeparatorText("Grid");

	// 언리얼처럼 그리기 방식은 투영으로 정해진다: 원근 = PS 그리드, 직교 = 배치 라인 그리드.
	ImGui::SliderInt("Grid Spacing", &Settings.GridSpacing, 1, 100);

	ImGui::SliderFloat("Grid Range", &Settings.GridRange, 1.0f, 1000.0f, "%.0f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Maximum perspective grid distance. The visible distance also shrinks as the camera nears the grid plane.\nOrthographic views always cover the whole screen.");
	}

	//////////////////////////////////////////////////////////

	// 에디터 수치 설정 (Values)
	ImGui::Dummy(ImVec2(0.0f, SectionGap));
	ImGui::SeparatorText("Editor Settings");

	// 에디터 카메라 조작은 FEditorViewportClient가 이 값을 매 프레임 읽어 쓴다.
	ImGui::SliderFloat("Camera Rotate Sensitivity", &Settings.MouseSensitivity, 0.01f, 1.0f, "%.2f");

	ImGui::SliderFloat("Camera Speed", &Settings.CameraSpeed, 0.1f, 10.0f, "%.2f");

	//////////////////////////////////////////////////////////

	ImGui::Dummy(ImVec2(0.0f, SectionGap));
	ImGui::SeparatorText("Load Settings");

	if (ImGui::Button("Load Settings"))
	{
		LoadSettings();
	}

	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Load settings from Saved/editor.ini");
	}

	ImGui::SameLine();

	if (ImGui::Button("Save Settings"))
	{
		SaveSettings();
	}

	//////////////////////////////////////////////////////////

	ImGui::PopItemWidth();
	ImGui::PopTextWrapPos();
	ImGui::End();
}

// 알려진 설정 섹션을 Saved/editor.ini에 함께 기록해 뷰포트 값 유실을 막는다.
bool FSettingsPanel::SaveSettings() const
{
	FEditorSettings Snapshot = Settings;
	ReadViewportSettings(Snapshot);
	std::error_code SavedFolderError;
	std::filesystem::create_directories(EditorResourcePaths::SavedFolder, SavedFolderError);
	std::ofstream File(EditorResourcePaths::EditorSettingsFile);

	if (!File.is_open())
	{
		HTR_LOG(Error, "Failed to open {} for saving.", EditorResourcePaths::EditorSettingsFile);
		return false;
	}

	File << "[Rendering]\n";
	File << "LightingModel=" << Settings.LightingModel << "\n";
	File << "\n";

	File << "[Editor]\n";
	File << "CameraMoveSpeed=" << Settings.CameraSpeed << "\n";
	File << "CameraRotateSensitivity=" << Settings.MouseSensitivity << "\n";
	File << "GridSpacing=" << Settings.GridSpacing << "\n";
	File << "GridRange=" << Settings.GridRange << "\n";
	File << "\n";

	File << "[MultipleViewports]\n";
	File << "Horizontal=" << Snapshot.MultipleViewportsHorizontal << "\n";
	File << "Vertical=" << Snapshot.MultipleViewportsVertical << "\n";
	File << "Layout=" << (Snapshot.bMultipleViewportsSingle ? "Single" : "Quad") << "\n";
	File << "SingleViewIndex=" << Snapshot.MultipleViewportsSingleViewIndex << "\n";

	for (int32 Index = 0; Index < 4; ++Index)
	{
		File << "View" << Index << "Fov=" << Snapshot.ViewFov[Index] << "\n";
		File << "View" << Index << "OrthoWidth=" << Snapshot.ViewOrthoWidth[Index] << "\n";
		File << "View" << Index << "Preset=" << Snapshot.ViewPreset[Index] << "\n";
		File << "View" << Index << "LocationX=" << Snapshot.ViewLocation[Index].X << "\n";
		File << "View" << Index << "LocationY=" << Snapshot.ViewLocation[Index].Y << "\n";
		File << "View" << Index << "LocationZ=" << Snapshot.ViewLocation[Index].Z << "\n";
		File << "View" << Index << "RotationX=" << Snapshot.ViewRotation[Index].X << "\n";
		File << "View" << Index << "RotationY=" << Snapshot.ViewRotation[Index].Y << "\n";
		File << "View" << Index << "RotationZ=" << Snapshot.ViewRotation[Index].Z << "\n";
		File << "View" << Index << "RotationW=" << Snapshot.ViewRotation[Index].W << "\n";
	}
	File.close();
	return true;
}

// 파일의 키를 파싱해 렌더·카메라·레이아웃 설정을 복원한다.
bool FSettingsPanel::LoadSettings()
{
	std::ifstream File(EditorResourcePaths::EditorSettingsFile);
	if (!File.is_open())
	{
		return false;
	}

	uint8 LocationComponentMasks[4]{};
	uint8 RotationComponentMasks[4]{};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Settings.bViewLocationSaved[Index] = false;
		Settings.bViewRotationSaved[Index] = false;
	}

	std::string Line;
	while (std::getline(File, Line))
	{
		if (Line.empty() || Line[0] == ';' || Line[0] == '[')
		{
			continue;
		}

		std::istringstream Iss(Line);
		std::string Key;
		if (std::getline(Iss, Key, '='))
		{
			std::string ValueStr;
			if (std::getline(Iss, ValueStr))
			{
				// 새 View 키는 NaN·잘못된 숫자·범위 밖 값을 무시한다.
				for (int32 Index = 0; Index < 4; ++Index)
				{
					FString Prefix = "View";
					Prefix += static_cast<char>('0' + Index);
					std::istringstream Number(ValueStr);
					float Value = 0;
					if (!(Number >> Value) || !std::isfinite(Value))
					{
						continue;
					}
					if (Key == Prefix + "Fov" && Value >= 1 && Value <= 179)
					{
						Settings.ViewFov[Index] = Value;
					}
					else if (Key == Prefix + "OrthoWidth" && Value >= 0.01f)
					{
						// 파일 값은 절대 상한으로 먼저 제한하고, 실제 View 비율 상한은 Adapter가 적용한다.
						Settings.ViewOrthoWidth[Index] = FMath::Clamp(Value, 0.01f, 1000000.0f);
					}
					else if (Key == Prefix + "Preset" && Value >= 0 && Value <= 7 && Value == static_cast<int32>(Value))
					{
						Settings.ViewPreset[Index] = static_cast<int32>(Value);
					}
					else if (Key == Prefix + "LocationX")
					{
						Settings.ViewLocation[Index].X = Value;
						LocationComponentMasks[Index] |= 1 << 0;
					}
					else if (Key == Prefix + "LocationY")
					{
						Settings.ViewLocation[Index].Y = Value;
						LocationComponentMasks[Index] |= 1 << 1;
					}
					else if (Key == Prefix + "LocationZ")
					{
						Settings.ViewLocation[Index].Z = Value;
						LocationComponentMasks[Index] |= 1 << 2;
					}
					else if (Key == Prefix + "RotationX")
					{
						Settings.ViewRotation[Index].X = Value;
						RotationComponentMasks[Index] |= 1 << 0;
					}
					else if (Key == Prefix + "RotationY")
					{
						Settings.ViewRotation[Index].Y = Value;
						RotationComponentMasks[Index] |= 1 << 1;
					}
					else if (Key == Prefix + "RotationZ")
					{
						Settings.ViewRotation[Index].Z = Value;
						RotationComponentMasks[Index] |= 1 << 2;
					}
					else if (Key == Prefix + "RotationW")
					{
						Settings.ViewRotation[Index].W = Value;
						RotationComponentMasks[Index] |= 1 << 3;
					}
				}
				if (Key == "Layout")
				{
					Settings.bMultipleViewportsSingle = ValueStr == "Single";
					continue;
				}

				// 손으로 고친 파일의 빈 값·글자·NaN에서 std::stoi/stof 예외로 죽지 않도록, 읽지 못한 값은 기본값을 유지한다.
				std::istringstream Number(ValueStr);
				float Value = 0;
				if (!(Number >> Value) || !std::isfinite(Value))
				{
					continue;
				}

				if (Key == "LightingModel")
				{
					Settings.LightingModel = FMath::Clamp(static_cast<int32>(Value), 0, 2);
				}
				else if (Key == "CameraMoveSpeed")
				{
					Settings.CameraSpeed = Value;
				}
				else if (Key == "CameraRotateSensitivity")
				{
					Settings.MouseSensitivity = Value;
				}
				else if (Key == "GridSpacing")
				{
					Settings.GridSpacing = std::max(1, static_cast<int32>(Value));
				}
				else if (Key == "GridRange")
				{
					Settings.GridRange = FMath::Clamp(Value, 1.0f, 1000.0f);
				}
				else if (Key == "Horizontal")
				{
					Settings.MultipleViewportsHorizontal = Value;
				}
				else if (Key == "Vertical")
				{
					Settings.MultipleViewportsVertical = Value;
				}
				else if (Key == "SingleViewIndex")
				{
					const int32 ViewIndex = static_cast<int32>(Value);
					Settings.MultipleViewportsSingleViewIndex = ViewIndex >= 0 && ViewIndex < 4 ? ViewIndex : 0;
				}
			}
		}
	}

	File.close();
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Settings.bViewLocationSaved[Index] = LocationComponentMasks[Index] == 0x7;
		if (RotationComponentMasks[Index] == 0xF)
		{
			const FQuat& Rotation = Settings.ViewRotation[Index];
			const float LengthSquared = Rotation.X * Rotation.X + Rotation.Y * Rotation.Y +
				Rotation.Z * Rotation.Z + Rotation.W * Rotation.W;
			if (LengthSquared > 1.0e-12f)
			{
				Settings.ViewRotation[Index] = Rotation.Normalized();
				Settings.bViewRotationSaved[Index] = true;
			}
		}
	}
	ApplyViewportSettings();
	return true;
}

// 초기화된 카메라에 파일 설정을 적용하고 미저장 값은 초기 설정으로 채운다.
void FSettingsPanel::SetViewportsPanel(FViewportsPanel* Value)
{
	ViewportsPanel = Value;
	ApplyViewportSettings();
	CaptureViewportSettings();
}

// Transform·투영·표시·레이아웃을 저장용 설정에 복사한다.
void FSettingsPanel::ReadViewportSettings(FEditorSettings& Out) const
{
	if (!ViewportsPanel)
	{
		return;
	}
	const FLevelViewportLayout& Layout = ViewportsPanel->GetLayout();
	Out.MultipleViewportsHorizontal = Layout.GetSplitRatio().Horizontal;
	Out.MultipleViewportsVertical = Layout.GetSplitRatio().Vertical;
	Out.bMultipleViewportsSingle = Layout.GetLayoutMode() == ELayoutMode::Single;
	Out.MultipleViewportsSingleViewIndex = Layout.GetSingleViewIndex();
	for (int32 Index = 0; Index < FViewportsPanel::ViewCount; ++Index)
	{
		const FEditorViewportClient& Client = ViewportsPanel->GetViewportClient(Index);
		const FViewCamera& Camera = Client.GetCamera();
		Out.ViewFov[Index] = Camera.Projection.FovDegrees;
		Out.ViewOrthoWidth[Index] = Camera.Projection.OrthoWidth;
		Out.ViewPreset[Index] = static_cast<int32>(Client.GetViewportType());
		Out.ViewLocation[Index] = Camera.Transform.Location;
		Out.ViewRotation[Index] = Camera.Transform.Rotation;
		Out.bViewLocationSaved[Index] = true;
		Out.bViewRotationSaved[Index] = true;
	}
}

// 종료 시 패널을 읽지 않도록 살아 있는 동안 저장용 설정을 갱신한다.
void FSettingsPanel::CaptureViewportSettings() { ReadViewportSettings(Settings); }

// 저장 프리셋·투영·표시를 슬롯별로 복원하며 구형 ini는 초기값과 공통 Wireframe을 사용한다.
void FSettingsPanel::ApplyViewportSettings()
{
	if (!ViewportsPanel)
	{
		return;
	}
	FLevelViewportLayout& Layout = ViewportsPanel->GetLayout();
	Layout.SetSplitRatio({ Settings.MultipleViewportsHorizontal, Settings.MultipleViewportsVertical });
	Layout.SetSingleViewIndex(Settings.MultipleViewportsSingleViewIndex);
	Layout.SetLayoutMode(Settings.bMultipleViewportsSingle ? ELayoutMode::Single : ELayoutMode::QuadSplit);
	for (int32 Index = 0; Index < FViewportsPanel::ViewCount; ++Index)
	{
		FEditorViewportClient& Client = ViewportsPanel->GetViewportClient(Index);
		if (Settings.ViewPreset[Index] >= 0 && Settings.ViewPreset[Index] <= 7)
		{
			Client.SetViewportType(static_cast<ELevelViewportType>(Settings.ViewPreset[Index]));
		}
		FViewCamera Camera = Client.GetCamera();
		if (Settings.ViewFov[Index] > 0)
		{
			Camera.Projection.FovDegrees = Settings.ViewFov[Index];
		}
		if (Settings.ViewOrthoWidth[Index] > 0)
		{
			Camera.Projection.OrthoWidth = Settings.ViewOrthoWidth[Index];
		}
		if (Settings.bViewLocationSaved[Index])
		{
			Camera.Transform.Location = Settings.ViewLocation[Index];
		}
		if (Settings.bViewRotationSaved[Index])
		{
			Camera.Transform.Rotation = Settings.ViewRotation[Index];
		}
		Client.ApplyCameraProperties(Camera, false);
	}
}
