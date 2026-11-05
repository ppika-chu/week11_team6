local Runtime = { mManager = false, mShakes = {}, mLastPreset = "Ready", mStatus = "Press 1-9 to play a shake" }

local DANGER_VIGNETTE = {
    NearDistance = 3.5,
    FarDistance = 16.0,
    MaxIntensity = 0.6,
    SafeRadius = 0.85,
    DangerRadius = 0.65,
    Softness = 0.2,
    InterpSpeed = 5.0,
    Color = FVector.new(0.3, 0, 0),
}

local Presets = {
    {
        Name = "Wave / Horizontal sway",
        Pattern = { Type = "Wave", Duration = 2.5, BlendInTime = 0.25, BlendOutTime = 0.6, Y = { Amplitude = 0.25, Frequency = 2.0, InitialOffsetType = EInitialWaveOscillatorOffsetType.Zero } }
    },
    {
        Name = "Wave / Vertical bounce",
        Pattern = { Type = "Wave", Duration = 2.5, BlendInTime = 0.2, BlendOutTime = 0.6, Z = { Amplitude = 0.18, Frequency = 3.0, InitialOffsetType = EInitialWaveOscillatorOffsetType.Zero } }
    },
    {
        Name = "Wave / Recoil",
        Pattern = { Type = "Wave", Duration = 0.45, BlendInTime = 0.02, BlendOutTime = 0.35, X = { Amplitude = 0.12, Frequency = 8.0 }, Pitch = { Amplitude = 3.0, Frequency = 7.0, InitialOffsetType = EInitialWaveOscillatorOffsetType.Zero } }
    },
    {
        Name = "Wave / Roll",
        Pattern = { Type = "Wave", Duration = 3.0, BlendInTime = 0.4, BlendOutTime = 0.8, Roll = { Amplitude = 5.0, Frequency = 1.5, InitialOffsetType = EInitialWaveOscillatorOffsetType.Zero } }
    },
    {
        Name = "Wave / FOV pulse",
        Pattern = { Type = "Wave", Duration = 3.0, BlendInTime = 0.3, BlendOutTime = 0.8, FOV = { Amplitude = 8.0, Frequency = 1.8, InitialOffsetType = EInitialWaveOscillatorOffsetType.Zero } }
    },
    {
        Name = "Perlin / Handheld",
        Pattern = { Type = "Perlin", Duration = 4.0, BlendInTime = 0.6, BlendOutTime = 0.8, Y = { Amplitude = 0.12, Frequency = 2.0 }, Z = { Amplitude = 0.1, Frequency = 2.4 }, Pitch = { Amplitude = 1.8, Frequency = 1.8 }, Yaw = { Amplitude = 1.5, Frequency = 2.2 }, Roll = { Amplitude = 1.0, Frequency = 1.5 } }
    },
    {
        Name = "Perlin / Explosion",
        Pattern = { Type = "Perlin", Duration = 1.2, BlendInTime = 0.02, BlendOutTime = 1.1, X = { Amplitude = 0.25, Frequency = 20.0 }, Y = { Amplitude = 0.3, Frequency = 23.0 }, Z = { Amplitude = 0.25, Frequency = 25.0 }, Pitch = { Amplitude = 5.0, Frequency = 22.0 }, Yaw = { Amplitude = 4.0, Frequency = 19.0 }, Roll = { Amplitude = 3.0, Frequency = 17.0 }, FOV = { Amplitude = 3.0, Frequency = 12.0 } }
    },
    {
        Name = "Composite / Earthquake",
        PlaySpace = ECameraShakePlaySpace.World,
        Pattern = {
            Type = "Composite",
            Children = {
                { Type = "Wave", Duration = 4.0, BlendInTime = 0.6, BlendOutTime = 1.0, Y = { Amplitude = 0.2, Frequency = 1.6 }, Z = { Amplitude = 0.15, Frequency = 2.3 }, Roll = { Amplitude = 2.0, Frequency = 1.2 } },
                { Type = "Perlin", Duration = 4.0, BlendInTime = 0.3, BlendOutTime = 1.2, X = { Amplitude = 0.1, Frequency = 16.0 }, Z = { Amplitude = 0.12, Frequency = 20.0 }, Pitch = { Amplitude = 2.5, Frequency = 13.0 }, Yaw = { Amplitude = 2.0, Frequency = 15.0 } }
            }
        }
    },
    {
        Name = "Composite / Engine vibration (loop)",
        Pattern = {
            Type = "Composite",
            Children = {
                { Type = "Wave", Duration = 0.0, BlendInTime = 0.5, BlendOutTime = 0.8, Z = { Amplitude = 0.035, Frequency = 18.0 }, Roll = { Amplitude = 0.5, Frequency = 12.0 } },
                { Type = "Perlin", Duration = 0.0, BlendInTime = 0.5, BlendOutTime = 0.8, Y = { Amplitude = 0.05, Frequency = 6.0 }, Pitch = { Amplitude = 0.7, Frequency = 8.0 }, Yaw = { Amplitude = 0.6, Frequency = 7.0 } }
            }
        }
    }
}

local function StopShakes(Immediately)
    for Index = #Runtime.mShakes, 1, -1 do
        local Shake = Runtime.mShakes[Index]
        if Shake:IsValid() then
            if Immediately then
                Runtime.mManager:RemoveCameraModifier(Shake)
            else
                Shake:StopShake(false)
            end
        end
        if Immediately then
            table.remove(Runtime.mShakes, Index)
        end
    end
end

local function Initialize()
    Runtime.mManager = GetPlayerCameraManager()
    Runtime.mShakes = {}
    Runtime.mLastPreset = "Ready"
    Runtime.mStatus = "Press 1-9 to play a shake"
    if not Runtime.mManager or not Runtime.mManager:IsValid() or not Runtime.mManager:SetViewTarget(self) then
        Runtime.mStatus = "Camera manager unavailable"
        Log("[CameraShakeSample] " .. Runtime.mStatus)
        return
    end
    Log("[CameraShakeSample] 1-9: play | Shift + 1-9: layer | Space: fade out | 0: stop immediately")
end

local function PlayPreset(Index)
    if not Runtime.mManager or not Runtime.mManager:IsValid() then
        return
    end
    if not IsKeyDown(Key.Shift) then
        StopShakes(true)
    end
    local Preset = Presets[Index]
    local Shake = Runtime.mManager:AddCameraModifier("UCameraModifier_CameraShake", { Pattern = Preset.Pattern, Scale = 1.0, PlaySpace = Preset.PlaySpace or ECameraShakePlaySpace.CameraLocal })
    if Shake and Shake:IsValid() then
        table.insert(Runtime.mShakes, Shake)
        Runtime.mLastPreset = Preset.Name
        Runtime.mStatus = "Playing"
        Log("[CameraShakeSample] " .. Index .. ": " .. Preset.Name)
    else
        Runtime.mStatus = "Failed to create shake"
        Log("[CameraShakeSample] " .. Runtime.mStatus .. ": " .. Preset.Name)
    end
end

local function DrawHUD()
    local Viewport = GetViewportSize()
    local TextSize = math.max(12.0, math.min(18.0, (Viewport.Y - 32.0) / 19.0))
    local LineHeight = TextSize * 1.35
    local Anchor = FVector2.new(0.0, 0.0)
    AddUIText("CAMERA SHAKE SAMPLE", TextSize * 1.3, Anchor, Anchor, FVector2.new(16.0, 12.0))
    for Index, Preset in ipairs(Presets) do
        AddUIText(string.format("[%d] %s", Index, Preset.Name), TextSize, Anchor, Anchor, FVector2.new(16.0, 16.0 + LineHeight * (Index + 1)))
    end
    AddUIText("[Shift + 1-9] Layer shakes", TextSize, Anchor, Anchor, FVector2.new(16.0, 16.0 + LineHeight * 12.0))
    AddUIText("[Space] Fade out   [0] Stop / reset", TextSize, Anchor, Anchor, FVector2.new(16.0, 16.0 + LineHeight * 13.0))
    AddUIText(string.format("%s | Active: %d", Runtime.mStatus, #Runtime.mShakes), TextSize, Anchor, Anchor, FVector2.new(16.0, 16.0 + LineHeight * 15.0))
    AddUIText(Runtime.mLastPreset, TextSize, Anchor, Anchor, FVector2.new(16.0, 16.0 + LineHeight * 16.0))
    local Center = FVector2.new(0.5, 0.5)
    AddUIText("+", TextSize * 1.4, Center, Center, FVector2.new(0.0, 0.0))
end

function BeginPlay()
    Initialize()
end

function Tick(DeltaTime)
    for Index = #Runtime.mShakes, 1, -1 do
        if not Runtime.mShakes[Index]:IsValid() or Runtime.mShakes[Index]:IsFinished() then
            table.remove(Runtime.mShakes, Index)
        end
    end
    if #Runtime.mShakes == 0 and (Runtime.mStatus == "Playing" or Runtime.mStatus == "Fading out") then
        Runtime.mStatus = "Ready"
    end
    if IsKeyPressed(Key.Num0) or IsKeyPressed(Key.NumPad0) then
        StopShakes(true)
        Runtime.mLastPreset = "Ready"
        Runtime.mStatus = "Stopped"
    elseif IsKeyPressed(Key.Space) then
        StopShakes(false)
        Runtime.mStatus = #Runtime.mShakes > 0 and "Fading out" or "Ready"
    else
        for Index = 1, #Presets do
            if IsKeyPressed(Key["Num" .. Index]) or IsKeyPressed(Key["NumPad" .. Index]) then
                PlayPreset(Index)
		    SetVignette(
        DANGER_VIGNETTE.MaxIntensity * 0.2,
        0.3,
        DANGER_VIGNETTE.Softness,
        DANGER_VIGNETTE.Color)
            end
        end
    end
    DrawHUD()
end

function EndPlay()
    StopShakes(true)
    Runtime.mManager = false
end

function OnReload()
    EndPlay()
    Initialize()
end
