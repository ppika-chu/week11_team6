local Config = {
    SlowMotionScale = 0.2,
    HitStopDuration = 0.65,
    VignetteIntensity = 0.78,
    VignetteRadius = 0.5,
    VignetteSoftness = 0.28,
    VignetteColor = FVector.new(0.0, 0.0, 0.0),
}

local Runtime = {
    mCameraManager = false,
    mBaseTimeScale = 1.0,
    mHitStopRemaining = 0.0,
    mVignetteEnabled = false,
    mStatus = "NORMAL",
}

local function IsNumberPressed(Number)
    return IsKeyPressed(Key["Num" .. Number])
        or IsKeyPressed(Key["NumPad" .. Number])
end

local function ApplyBaseTimeScale(Scale, Status)
    Runtime.mBaseTimeScale = Scale
    Runtime.mStatus = Status

    if Runtime.mHitStopRemaining <= 0.0 then
        SetGlobalTimeDilation(Scale)
    end
end

local function StartHitStop()
    Runtime.mHitStopRemaining = Config.HitStopDuration
    Runtime.mStatus = "HIT STOP"
    SetGlobalTimeDilation(0.0)
end

local function ToggleVignette()
    Runtime.mVignetteEnabled = not Runtime.mVignetteEnabled
    SetVignetteEnabled(Runtime.mVignetteEnabled)
end

local function ResetPresentation()
    Runtime.mBaseTimeScale = 1.0
    Runtime.mHitStopRemaining = 0.0
    Runtime.mVignetteEnabled = false
    Runtime.mStatus = "NORMAL"

    ResetTimeEffects()
    SetGlobalTimeDilation(1.0)
    SetVignette(
        Config.VignetteIntensity,
        Config.VignetteRadius,
        Config.VignetteSoftness,
        Config.VignetteColor)
    SetVignetteEnabled(false)
end

local function DrawHUD()
    local Viewport = GetViewportSize()
    local TextSize = math.max(16.0, math.min(24.0, Viewport.Y / 45.0))
    local LineHeight = TextSize * 1.45
    local TopLeft = FVector2.new(0.0, 0.0)
    local TopRight = FVector2.new(1.0, 0.0)
    local BottomCenter = FVector2.new(0.5, 1.0)

    AddUIText("TIME EFFECTS PRESENTATION", TextSize * 1.45, TopLeft, TopLeft, FVector2.new(24.0, 18.0))
    AddUIText("[1] Normal Speed (1.0x)", TextSize, TopLeft, TopLeft, FVector2.new(24.0, 18.0 + LineHeight * 1.5))
    AddUIText("[2] Slow Motion (0.2x)", TextSize, TopLeft, TopLeft, FVector2.new(24.0, 18.0 + LineHeight * 2.5))
    AddUIText("[3 / Space] Hit Stop (0.65 sec)", TextSize, TopLeft, TopLeft, FVector2.new(24.0, 18.0 + LineHeight * 3.5))
    AddUIText("[4 / V] Vignette ON / OFF", TextSize, TopLeft, TopLeft, FVector2.new(24.0, 18.0 + LineHeight * 4.5))
    AddUIText("[R] Reset presentation", TextSize, TopLeft, TopLeft, FVector2.new(24.0, 18.0 + LineHeight * 5.5))

    local TimeScale = Runtime.mHitStopRemaining > 0.0 and 0.0 or Runtime.mBaseTimeScale
    AddUIText(string.format("TIME SCALE  %.2fx", TimeScale), TextSize * 1.25, TopRight, TopRight, FVector2.new(-24.0, 18.0))
    AddUIText("STATE  " .. Runtime.mStatus, TextSize, TopRight, TopRight, FVector2.new(-24.0, 18.0 + LineHeight * 1.5))
    AddUIText(
        "VIGNETTE  " .. (Runtime.mVignetteEnabled and "ON" or "OFF"),
        TextSize,
        TopRight,
        TopRight,
        FVector2.new(-24.0, 18.0 + LineHeight * 2.5))

    if Runtime.mHitStopRemaining > 0.0 then
        AddUIText(
            string.format("HIT STOP  %.2f", Runtime.mHitStopRemaining),
            TextSize * 1.8,
            BottomCenter,
            BottomCenter,
            FVector2.new(0.0, -64.0))
    else
        AddUIText(
            "Moving objects use scaled DeltaTime",
            TextSize * 0.9,
            BottomCenter,
            BottomCenter,
            FVector2.new(0.0, -42.0))
    end
end

local function InitializeCamera()
    Runtime.mCameraManager = GetPlayerCameraManager()
    if Runtime.mCameraManager
        and Runtime.mCameraManager:IsValid() then
        Runtime.mCameraManager:SetViewTarget(self)
    end
end

function BeginPlay()
    InitializeCamera()
    ResetPresentation()
    Log("[TimeEffectsPresentation] 1: Normal | 2: Slow Motion | 3/Space: Hit Stop | 4/V: Vignette | R: Reset")
end

function Tick(DeltaTime)
    local RealDeltaTime = GetUnscaledDeltaTime()

    if IsNumberPressed(1) then
        ApplyBaseTimeScale(1.0, "NORMAL")
    elseif IsNumberPressed(2) then
        ApplyBaseTimeScale(Config.SlowMotionScale, "SLOW MOTION")
    elseif IsNumberPressed(3) or IsKeyPressed(Key.Space) then
        StartHitStop()
    elseif IsNumberPressed(4) or IsKeyPressed(Key.V) then
        ToggleVignette()
    elseif IsKeyPressed(Key.R) then
        ResetPresentation()
    end

    if Runtime.mHitStopRemaining > 0.0 then
        Runtime.mHitStopRemaining = Runtime.mHitStopRemaining - RealDeltaTime
        if Runtime.mHitStopRemaining <= 0.0 then
            Runtime.mHitStopRemaining = 0.0
            SetGlobalTimeDilation(Runtime.mBaseTimeScale)
            Runtime.mStatus = Runtime.mBaseTimeScale < 1.0 and "SLOW MOTION" or "NORMAL"
        end
    end

    DrawHUD()
end

function EndPlay()
    ResetTimeEffects()
    ResetVignette()
    SetVignetteEnabled(true)
    Runtime.mCameraManager = false
end

function OnReload()
    InitializeCamera()
    ResetPresentation()
end
