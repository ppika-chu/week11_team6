-- Assets/Scenes/Tests/KirbyAnimDemo.scene의 KirbyPlayer에 연결한다.
-- 기존 importer 출력: Assets/Imported/Kirby (Skeletal Combine / Import Animations 켜짐).
-- SequencePlayer -> StateResult -> StateMachine -> Slot -> Root -> AnimInstance 최종 pose.
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
local Mesh, Camera, Anim, Machine, Slot
local States, Names = {}, {}
local Home, Yaw, VelocityZ = nil, 0, 0
local MoveX, MoveY, Moving, Running, Flying, Sitting = 0, 0, false, false, false, false
local Grounded, Landing, GroundContact, CeilingContact = true, false, false, false
local DesiredState, GalleryIndex, SlotLabel = "Idle", 1, "None"
local HitName, HitTimer, Ready, ShowHelp = "None", 0, false, true

local function ClipPath(name) return PREFIX .. name .. ".fbxbin" end
local function CurrentState() return Names[Machine:GetCurrentStateIndex()] end

local function BuildGraph()
    States, Names = {}, {}
    Machine = Anim:CreateNode("StateMachine")
    for _, name in ipairs(STATE_ORDER) do
        local player = Anim:CreateNode("SequencePlayer")
        player:SetSequencePlayer(ClipPath(STATE_CLIPS[name]))
        assert(player:GetPlayLength() > 0, "Kirby animation is empty: " .. name)
        player:SetLooping(name ~= "Sit" and name ~= "Land")
        -- Sit은 마지막 앉은 자세를 유지하고, 착지는 짧게 재생한다.
        if name == "Land" then player:SetPlayRate(player:GetPlayLength() / 0.35) end
        local result = Anim:CreateNode("StateResult")
        result:SetInputNode(player)
        local state = Anim:CreateState()
        state:SetStateResult(result)
        local index = Machine:AddState(state)
        assert(index >= 0, "Kirby state registration failed: " .. name)
        States[name], Names[index] = { Index = index, Player = player }, name
    end
    -- 전이 조건에는 부수 효과가 없다. 목표 상태는 충돌 이동 결과로 프레임마다 결정한다.
    for _, from in ipairs(STATE_ORDER) do
        for _, to in ipairs(STATE_ORDER) do
            if from ~= to then
                local target = to
                local transition = Anim:CreateTransition()
                transition:SetFromStateIndex(States[from].Index)
                transition:SetToStateIndex(States[target].Index)
                transition:SetBlendDuration((target == "Fly" or target == "Fall") and 0.10 or 0.16)
                transition:SetConditionFunction(function() return DesiredState == target end)
                assert(Machine:AddTransition(transition) >= 0, "Kirby transition registration failed")
            end
        end
    end
    assert(Machine:SetInitialStateIndex(States.Idle.Index))
    Slot = Anim:CreateNode("Slot")
    Slot:SetSlotName("KirbyFullBody")
    Slot:SetInputNode(Machine)
    local root = Anim:CreateNode("Root")
    root:SetInputNode(Slot)
    Anim:SetRootNode(root)
end

local function PlaySlot(clip)
    -- Slot은 전신 override이며, 기본 StateMachine의 시간과 전이는 계속 진행된다.
    Slot:PlayMontage(ClipPath(clip), 1.0, 0.12, 0.20, false)
    SlotLabel = clip
end

local function ResetPlayer()
    Slot:StopMontage(0)
    self:SetActorLocation(FVector.new(Home.X, Home.Y, Home.Z))
    VelocityZ, Grounded, Landing, Sitting = 0, true, false, false
    MoveX, MoveY, Moving, Running, Flying = 0, 0, false, false, false
    DesiredState, SlotLabel, HitName, HitTimer = "Idle", "None", "None", 0
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
    if IsKeyPressed(Key.F) then PlaySlot("Land-SaluteForward") end
    if IsKeyPressed(Key.Q) then PlaySlot("Land-SaluteLeft") end
    if IsKeyPressed(Key.E) then PlaySlot("Land-SaluteRight") end
    if IsKeyPressed(Key.N) then Slot:StopMontage(0.20) end
    if IsKeyPressed(Key.Z) then GalleryIndex = (GalleryIndex - 2) % #CATALOG + 1 end
    if IsKeyPressed(Key.X) then GalleryIndex = GalleryIndex % #CATALOG + 1 end
    if IsKeyPressed(Key.V) then PlaySlot(CATALOG[GalleryIndex]) end
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
    if not Slot:IsMontagePlaying() then SlotLabel = "None" end
    local anchor = FVector2.new(0, 0)
    local size = math.max(14, math.min(18, GetViewportSize().Y / 45))
    AddUIImage("Assets/Textures/SkeletalAnimTestHUD.png", FVector2.new(760, size * (ShowHelp and 12.5 or 5.8)),
        anchor, anchor, FVector2.new(10, 44))
    local function Text(value, line)
        AddUIText(value, size, anchor, anchor, FVector2.new(20, 54 + line * size * 1.4))
    end
    Text("KIRBY / STATE MACHINE + SLOT", 0)
    Text(string.format("State: %s   Slot: %s   Weight: %.2f", CurrentState() or "Idle", SlotLabel, Slot:GetMontageWeight()), 1)
    Text(string.format("Grounded: %s   Last obstacle: %s", tostring(Grounded), HitTimer > 0 and HitName or "None"), 2)
    if ShowHelp then
        Text("WASD move | Shift run | Hold Space fly / release to fall | C sit", 4)
        Text("F / Q / E salute in Slot | N blend Slot out | R reset", 5)
        Text(string.format("Z / X select clip | V play in Slot [%02d/%02d: %s]", GalleryIndex, #CATALOG, CATALOG[GalleryIndex]), 6)
        Text("Tab help | Shift+F1 release cursor | Click viewport to resume", 7)
    end
end

function BeginPlay()
    Ready = false
    Mesh, Camera = self:GetSkeletalMesh(), self:GetCamera()
    assert(Mesh and Camera, "KirbyAnimDemo requires a SkeletalMesh and Camera")
    Home, Yaw = self:GetActorLocation(), self:GetActorRotation().Yaw
    VelocityZ, Grounded, Landing, Sitting, DesiredState = 0, true, false, false, "Idle"
    Anim = Mesh:CreateAnimInstance()
    BuildGraph()
    UpdateCamera()
    local manager = GetPlayerCameraManager()
    if manager then manager:SetViewTargetCamera(Camera) end
    SetCursorMode(CursorMode.Locked)
    Ready = true
    print("[KirbyAnimDemo] Ready: StateMachine -> KirbyFullBody Slot -> Root -> AnimInstance")
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
        Slot:StopMontage(0)
        Anim:SetRootNode(nil)
    end
    SetCursorMode(CursorMode.Free)
end

function OnReload() BeginPlay() end
