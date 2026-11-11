-- Independent spectator camera. Neither model is possessed or controlled by input.
local Motion = require("Tests.BlendComparisonMotion")
local Camera, Position, Pitch, Yaw

local function ResetCamera()
    Position, Pitch, Yaw = FVector.new(-6.5, -11.0, 5.4), 20.5, 59.4
end

local function ApplyCamera()
    self:SetActorLocation(Position)
    self:SetActorRotation(FRotator.new(Pitch, Yaw, 0))
end

function BeginPlay()
    Camera = self:GetCamera()
    assert(Camera, "BlendComparisonCamera requires a camera")
    ResetCamera()
    ApplyCamera()
    local manager = GetPlayerCameraManager()
    if manager then manager:SetViewTargetCamera(Camera) end
    SetCursorMode(CursorMode.Locked)
end

function Tick(dt)
    if GetCursorMode() == CursorMode.Locked then
        if IsKeyPressed(Key.Home) then ResetCamera() end
        Yaw = Yaw + GetMouseDeltaX() * 0.12
        Pitch = math.max(-85, math.min(85, Pitch + GetMouseDeltaY() * 0.12))
        local y, p = math.rad(Yaw), math.rad(Pitch)
        local forward = (IsKeyDown(Key.W) and 1 or 0) - (IsKeyDown(Key.S) and 1 or 0)
        local right = (IsKeyDown(Key.D) and 1 or 0) - (IsKeyDown(Key.A) and 1 or 0)
        local up = (IsKeyDown(Key.E) and 1 or 0) - (IsKeyDown(Key.Q) and 1 or 0)
        local dx = math.cos(p) * math.cos(y) * forward - math.sin(y) * right
        local dy = math.cos(p) * math.sin(y) * forward + math.cos(y) * right
        local dz = -math.sin(p) * forward + up
        local length = math.sqrt(dx * dx + dy * dy + dz * dz)
        if length > 0 then
            local step = (IsKeyDown(Key.Shift) and 12 or 5) * dt / length
            Position = FVector.new(Position.X + dx * step, Position.Y + dy * step, Position.Z + dz * step)
        end
        ApplyCamera()
    end
    local speed, _, _, phase, direction = Motion.Sample(GetWorldTimeSeconds())
    local size = math.max(12, math.min(19, GetViewportSize().Y / 44))
    local anchor = FVector2.new(0, 0)
    AddUIImage("Assets/Textures/SkeletalAnimTestHUD.png",
        FVector2.new(math.min(GetViewportSize().X - 16, size * 53), size * 8.2),
        anchor, anchor, FVector2.new(8, 38))
    local function Text(value, row)
        AddUIText(value, size, anchor, anchor, FVector2.new(18, 48 + row * size * 1.4))
    end
    Text("LOCOMOTION / TIME BLEND vs SPEED BLEND", 0)
    Text(string.format("%s | %.2f m/s | %s", phase, speed, direction == 1 and "A -> B" or "B -> A"), 1)
    Text("At 2.80 m/s: orange WALK 100% | cyan RUN 85%", 2)
    Text("WASD fly | Q/E down/up | Mouse look | Shift fast | Home reset camera", 3)
    Text("Shift+F1 release cursor | Click viewport to resume | Models move automatically", 4)
end

function OnReload()
    BeginPlay()
end

function EndPlay()
    SetCursorMode(CursorMode.Free)
end
