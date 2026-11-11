-- Three unpossessed actors share this graph: baseline jump, run + hit, jump + hit.
-- Jump_Loop is a looping airborne pose; Lua supplies ballistic actor motion.
local PREFIX = "Imported/UAL1_Standard/ANIM_UAL1_Standard_Armature_"
local TAKEOFF_SPEED, GRAVITY = 6.4, 8.5
local START_DURATION, LAND_DURATION, HIT_DURATION = 0.30, 0.32, 0.60
local TAKEOFF_AT, IDLE_WAIT = 0.12, 0.8
local RUN_SPEED, RUN_LIMIT = 2.6, 2.7
local HIT_TARGETS = { "JumpLoop", "JumpStart", "JumpLand" }
local Anim, Machine, Home, Mode, Column
local States, Names, Players, Edges = {}, {}, {}, {}
local Ready, LastState, StateAge = false, nil, 0
local Height, VelocityZ, Airborne, HasTakenOff, LandingNeeded = 0, 0, false, false, false
local JumpCycle, JumpHitDone = 0, false
local Direction, Yaw, TurnFromYaw, DesiredRun, RunLeg, RunHitDone = 1, 0, 0, false, 1, false
-- This clock belongs to the turn, so entering Hit/Idle must not restart it.
local TurnElapsed = 0
local HitRequested, HitCount, HitSource, HitInterrupted, LastHitTime = false, 0, "--", false, -100

local function CurrentState() return Names[Machine:GetCurrentStateIndex()] end

local function AddState(name, clip, looping, duration)
    local player = Anim:CreateNode("SequencePlayer")
    player:SetSequencePlayer(PREFIX .. clip .. ".fbxbin")
    player:SetLooping(looping)
    if duration then
        assert(player:GetPlayLength() > 0, "Empty clip: " .. clip)
        player:SetPlayRate(player:GetPlayLength() / duration)
    end
    local result = Anim:CreateNode("StateResult")
    result:SetInputNode(player)
    local state = Anim:CreateState()
    state:SetStateResult(result)
    local index = Machine:AddState(state)
    assert(index >= 0, "Cannot add state: " .. name)
    States[name], Names[index], Players[name] = index, name, player
end

local function Transition(from, to, duration, condition, anyState)
    local transition = Anim:CreateTransition()
    if anyState then transition:SetAnyState(true)
    else transition:SetFromStateIndex(States[from]) end
    transition:SetToStateIndex(States[to])
    transition:SetPriority(anyState and 0 or 10)
    transition:SetBlendDuration(duration)
    transition:SetConditionFunction(condition)
    local index = Machine:AddTransition(transition)
    assert(index >= 0, "Cannot add transition")
    Edges[index] = { From = anyState and "ANY" or from, To = to, Duration = duration }
end

local function BuildGraph()
    Machine = Anim:CreateNode("StateMachine")
    AddState("Idle", "Idle_Loop", true)
    AddState("Run", "Sprint_Loop", true)
    AddState("JumpStart", "Jump_Start", false, START_DURATION)
    AddState("JumpLoop", "Jump_Loop", true)
    AddState("JumpLand", "Jump_Land", false, LAND_DURATION)
    AddState("Hit", "Hit_Chest", false, HIT_DURATION)

    -- ONE AnyState edge serves every source state, including an active normal blend.
    Transition(nil, "Hit", 0.10, function()
        if not HitRequested then return false end
        HitRequested = false
        HitSource = CurrentState()
        HitInterrupted = Machine:GetActiveTransitionIndex() >= 0
        HitCount, LastHitTime = HitCount + 1, GetWorldTimeSeconds()
        print(string.format("[JumpAnyState][%s] %s -> Hit | airborne=%s height=%.2f interrupt=%s",
            self:GetName(), HitSource, tostring(Airborne), Height, tostring(HitInterrupted)))
        return true
    end, true)

    if Mode == "RunHit" then
        Transition("Idle", "Run", 0.26, function() return DesiredRun end)
        Transition("Run", "Idle", 0.18, function() return not DesiredRun end)
        Transition("Hit", "Run", 0.18, function()
            return not Players.Hit:IsPlaying() and DesiredRun
        end)
        Transition("Hit", "Idle", 0.18, function()
            return not Players.Hit:IsPlaying() and not DesiredRun
        end)
    else
        Transition("Idle", "JumpStart", 0.24, function() return StateAge >= IDLE_WAIT end)
        Transition("JumpStart", "JumpLoop", 0.14, function()
            return HasTakenOff and Airborne and not Players.JumpStart:IsPlaying()
        end)
        Transition("JumpStart", "JumpLand", 0.16, function()
            return HasTakenOff and not Airborne
        end)
        Transition("JumpLoop", "JumpLand", 0.16, function() return not Airborne end)
        Transition("JumpLand", "Idle", 0.16, function() return not Players.JumpLand:IsPlaying() end)
        -- Hit replaces the pose, not the physics. Never launch another jump on recovery.
        Transition("Hit", "JumpLoop", 0.14, function()
            return not Players.Hit:IsPlaying() and Airborne
        end)
        Transition("Hit", "JumpLand", 0.16, function()
            return not Players.Hit:IsPlaying() and not Airborne and LandingNeeded
        end)
        Transition("Hit", "Idle", 0.16, function()
            return not Players.Hit:IsPlaying() and not Airborne and not LandingNeeded
        end)
    end
    assert(Machine:SetInitialStateIndex(States.Idle))
    local root = Anim:CreateNode("Root")
    root:SetInputNode(Machine)
    Anim:SetRootNode(root)
end

local function SyncStateEntry(dt)
    local state = CurrentState()
    if state == LastState then StateAge = StateAge + dt; return state end
    LastState, StateAge = state, 0
    if state == "JumpStart" then
        JumpCycle, JumpHitDone, HasTakenOff = JumpCycle + 1, false, false
    elseif state == "JumpLand" then
        LandingNeeded = false
    end
    return state
end

local function RequestHit()
    -- The engine intentionally excludes AnyState self-transitions. Do not queue a stale hit.
    if CurrentState() ~= "Hit" then HitRequested = true end
end

local function UpdateJump(dt, state)
    if state == "JumpStart" and not HasTakenOff and StateAge >= TAKEOFF_AT then
        Height, VelocityZ, Airborne, HasTakenOff, LandingNeeded = 0, TAKEOFF_SPEED, true, true, true
    end
    if Airborne then
        Height = Height + VelocityZ * dt - 0.5 * GRAVITY * dt * dt
        VelocityZ = VelocityZ - GRAVITY * dt
        if Height <= 0 and VelocityZ <= 0 then Height, VelocityZ, Airborne = 0, 0, false end
    end
    -- Both jumping actors stay at the same XY, even while their pose is Hit.
    self:SetActorLocation(FVector.new(Home.X, Home.Y, Home.Z + Height))

    if Mode == "JumpHit" and JumpCycle > 0 and not JumpHitDone then
        local target = HIT_TARGETS[(JumpCycle - 1) % #HIT_TARGETS + 1]
        local atTime = target == "JumpLoop" and 0.42 or (target == "JumpStart" and 0.17 or 0.05)
        if state == target and StateAge >= atTime then
            JumpHitDone = true
            RequestHit()
        end
    end
end

local function UpdateRunner(dt, state)
    if state == "Idle" and not DesiredRun then
        TurnElapsed = TurnElapsed + dt
        local alpha = math.min(1, TurnElapsed / 0.55)
        alpha = alpha * alpha * (3 - 2 * alpha)
        local target = Direction == 1 and 0 or 180
        local delta = (target - TurnFromYaw + 180) % 360 - 180
        Yaw = TurnFromYaw + delta * alpha
        if TurnElapsed >= IDLE_WAIT then DesiredRun = true end
    end
    if state == "Run" then
        local location = self:GetActorLocation()
        local x = math.max(-RUN_LIMIT, math.min(RUN_LIMIT, location.X + Direction * RUN_SPEED * dt))
        self:SetActorLocation(FVector.new(x, Home.Y, Home.Z))
        if Direction * x >= RUN_LIMIT then
            TurnFromYaw, Direction, DesiredRun = Yaw, -Direction, false
            TurnElapsed = 0
            RunLeg, RunHitDone = RunLeg + 1, false
        elseif not RunHitDone then
            -- Alternate a mid-run hit with a hit DURING the next Idle -> Run blend.
            local triggerTime = RunLeg % 2 == 1 and 0.95 or 0.07
            if StateAge >= triggerTime then RunHitDone = true; RequestHit() end
        end
    end
    self:SetActorRotation(FRotator.new(0, Yaw, 0))
end

local function DrawHUD(state)
    local viewport = GetViewportSize()
    local size = math.max(11, math.min(17, viewport.Y / 48))
    local anchor = FVector2.new(Column * 0.5, 1)
    local offset = Column == 0 and 8 or (Column == 2 and -8 or 0)
    local width = math.min(viewport.X / 3 - 12, size * 29)
    AddUIImage("Assets/Textures/JumpAnyStateHUD.png", FVector2.new(width, size * 10),
        anchor, anchor, FVector2.new(offset, -8))
    local left = Column == 0 and 18 or (Column == 2 and -width + 2 or -width / 2 + 10)
    local textAnchor = FVector2.new(Column * 0.5, 1)
    local alignment = FVector2.new(0, 1)
    local function Text(text, row)
        AddUIText(text, size, textAnchor, alignment, FVector2.new(left, -20 - (5 - row) * size * 1.4))
    end
    Text(Mode == "JumpOnly" and "1 / CYAN - JUMP LOOP"
        or (Mode == "RunHit" and "2 / ORANGE - RUN + HIT" or "3 / PINK - JUMP + HIT"), 0)
    Text(string.format("State: %s | Height: %.2fm", state or "Idle", Height), 1)
    Text(string.format("AnyState hits: %d | From: %s", HitCount, HitSource), 2)
    Text(HitCount == 0 and "Last hit: --"
        or (HitInterrupted and "Last hit: INTERRUPTED A BLEND" or "Last hit: state pose -> Hit"), 3)
    if Mode == "JumpHit" then
        Text("Auto hit: Loop -> Start -> Land", 4)
    elseif Mode == "RunHit" then
        Text("Mid-run / transition hits alternate", 4)
    else
        Text("Start -> LOOP in air -> Land -> Idle", 4)
    end
    local edge = Edges[Machine:GetActiveTransitionIndex()]
    if edge then
        Text(string.format("%s -> %s | blend %.0f%%", edge.From, edge.To,
            math.min(1, Machine:GetTransitionElapsedTime() / edge.Duration) * 100), 5)
    else
        Text(GetWorldTimeSeconds() - LastHitTime < HIT_DURATION and "ANY STATE HIT" or "Transition: inactive", 5)
    end
end

function BeginPlay()
    Ready = false
    local name = self:GetName()
    Mode = name:find("RunHit", 1, true) and "RunHit" or (name:find("JumpHit", 1, true) and "JumpHit" or "JumpOnly")
    Column = Mode == "JumpOnly" and 0 or (Mode == "RunHit" and 1 or 2)
    Home = self:GetActorLocation()
    -- OnReload may happen in mid-air. The demonstration ground is always Z=0.025.
    Home = FVector.new(Mode == "RunHit" and -RUN_LIMIT or 0, Home.Y, 0.025)
    self:SetActorLocation(Home)
    Yaw = Mode == "RunHit" and 0 or 180
    self:SetActorRotation(FRotator.new(0, Yaw, 0))
    Direction, TurnFromYaw, DesiredRun, RunLeg, RunHitDone = 1, 0, false, 1, false
    TurnElapsed = 0
    Height, VelocityZ, Airborne, HasTakenOff, LandingNeeded = 0, 0, false, false, false
    LastState, StateAge, JumpCycle, JumpHitDone = nil, 0, 0, false
    HitRequested, HitCount, HitSource, HitInterrupted, LastHitTime = false, 0, "--", false, -100
    States, Names, Players, Edges = {}, {}, {}, {}
    local mesh = self:GetSkeletalMesh()
    assert(mesh, "JumpAnyStateModel requires a skeletal mesh")
    Anim = mesh:CreateAnimInstance()
    BuildGraph()
    Ready = true
end

function Tick(dt)
    if not Ready or not Anim:IsValid() then return end
    local state = SyncStateEntry(dt)
    if Mode == "RunHit" then UpdateRunner(dt, state) else UpdateJump(dt, state) end
    if GetCursorMode() == CursorMode.Locked and (IsKeyPressed(Key.H)
        or IsKeyPressed(Key["Num" .. (Column + 1)])) then RequestHit() end
    DrawHUD(state)
end

function EndPlay()
    Ready = false
    if Anim and Anim:IsValid() then Anim:SetRootNode(nil) end
    Anim, Machine = nil, nil
    States, Names, Players, Edges = {}, {}, {}, {}
end

function OnReload()
    EndPlay()
    BeginPlay()
end
