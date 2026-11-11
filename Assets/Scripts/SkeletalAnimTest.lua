-- Assets/Scenes/Tests/SkeletalAnimTest.scene의 Pawn에 연결한다.
-- 이동/카메라/입력/전이 구성은 전부 Lua이며 실제 혼합은 기존 StateMachine이 수행한다.
local PREFIX = "Imported/UAL1_Standard/ANIM_UAL1_Standard_Armature_"
local WALK_SPEED, RUN_SPEED, CROUCH_SPEED = 1.7, 4.8, 0.85
local JUMP_SPEED, GRAVITY = 6.5, 18.0 -- m/s, m/s². 최고 높이 약 1.17m, 체공 시간 약 0.72초.
local JUMP_START_DURATION, JUMP_LAND_DURATION = 0.18, 0.22
local Catalog = {
    "A_TPose", "Crouch_Fwd_Loop", "Crouch_Idle_Loop", "Dance_Loop", "Death01",
    "Driving_Loop", "Fixing_Kneeling", "Hit_Chest", "Hit_Head", "Idle_Loop",
    "Idle_Talking_Loop", "Idle_Torch_Loop", "Interact", "Jog_Fwd_Loop",
    "Jump_Land", "Jump_Loop", "Jump_Start", "PickUp_Table", "Pistol_Aim_Down",
    "Pistol_Aim_Neutral", "Pistol_Aim_Up", "Pistol_Idle_Loop", "Pistol_Reload",
    "Pistol_Shoot", "Punch_Cross", "Punch_Jab", "Push_Loop", "Roll",
    "Sitting_Enter", "Sitting_Exit", "Sitting_Idle_Loop", "Sitting_Talking_Loop",
    "Spell_Simple_Enter", "Spell_Simple_Exit", "Spell_Simple_Idle_Loop",
    "Spell_Simple_Shoot", "Sprint_Loop", "Swim_Fwd_Loop", "Swim_Idle_Loop",
    "Sword_Attack", "Sword_Idle", "Walk_Formal_Loop", "Walk_Loop",
}
local Locomotion = { "Idle", "Walk", "Run", "CrouchIdle", "CrouchWalk" }
local Definitions = {
    Idle = "Idle_Loop", Walk = "Walk_Loop", Run = "Sprint_Loop",
    CrouchIdle = "Crouch_Idle_Loop", CrouchWalk = "Crouch_Fwd_Loop",
}
local StateOrder, ActionStates, ClipToState = {}, {}, {}
for _, name in ipairs(Locomotion) do
    StateOrder[#StateOrder + 1] = name
    ClipToState[Definitions[name]] = name
end
for _, clip in ipairs(Catalog) do
    if not ClipToState[clip] then
        -- 클립마다 별도 State와 고정 SequencePlayer를 만든다.
        ActionStates[clip] = { Clip = clip, Loop = clip:sub(-5) == "_Loop" }
        ClipToState[clip] = clip
        StateOrder[#StateOrder + 1] = clip
    end
end
ActionStates.Death01.Hold = true
ActionStates.Jump_Start.Next, ActionStates.Jump_Start.Takeoff = "Jump_Loop", true
ActionStates.Jump_Loop.Next, ActionStates.Jump_Loop.Takeoff = "Jump_Land", true
ActionStates.Sitting_Enter.Next = "Sitting_Idle_Loop"
ActionStates.Sitting_Idle_Loop.ExitOnMove = "Sitting_Exit"
local Bindings = {
    { Key.F, "Punch_Jab" }, { Key.Q, "Punch_Cross" },
    { Key.G, "Hit_Chest" }, { Key.H, "Hit_Head" }, { Key.R, "Roll" },
    { Key.Space, "Jump_Start" }, { Key.E, "Interact" }, { Key.T, "Dance_Loop" },
    { Key.Y, "Idle_Talking_Loop" }, { Key.K, "Death01" },
    { Key.Num1, "Sword_Attack" }, { Key.Num2, "Pistol_Shoot" },
    { Key.Num3, "Pistol_Reload" }, { Key.Num4, "Spell_Simple_Shoot" },
    { Key.Num5, "PickUp_Table" }, { Key.Num6, "Fixing_Kneeling" },
    { Key.Num7, "Push_Loop" }, { Key.Num8, "Driving_Loop" },
    { Key.Num9, "Swim_Idle_Loop" },
}
local Anim, Machine, Mesh, Camera, Slot
local States, Names = {}, {}
local DesiredLocomotion, RequestedState, PreviewLocomotion = "Idle", nil, nil
local Moving, Crouching, ForceResume = false, false, false
local MoveX, MoveY, Yaw, GalleryIndex = 0, 0, 0, 26
local Home, PreviousLabel, LastState, RuntimeAction = nil, nil, nil, nil
local Ready, ShowHelp = false, true
local Jump = { Height = 0, VelocityZ = 0, Airborne = false }

local function Path(clip) return PREFIX .. clip .. ".fbxbin" end
local function IsAction(name) return ActionStates[name] ~= nil end
local function IsJumpClip(clip)
    return clip == "Jump_Start" or clip == "Jump_Loop" or clip == "Jump_Land"
end
local function CurrentState() return Names[Machine:GetCurrentStateIndex()] end
local function CurrentAction()
    local name = CurrentState()
    return IsAction(name) and RuntimeAction or nil
end
local function QueueAction(clip)
    local target = ClipToState[clip]
    if not target then return end
    if not IsAction(target) then
        -- 갤러리에서는 이동 입력이 없어도 해당 이동 클립을 미리 볼 수 있다.
        PreviewLocomotion = target
        DesiredLocomotion, RequestedState, ForceResume = target, nil, true
        return
    end
    PreviewLocomotion = nil
    local current = CurrentAction()
    -- Space를 연속 입력해도 공중에서 다시 도약하지 않는다.
    if clip == "Jump_Start" and (Jump.Airborne or RequestedState == "Jump_Start"
        or (current and IsJumpClip(current.Clip))) then return end
    if CurrentState() == target then
        -- StateMachine은 자기 전이를 지원하지 않는다. 반복 클립의 재입력은 복귀 요청으로 취급한다.
        if current and current.Loop then ForceResume, RequestedState = true, nil end
        return
    end
    RequestedState, ForceResume = target, false
end

local function AddState(name, clip, looping)
    local player = Anim:CreateNode("SequencePlayer")
    player:SetSequencePlayer(Path(clip))
    player:SetLooping(looping)
    local result = Anim:CreateNode("StateResult")
    result:SetInputNode(player)
    local state = Anim:CreateState()
    state:SetStateResult(result)
    local index = Machine:AddState(state)
    assert(index >= 0, "State registration failed: " .. name)
    States[name], Names[index] = { Index = index, Player = player }, name
end

local function AddTransition(from, to, duration, priority, condition)
    local transition = Anim:CreateTransition()
    transition:SetFromStateIndex(States[from].Index)
    transition:SetToStateIndex(States[to].Index)
    transition:SetBlendDuration(duration)
    transition:SetPriority(priority)
    transition:SetConditionFunction(condition)
    assert(Machine:AddTransition(transition) >= 0, "Transition registration failed")
end

local function CanResume(from)
    if RequestedState then return false end
    if not IsAction(from) then return true end
    if ForceResume then return true end
    if Jump.Airborne then return false end
    local action = ActionStates[from]
    if not action or action.Next or action.Hold then return false end
    if action.ExitOnMove then return false end
    return not States[from].Player:IsPlaying() or (action.Loop and Moving)
end

local function BuildGraph()
    Machine = Anim:CreateNode("StateMachine")
    for _, name in ipairs(StateOrder) do
        local action = ActionStates[name]
        local clip = action and action.Clip or Definitions[name]
        AddState(name, clip, action and action.Loop or not action)
        if action and (name == "Jump_Start" or name == "Jump_Land") then
            local player = States[name].Player
            local duration = name == "Jump_Start" and JUMP_START_DURATION or JUMP_LAND_DURATION
            player:SetPlayRate(player:GetPlayLength() / duration)
        end
    end
    for _, from in ipairs(StateOrder) do
        -- 현재 엔진에는 Any State 전이가 없어 각 출발 상태에서 목표 Action까지 연결한다.
        for _, to in ipairs(StateOrder) do
            if IsAction(to) and from ~= to then
                local target = to
                AddTransition(from, target, 0.12, 0, function()
                    if RequestedState ~= target then return false end
                    RequestedState = nil
                    return true
                end)
            end
        end
        for _, to in ipairs(Locomotion) do
            if from ~= to then
                local source, target = from, to
                AddTransition(from, to, IsAction(from) and 0.18 or 0.16, 10, function()
                    if DesiredLocomotion ~= target or not CanResume(source) then return false end
                    ForceResume = false
                    return true
                end)
            end
        end
    end
    assert(Machine:SetInitialStateIndex(States.Idle.Index))
    Slot = Anim:CreateNode("Slot")
    Slot:SetSlotName("DefaultSlot")
    Slot:SetInputNode(Machine)
    local root = Anim:CreateNode("Root")
    root:SetInputNode(Slot)
    Anim:SetRootNode(root)
end

local function SyncStateEntry()
    local name = CurrentState()
    if name == LastState then return end
    local definition = ActionStates[name]
    RuntimeAction = nil
    if definition then
        -- 상태 정의는 고정하고, 재진입 때만 점프/구르기 실행 값을 새로 만든다.
        RuntimeAction = {
            Clip = name, Loop = definition.Loop, Next = definition.Next,
            Hold = definition.Hold, ExitOnMove = definition.ExitOnMove,
            Takeoff = definition.Takeoff and (name ~= "Jump_Loop" or LastState ~= "Jump_Start"),
            RollX = math.cos(math.rad(Yaw)), RollY = math.sin(math.rad(Yaw)),
        }
    end
    LastState = name
end

local function UpdateActionChain()
    local action = CurrentAction()
    if not action or RequestedState or ForceResume
        or Machine:GetActiveTransitionIndex() ~= -1 then return end
    local player = States[CurrentState()].Player
    if action.Clip == "Jump_Loop" then
        -- 고정 시간이 아니라 실제 지면 도달 시점에 착지 포즈로 전환한다.
        if not Jump.Airborne then RequestedState = "Jump_Land" end
    elseif action.Clip == "Jump_Start" and not player:IsPlaying() then
        RequestedState = Jump.Airborne and "Jump_Loop" or "Jump_Land"
    elseif action.ExitOnMove and Moving then RequestedState = action.ExitOnMove
    elseif action.Next and not player:IsPlaying() then
        RequestedState = action.Next
    end
end

local function ReadInput()
    MoveX = (IsKeyDown(Key.W) and 1 or 0) - (IsKeyDown(Key.S) and 1 or 0)
    MoveY = (IsKeyDown(Key.D) and 1 or 0) - (IsKeyDown(Key.A) and 1 or 0)
    local length = math.sqrt(MoveX * MoveX + MoveY * MoveY)
    Moving = length > 0
    if Moving then MoveX, MoveY = MoveX / length, MoveY / length end
    if IsKeyPressed(Key.C) then Crouching, PreviewLocomotion = not Crouching, nil end
    if Moving then PreviewLocomotion = nil end
    if Crouching then DesiredLocomotion = PreviewLocomotion or (Moving and "CrouchWalk" or "CrouchIdle")
    else DesiredLocomotion = PreviewLocomotion or (Moving and (IsKeyDown(Key.Shift) and "Run" or "Walk") or "Idle") end
    for _, binding in ipairs(Bindings) do
        if IsKeyPressed(binding[1]) then QueueAction(binding[2]) end
    end
    -- 별도 경로 확인: M은 상태를 바꾸지 않고 DefaultSlot에 단일 클립을 재생한다.
    if IsKeyPressed(Key.M) then Slot:PlayMontage(Path("Punch_Jab"), 1.0, 0.12, 0.18, false) end
    if IsKeyPressed(Key.N) then Slot:StopMontage(0.18) end
    if IsKeyPressed(Key.B) then
        local action = CurrentAction()
        QueueAction(action and action.Clip == "Sitting_Idle_Loop" and "Sitting_Exit" or "Sitting_Enter")
    end
    if IsKeyPressed(Key.Z) then GalleryIndex = (GalleryIndex - 2) % #Catalog + 1 end
    if IsKeyPressed(Key.X) then GalleryIndex = GalleryIndex % #Catalog + 1 end
    if IsKeyPressed(Key.V) then QueueAction(Catalog[GalleryIndex]) end
    if IsKeyPressed(Key.Backspace) then
        PreviewLocomotion, ForceResume, RequestedState = nil, true, nil
        DesiredLocomotion = Crouching and (Moving and "CrouchWalk" or "CrouchIdle")
            or (Moving and (IsKeyDown(Key.Shift) and "Run" or "Walk") or "Idle")
    end
    if IsKeyPressed(Key.Num0) then
        self:SetActorLocation(FVector.new(Home.X, Home.Y, Home.Z))
        Jump.Height, Jump.VelocityZ, Jump.Airborne = 0, 0, false
        ForceResume, RequestedState, PreviewLocomotion, Crouching = true, nil, nil, false
        DesiredLocomotion = Moving and "Walk" or "Idle"
    end
    if IsKeyPressed(Key.Tab) then ShowHelp = not ShowHelp end
end

local function UpdateJump(dt)
    local action = CurrentAction()
    if action and action.Takeoff and not action.LaunchChecked then
        action.LaunchChecked = true
        if not Jump.Airborne and not ForceResume then
            Jump.Height, Jump.VelocityZ, Jump.Airborne = 0, JUMP_SPEED, true
            Crouching = false
        end
    end
    if Jump.Airborne then
        -- 일정 중력의 해석적 적분으로 프레임 간격이 달라도 같은 궤적을 따른다.
        Jump.Height = Jump.Height + Jump.VelocityZ * dt - 0.5 * GRAVITY * dt * dt
        Jump.VelocityZ = Jump.VelocityZ - GRAVITY * dt
        if Jump.Height <= 0 and Jump.VelocityZ <= 0 then
            Jump.Height, Jump.VelocityZ, Jump.Airborne = 0, 0, false
        end
    end
end

local function UpdateMovement(dt)
    local name, action = CurrentState(), CurrentAction()
    local dx, dy, speed = 0, 0, 0
    local jumping = Jump.Airborne or (action and IsJumpClip(action.Clip))
    if Moving and (jumping or (not IsAction(name) and not RequestedState)) then
        dx, dy = MoveX, MoveY
        speed = not jumping and Crouching and CROUCH_SPEED or (IsKeyDown(Key.Shift) and RUN_SPEED or WALK_SPEED)
        local target = math.deg(math.atan2(dy, dx))
        local difference = (target - Yaw + 180) % 360 - 180
        Yaw = Yaw + difference * (1 - math.exp(-14 * dt))
    elseif action and action.Clip == "Roll" and States[name].Player:IsPlaying() then
        dx, dy, speed = action.RollX, action.RollY, 3.0
    end
    local location = self:GetActorLocation()
    -- XY는 실험용 바닥 범위 내에서 이동하고 Z는 점프 높이를 적용한다. 루트 모션은 사용하지 않는다.
    self:SetActorLocation(FVector.new(
        math.max(-24, math.min(24, location.X + dx * speed * dt)),
        math.max(-24, math.min(24, location.Y + dy * speed * dt)), Home.Z + Jump.Height))
    self:SetActorRotation(FRotator.new(0, Yaw, 0))
end

local function UpdateCamera()
    if not Camera or not Camera:IsValid() then return end
    -- Pawn의 회전을 상쇄해 W=화면 안쪽인 고정 방향 3인칭 시점을 유지한다.
    local yaw = math.rad(Yaw)
    Camera:SetRelativeLocation(FVector.new(-4.5 * math.cos(yaw), 4.5 * math.sin(yaw), 2.45))
    Camera:SetRelativeRotation(FRotator.new(17, -Yaw, 0))
end

local function DrawHUD()
    local name = CurrentState() or "Initializing"
    local action = CurrentAction()
    local label = action and action.Clip or name
    local alpha = Machine:GetActiveTransitionIndex() >= 0 and Machine:GetTransitionElapsedTime() or 0
    local size = math.max(15, math.min(19, GetViewportSize().Y / 42))
    local anchor = FVector2.new(0, 0)
    AddUIImage("Assets/Textures/SkeletalAnimTestHUD.png",
        FVector2.new(math.min(780, GetViewportSize().X - 24), size * (ShowHelp and 15.0 or 4.8)),
        anchor, anchor, FVector2.new(10, 44))
    local function Text(text, line)
        AddUIText(text, size, anchor, anchor, FVector2.new(18, 54 + line * size * 1.4))
    end
    Text("SKELETAL ANIMATION / LUA STATE MACHINE", 0)
    local position = self:GetActorLocation()
    Text(string.format("State: %s   Blend: %.2fs   Position: %.2f, %.2f, %.2f", label, alpha, position.X, position.Y, position.Z), 1)
    Text(string.format("Clip browser %02d/%02d: %s  [Z/X select, V play]", GalleryIndex, #Catalog, Catalog[GalleryIndex]), 2)
    if ShowHelp then
        Text("WASD move | Shift run | C crouch | F jab / Q cross | G/H hit", 4)
        Text("Space jump + WASD air move | R roll | E interact | T dance | B sit/stand | Y talk", 5)
        Text("1 sword | 2 pistol | 3 reload | 4 spell | 5 pickup | 6 fixing", 6)
        Text("7 push | 8 drive | 9 swim | K death | Backspace stop | 0 respawn", 7)
        Text("Tab help | Shift+F1 release cursor | Click viewport to resume", 8)
        Text("M Slot punch | N stop Slot | F punch State (compare playback paths)", 9)
    end
    if label ~= PreviousLabel then
        print("[SkeletalAnimTest] " .. label)
        PreviousLabel = label
    end
end

function BeginPlay()
    Ready = false
    Mesh, Camera = self:GetSkeletalMesh(), self:GetCamera()
    assert(Mesh, "SkeletalAnimTest requires a SkeletalMeshComponent")
    Home, Yaw = self:GetActorLocation(), self:GetActorRotation().Yaw
    Jump.Height, Jump.VelocityZ, Jump.Airborne = 0, 0, false
    Anim = Mesh:CreateAnimInstance()
    BuildGraph()
    LastState, RuntimeAction = nil, nil
    UpdateCamera()
    local manager = GetPlayerCameraManager()
    if manager and Camera then manager:SetViewTargetCamera(Camera) end
    SetCursorMode(CursorMode.Locked)
    Ready = true
    print("[SkeletalAnimTest] Ready: WASD / Shift / F. Z/X/V browses all 43 clips.")
end

function Tick(dt)
    if not Ready or not Anim:IsValid() or not Mesh:IsValid() then return end
    SyncStateEntry()
    if GetCursorMode() == CursorMode.Locked then
        ReadInput()
    else
        Moving, MoveX, MoveY = false, 0, 0
        PreviewLocomotion = nil
        DesiredLocomotion = Crouching and "CrouchIdle" or "Idle"
    end
    -- 커서를 해제해도 중력과 착지 처리는 계속한다.
    UpdateJump(dt)
    UpdateActionChain()
    UpdateMovement(dt)
    UpdateCamera()
    DrawHUD()
end

function EndPlay()
    Ready = false
    if Anim and Anim:IsValid() then Anim:SetRootNode(nil) end
end
