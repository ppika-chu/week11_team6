-- Camera-only spectator; all three demonstrations run without player movement input.
local Camera, Position, Pitch, Yaw

local function ResetCamera()
    Position, Pitch, Yaw = FVector.new(-9, -6.5, 5.8), 22, 36
end

local function ApplyCamera()
    self:SetActorLocation(Position)
    self:SetActorRotation(FRotator.new(Pitch, Yaw, 0))
end

function BeginPlay()
    Camera = self:GetCamera()
    assert(Camera, "JumpAnyStateCamera requires a camera")
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
end

function EndPlay() SetCursorMode(CursorMode.Free) end
function OnReload() BeginPlay() end
