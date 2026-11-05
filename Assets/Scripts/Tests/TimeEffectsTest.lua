local BaseTimeScale = 1.0
local SlowMotionRemaining = 0.0
local HitStopRemaining = 0.0
local LogElapsed = 0.0
local HitStopLogElapsed = 0.0

local function PrintTimeState(Label, DeltaTime)
    local RealDt = GetUnscaledDeltaTime()
    local WorldDt = GetWorldDeltaTime()

    print(string.format(
        "[TimeTest][%s] TickDt=%.6f RealDt=%.6f WorldDt=%.6f Global=%.3f Effective=%.3f RealTime=%.3f WorldTime=%.3f",
        Label,
        DeltaTime,
        RealDt,
        WorldDt,
        GetGlobalTimeDilation(),
        GetEffectiveTimeDilation(),
        GetRealTimeSeconds(),
        GetWorldTimeSeconds()
    ))
end

local function StartSlowMotion(Scale, RealDuration)
    BaseTimeScale = Scale
    SlowMotionRemaining = RealDuration

    -- Hit Stop 중에는 0배속을 유지하고 종료 후 새 슬로모 배율을 적용한다.
    if HitStopRemaining <= 0.0 then
        SetGlobalTimeDilation(BaseTimeScale)
    end

    print(string.format(
        "[TimeTest] Slow motion started: Scale=%.2f Duration=%.2f real seconds",
        Scale,
        RealDuration
    ))
end

local function StopSlowMotion()
    BaseTimeScale = 1.0
    SlowMotionRemaining = 0.0

    if HitStopRemaining <= 0.0 then
        SetGlobalTimeDilation(BaseTimeScale)
    end

    print("[TimeTest] Slow motion ended: BaseScale=1.0")
end

local function StartHitStop(RealDuration)
    HitStopRemaining = math.max(HitStopRemaining, RealDuration)
    HitStopLogElapsed = 0.0

    -- Hit Stop도 별도 엔진 API 없이 Global Time Dilation으로 구현한다.
    SetGlobalTimeDilation(0.0)

    print(string.format(
        "[TimeTest] Lua Hit Stop started: Scale=0.0 Duration=%.2f real seconds",
        RealDuration
    ))
end

local function StopHitStop()
    HitStopRemaining = 0.0
    HitStopLogElapsed = 0.0

    -- Hit Stop 이전에 슬로모 중이었다면 해당 배율로 돌아간다.
    SetGlobalTimeDilation(BaseTimeScale)

    print(string.format(
        "[TimeTest] Lua Hit Stop ended: restored Scale=%.2f",
        BaseTimeScale
    ))
end

local function ResetTimeState()
    BaseTimeScale = 1.0
    SlowMotionRemaining = 0.0
    HitStopRemaining = 0.0
    HitStopLogElapsed = 0.0
    SetGlobalTimeDilation(1.0)
end

function BeginPlay()
    ResetTimeEffects()
    ResetTimeState()
    LogElapsed = 0.0

    print("[TimeTest] =================================")
    print("[TimeTest] F1: 3초 동안 0.2배 슬로모")
    print("[TimeTest] F2: 모든 시간 효과 해제")
    print("[TimeTest] F3: Lua Slomo 기반 0.25초 Hit Stop")
    print("[TimeTest] =================================")
end

function Tick(DeltaTime)
    local RealDt = GetUnscaledDeltaTime()
    local SlowMotionStartedThisFrame = false
    local HitStopStartedThisFrame = false

    if IsKeyPressed(Key.F1) then
        StartSlowMotion(0.2, 3.0)
        SlowMotionStartedThisFrame = true
    elseif IsKeyPressed(Key.F2) then
        ResetTimeState()
        print("[TimeTest] All time effects reset")
    elseif IsKeyPressed(Key.F3) then
        StartHitStop(0.25)
        HitStopStartedThisFrame = true
    end

    -- 슬로모 지속시간은 WorldDt가 아니라 실제 시간으로 계산한다.
    if not SlowMotionStartedThisFrame
        and SlowMotionRemaining > 0.0 then

        SlowMotionRemaining =
            SlowMotionRemaining - RealDt

        if SlowMotionRemaining <= 0.0 then
            StopSlowMotion()
        end
    end

    -- Time Dilation이 0이어도 Lua Tick은 실행되므로 실제 시간으로 복구한다.
    if not HitStopStartedThisFrame
        and HitStopRemaining > 0.0 then

        HitStopRemaining =
            HitStopRemaining - RealDt

        HitStopLogElapsed =
            HitStopLogElapsed + RealDt

        if HitStopLogElapsed >= 0.05 then
            HitStopLogElapsed =
                HitStopLogElapsed - 0.05

            PrintTimeState("LuaHitStop", DeltaTime)
        end

        if HitStopRemaining <= 0.0 then
            StopHitStop()
        end
    end

    -- 평상시에는 0.5 실제 초마다 시간 상태를 출력한다.
    LogElapsed = LogElapsed + RealDt

    if LogElapsed >= 0.5 then
        LogElapsed = LogElapsed - 0.5
        PrintTimeState("Periodic", DeltaTime)
    end

end

function EndPlay()
    ResetTimeState()
    print("[TimeTest] EndPlay: time effects reset")
end

function OnReload()
    ResetTimeState()
    LogElapsed = 0.0

    print("[TimeTest] Reloaded: time effects reset")
end
