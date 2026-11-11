-- Assets/Scenes/Tests/KirbyAnimDemo.scene의 KirbyPlayer에 연결한다.
-- 기존 importer 출력: Assets/Imported/Kirby (Skeletal Combine / Import Animations 켜짐).
-- SequencePlayer -> StateResult -> StateMachine -> Root -> AnimInstance 최종 pose.
-- 인사와 클립 미리보기는 AnyState로 Action 상태에 진입하고, 종료 후 현재 이동 상태로 복귀한다.
-- 이동은 Capsule 루트의 실제 Sweep을 사용한다. SetActorLocation은 리셋에만 사용한다.
local PREFIX = "Imported/Kirby/ANIM_Kirby_"
local WALK_SPEED, RUN_SPEED, FLY_SPEED, GRAVITY = 2.2, 4.3, 2.6, 11.0
local CAMERA_DISTANCE, CAMERA_HEIGHT, CAMERA_PITCH = 6.8, 4.3, 29.0
local STATE_ORDER = { "Idle", "Walk", "Run", "Sit", "Fly", "Fall", "Land" }
local STATE_CLIPS = {
    Idle = "Idle", Walk = "Walk", Run = "Run", Sit = "Sit",
    Fly = "Fly", Fall = "Fall", Land = "Land-SaluteForward",
}
local CATALOG = {
    "Idle", "Walk", "Run", "Fly", "Fall", "Sit",
    "Land-SaluteForward", "Land-SaluteLeft", "Land-SaluteRight",
    "Carried", "Carried.001", "ClimbCeiling", "ClimbCeiling.001", "ArmatureAction",
}
local ACTION_ORDER, ACTION_CLIPS = {}, {}
for _, clip in ipairs(CATALOG) do
    local name = "Action_" .. clip
    ACTION_ORDER[#ACTION_ORDER + 1], ACTION_CLIPS[name] = name, clip
end
local Mesh, Camera, Anim, Machine
local States, Names, Edges = {}, {}, {}
local Home, Yaw, VelocityZ = nil, 0, 0
local MoveX, MoveY, Moving, Running, Flying, Sitting = 0, 0, false, false, false, false
local Grounded, Landing, GroundContact, CeilingContact = true, false, false, false
local DesiredState, GalleryIndex, RequestedAction, ForceResume = "Idle", 1, nil, false
local HitName, HitTimer, Ready, ShowHelp = "None", 0, false, true

local function ClipPath(name) return PREFIX .. name .. ".fbxbin" end
local function CurrentState() return Names[Machine:GetCurrentStateIndex()] end
local function IsAction(name) return ACTION_CLIPS[name] ~= nil end

local function AddState(name, clip, looping)
    local player = Anim:CreateNode("SequencePlayer")
    player:SetSequencePlayer(ClipPath(clip))
    assert(player:GetPlayLength() > 0, "Kirby animation is empty: " .. name)
    player:SetLooping(looping)
    -- 기본 착지 State만 짧게 재생한다. 같은 클립의 Action 미리보기는 원래 속도다.
    if name == "Land" then player:SetPlayRate(player:GetPlayLength() / 0.35) end
    local result = Anim:CreateNode("StateResult")
    result:SetInputNode(player)
    local state = Anim:CreateState()
    state:SetStateResult(result)
    local index = Machine:AddState(state)
    assert(index >= 0, "Kirby state registration failed: " .. name)
    States[name], Names[index] = { Index = index, Player = player }, name
end

local function AddTransition(from, to, duration, priority, condition)
    local transition = Anim:CreateTransition()
    if from then transition:SetFromStateIndex(States[from].Index)
    else transition:SetAnyState(true) end
    transition:SetToStateIndex(States[to].Index)
    transition:SetPriority(priority)
    transition:SetBlendDuration(duration)
    transition:SetConditionFunction(condition)
    local index = Machine:AddTransition(transition)
    assert(index >= 0, "Kirby transition registration failed")
    Edges[index] = { From = from or "ANY", To = to, Duration = duration }
end

local function BuildGraph()
    States, Names, Edges = {}, {}, {}
    Machine = Anim:CreateNode("StateMachine")
    for _, name in ipairs(STATE_ORDER) do
        AddState(name, STATE_CLIPS[name], name ~= "Sit" and name ~= "Land")
    end
    for _, name in ipairs(ACTION_ORDER) do AddState(name, ACTION_CLIPS[name], false) end
    -- 목표 이동 상태는 충돌 결과로 계속 갱신하지만 Action 재생 중에는 진입하지 않는다.
    for _, from in ipairs(STATE_ORDER) do
        for _, to in ipairs(STATE_ORDER) do
            if from ~= to then
                local target = to
                AddTransition(from, target, (target == "Fly" or target == "Fall") and 0.10 or 0.16,
                    10, function() return DesiredState == target end)
            end
        end
    end
    for _, name in ipairs(ACTION_ORDER) do
        local action = name
        -- 일반 전이(priority 10) 도중에도 Action(priority 0)이 현재 혼합 포즈에서 끼어든다.
        AddTransition(nil, action, 0.12, 0, function()
            if RequestedAction ~= action then return false end
            RequestedAction, ForceResume = nil, false
            return true
        end)
        for _, name in ipairs(STATE_ORDER) do
            local target = name
            AddTransition(action, target, 0.20, 10, function()
                return DesiredState == target and not States[action].Player:IsPlaying()
            end)
        end
    end
    for _, name in ipairs(STATE_ORDER) do
        local target = name
        -- N은 Action 진입 Blend도 중단하고 현재 이동/비행 상태로 복귀한다.
        AddTransition(nil, target, 0.20, -10, function()
            if not ForceResume or DesiredState ~= target then return false end
            ForceResume = false
            return true
        end)
    end
    assert(Machine:SetInitialStateIndex(States.Idle.Index))
    local root = Anim:CreateNode("Root")
    root:SetInputNode(Machine)
    Anim:SetRootNode(root)
end

local function RequestAction(clip)
    local action = "Action_" .. clip
    assert(ACTION_CLIPS[action], "Unknown Kirby action: " .. clip)
    -- AnyState의 자기 전이는 제외되므로 같은 Action 재요청은 종료/복귀 후 다시 실행한다.
    RequestedAction = action
end

local function StopAction()
    RequestedAction = nil
    ForceResume = IsAction(CurrentState())
end

local function ResetPlayer()
    self:SetActorLocation(FVector.new(Home.X, Home.Y, Home.Z))
    VelocityZ, Grounded, Landing, Sitting = 0, true, false, false
    MoveX, MoveY, Moving, Running, Flying = 0, 0, false, false, false
    DesiredState, RequestedAction, ForceResume, HitName, HitTimer = "Idle", nil, false, "None", 0
    BuildGraph()
end

local function ReadInput()
    MoveX, MoveY, Moving, Running, Flying = 0, 0, false, false, false
    if GetCursorMode() ~= CursorMode.Locked then return end
    MoveX = (IsKeyDown(Key.W) and 1 or 0) - (IsKeyDown(Key.S) and 1 or 0)
    MoveY = (IsKeyDown(Key.D) and 1 or 0) - (IsKeyDown(Key.A) and 1 or 0)
    local length = math.sqrt(MoveX * MoveX + MoveY * MoveY)
    Moving, Running, Flying = length > 0, IsKeyDown(Key.Shift), IsKeyDown(Key.Space)
    if Moving then MoveX, MoveY = MoveX / length, MoveY / length end
    if IsKeyPressed(Key.C) and Grounded then Sitting = not Sitting end
    if Moving or Flying then Sitting = false end
    if IsKeyPressed(Key.F) then RequestAction("Land-SaluteForward") end
    if IsKeyPressed(Key.Q) then RequestAction("Land-SaluteLeft") end
    if IsKeyPressed(Key.E) then RequestAction("Land-SaluteRight") end
    if IsKeyPressed(Key.N) then StopAction() end
    if IsKeyPressed(Key.Z) then GalleryIndex = (GalleryIndex - 2) % #CATALOG + 1 end
    if IsKeyPressed(Key.X) then GalleryIndex = GalleryIndex % #CATALOG + 1 end
    if IsKeyPressed(Key.V) then RequestAction(CATALOG[GalleryIndex]) end
    if IsKeyPressed(Key.Tab) then ShowHelp = not ShowHelp end
    if IsKeyPressed(Key.R) then ResetPlayer() end
end

local function MovePlayer(dt)
    if dt <= 0 then return end
    local wasGrounded = Grounded
    local before = self:GetActorLocation()
    if Moving then
        local targetYaw = math.deg(math.atan2(MoveY, MoveX))
        local difference = (targetYaw - Yaw + 180) % 360 - 180
        Yaw = Yaw + difference * (1 - math.exp(-15 * dt))
    end
    local rotation = FRotator.new(0, Yaw, 0)
    local speed = Running and RUN_SPEED or WALK_SPEED
    -- 짧은 substep과 축별 Sweep으로 바닥/모서리/천장 충돌을 처리한다.
    local steps = math.max(1, math.ceil(dt / (1 / 60)))
    local step = dt / steps
    for _ = 1, steps do
        if Moving then
            self:Move(FVector.new(MoveX * speed * step, 0, 0), rotation)
            self:Move(FVector.new(0, MoveY * speed * step, 0), rotation)
        else
            self:SetActorRotation(rotation)
        end
        if Flying then
            -- 예제의 비행 상한. 놓으면 중력으로 바닥이나 장애물 위에 착지한다.
            VelocityZ = self:GetActorLocation().Z < Home.Z + 4.5 and FLY_SPEED or 0
        else
            VelocityZ = math.max(-8.0, VelocityZ - GRAVITY * step)
        end
        GroundContact, CeilingContact = false, false
        self:Move(FVector.new(0, 0, VelocityZ * step), rotation)
        Grounded = GroundContact and VelocityZ <= 0
        if Grounded or CeilingContact then VelocityZ = 0 end
    end
    if not wasGrounded and Grounded then Landing = true end
    if not Grounded then Landing = false end
    -- 벽에 막혔을 때 실제 이동 속도가 0이면 제자리 걷기 대신 Idle을 선택한다.
    local after = self:GetActorLocation()
    local distance = math.sqrt((after.X - before.X)^2 + (after.Y - before.Y)^2)
    if not Grounded then
        DesiredState = Flying and "Fly" or "Fall"
    else
        if Landing and CurrentState() == "Land" and not States.Land.Player:IsPlaying() then Landing = false end
        if Landing then DesiredState = "Land"
        elseif Sitting then DesiredState = "Sit"
        elseif distance > 0.03 * dt then DesiredState = Running and "Run" or "Walk"
        else DesiredState = "Idle" end
    end
    if after.Z < -8 then ResetPlayer() end
end

local function UpdateCamera()
    if not Camera or not Camera:IsValid() then return end
    -- Pawn 회전을 상쇄한 고정 방향 시점: W는 화면 안쪽, D는 화면 오른쪽이다.
    local rad = math.rad(Yaw)
    Camera:SetRelativeLocation(FVector.new(-CAMERA_DISTANCE * math.cos(rad), CAMERA_DISTANCE * math.sin(rad), CAMERA_HEIGHT))
    Camera:SetRelativeRotation(FRotator.new(CAMERA_PITCH, -Yaw, 0))
end

local function DrawHUD()
    local name = CurrentState() or "Idle"
    local action = ACTION_CLIPS[name] or "None"
    local anchor = FVector2.new(0, 0)
    local size = math.max(14, math.min(18, GetViewportSize().Y / 45))
    AddUIImage("Assets/Textures/SkeletalAnimTestHUD.png", FVector2.new(760, size * (ShowHelp and 12.5 or 5.8)),
        anchor, anchor, FVector2.new(10, 44))
    local function Text(value, line)
        AddUIText(value, size, anchor, anchor, FVector2.new(20, 54 + line * size * 1.4))
    end
    Text("KIRBY / STATE MACHINE + ANY STATE", 0)
    Text(string.format("State: %s   Action: %s   Target: %s", name, action, DesiredState), 1)
    Text(string.format("Grounded: %s   Last obstacle: %s", tostring(Grounded), HitTimer > 0 and HitName or "None"), 2)
    if ShowHelp then
        local edge = Edges[Machine:GetActiveTransitionIndex()]
        if edge then
            Text(string.format("%s -> %s | Blend %.0f%% / %.2fs", edge.From, edge.To,
                math.min(1, Machine:GetTransitionElapsedTime() / edge.Duration) * 100, edge.Duration), 3)
        else Text("Transition: inactive | Pending: " .. (RequestedAction or "None"), 3) end
        Text("WASD move | Shift run | Hold Space fly / release to fall | C sit", 4)
        Text("F / Q / E AnyState salute | N cancel Action | R reset", 5)
        Text(string.format("Z / X select clip | V AnyState Action [%02d/%02d: %s]", GalleryIndex, #CATALOG, CATALOG[GalleryIndex]), 6)
        Text("Tab help | Shift+F1 release cursor | Click viewport to resume", 7)
    end
end

function BeginPlay()
    Ready = false
    Mesh, Camera = self:GetSkeletalMesh(), self:GetCamera()
    assert(Mesh and Camera, "KirbyAnimDemo requires a SkeletalMesh and Camera")
    Home, Yaw = self:GetActorLocation(), self:GetActorRotation().Yaw
    VelocityZ, Grounded, Landing, Sitting, DesiredState = 0, true, false, false, "Idle"
    RequestedAction, ForceResume = nil, false
    Anim = Mesh:CreateAnimInstance()
    BuildGraph()
    UpdateCamera()
    local manager = GetPlayerCameraManager()
    if manager then manager:SetViewTargetCamera(Camera) end
    SetCursorMode(CursorMode.Locked)
    Ready = true
    print("[KirbyAnimDemo] Ready: AnyState Actions -> StateMachine -> Root -> AnimInstance")
end

function Tick(dt)
    if not Ready or not Anim:IsValid() or not Mesh:IsValid() then return end
    HitTimer = math.max(0, HitTimer - dt)
    ReadInput()
    -- 입력을 해제해도 낙하와 착지는 계속된다.
    MovePlayer(math.max(0, math.min(dt, 0.10)))
    UpdateCamera()
    DrawHUD()
end

function OnHit(other, hit)
    if not Ready or not hit.bBlockingHit then return end
    if hit.Normal.Z > 0.55 then GroundContact = true
    elseif hit.Normal.Z < -0.55 then CeilingContact = true
    else
        HitName, HitTimer = other and other:GetName() or "Obstacle", 0.8
    end
end

function EndPlay()
    Ready = false
    if Anim and Anim:IsValid() then
        Anim:SetRootNode(nil)
    end
    SetCursorMode(CursorMode.Free)
end

function OnReload() BeginPlay() end
