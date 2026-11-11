-- Orange lane: SequencePlayer states, blended only for transition duration.
-- Cyan lane: Idle -> Locomotion, with walk/run in ONE real BlendSpacePlayer.
local Motion = require("Tests.BlendComparisonMotion")
local PREFIX = "Imported/UAL1_Standard/ANIM_UAL1_Standard_Armature_"
local Anim, Machine, Locomotion, Home, Continuous
local States, Names, Edges = {}, {}, {}
local Speed, Ready = 0, false

local function Clip(name) return PREFIX .. name .. ".fbxbin" end

local function Sequence(name)
    local player = Anim:CreateNode("SequencePlayer")
    player:SetSequencePlayer(Clip(name))
    player:SetLooping(true)
    return player
end

local function AddState(name, player)
    local result = Anim:CreateNode("StateResult")
    result:SetInputNode(player)
    local state = Anim:CreateState()
    state:SetStateResult(result)
    local index = Machine:AddState(state)
    assert(index >= 0, "Cannot add state: " .. name)
    States[name], Names[index] = index, name
end

local function Transition(from, to, condition)
    local transition = Anim:CreateTransition()
    transition:SetFromStateIndex(States[from])
    transition:SetToStateIndex(States[to])
    transition:SetBlendDuration(Motion.BlendDuration)
    transition:SetConditionFunction(condition)
    local index = Machine:AddTransition(transition)
    assert(index >= 0, "Cannot add transition")
    Edges[index] = { From = from, To = to }
end

local function BuildGraph()
    States, Names, Edges = {}, {}, {}
    Machine = Anim:CreateNode("StateMachine")
    AddState("Idle", Sequence("Idle_Loop"))
    if Continuous then
        Locomotion = Anim:CreateNode("BlendSpacePlayer")
        assert(Locomotion:SetBlendSpace1D(0, Motion.RunSpeed, {
            { Sequence = Clip("Walk_Loop"), Value = Motion.WalkSpeed },
            { Sequence = Clip("Sprint_Loop"), Value = Motion.RunSpeed },
        }))
        Locomotion:SetLoop(true)
        Locomotion:SetPosition(FVector.new(0, 0, 0))
        AddState("Locomotion", Locomotion)
        Transition("Idle", "Locomotion", function() return Speed >= 0.08 end)
        Transition("Locomotion", "Idle", function() return Speed < 0.08 end)
    else
        AddState("Walk", Sequence("Walk_Loop"))
        AddState("Run", Sequence("Sprint_Loop"))
        -- Keep the walk clip at 1x even close to run entry: speed is NOT a pose weight.
        Transition("Idle", "Walk", function() return Speed >= 0.08 end)
        Transition("Walk", "Run", function() return Speed >= Motion.RunEntry end)
        Transition("Run", "Walk", function() return Speed < Motion.RunEntry end)
        Transition("Walk", "Idle", function() return Speed < 0.08 end)
    end
    assert(Machine:SetInitialStateIndex(States.Idle))
    local root = Anim:CreateNode("Root")
    root:SetInputNode(Machine)
    Anim:SetRootNode(root)
end

local function DrawHUD()
    local viewport = GetViewportSize()
    local size = math.max(12, math.min(18, viewport.Y / 48))
    local anchor = FVector2.new(Continuous and 1 or 0, 1)
    AddUIImage("Assets/Textures/SkeletalAnimTestHUD.png",
        FVector2.new(math.min(viewport.X * 0.48, size * 32), size * 8.2),
        anchor, anchor, FVector2.new(Continuous and -8 or 8, -8))
    local function Text(value, row)
        AddUIText(value, size, anchor, anchor,
            FVector2.new(Continuous and -18 or 18, -20 - (4 - row) * size * 1.45))
    end
    Text(Continuous and "B / CYAN - BLEND SPACE PLAYER" or "A / ORANGE - STATE TRANSITIONS", 0)
    Text(string.format("Speed %.2f m/s | State: %s", Speed,
        Names[Machine:GetCurrentStateIndex()] or "Idle"), 1)
    local edge = Edges[Machine:GetActiveTransitionIndex()]
    if edge then
        local alpha = math.min(1, Machine:GetTransitionElapsedTime() / Motion.BlendDuration)
        Text(string.format("%s -> %s | %.0f%% / %.2fs", edge.From, edge.To,
            alpha * 100, Motion.BlendDuration), 2)
    else
        Text("Transition: inactive", 2)
    end
    if Continuous then
        local weight = Motion.RunWeight(Speed)
        Text(string.format("Locomotion samples: Walk %.0f%% / Run %.0f%%",
            (1 - weight) * 100, weight * 100), 3)
        Text("Pose weights follow SPEED continuously", 4)
    else
        Text(string.format("Walk sample %.1f | Run entry %.1f m/s", Motion.WalkSpeed, Motion.RunEntry), 3)
        Text("Pose blends only during 0.45s TRANSITIONS", 4)
    end
end

function BeginPlay()
    Ready = false
    Home = self:GetActorLocation()
    Continuous = self:GetName():find("Continuous", 1, true) ~= nil
    local mesh = self:GetSkeletalMesh()
    assert(mesh, "BlendComparisonModel requires a skeletal mesh")
    Anim = mesh:CreateAnimInstance()
    Speed = Motion.Sample(GetWorldTimeSeconds())
    BuildGraph()
    Ready = true
end

function Tick(dt)
    if not Ready or not Anim:IsValid() then return end
    local x, yaw
    Speed, x, yaw = Motion.Sample(GetWorldTimeSeconds())
    self:SetActorLocation(FVector.new(x, Home.Y, Home.Z))
    self:SetActorRotation(FRotator.new(0, yaw, 0))
    if Continuous then Locomotion:SetPosition(FVector.new(Speed, 0, 0)) end
    DrawHUD()
end

function EndPlay()
    Ready = false
    if Anim and Anim:IsValid() then Anim:SetRootNode(nil) end
    Anim, Machine, Locomotion = nil, nil, nil
    States, Names, Edges = {}, {}, {}
end

function OnReload()
    EndPlay()
    BeginPlay()
end
