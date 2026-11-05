-- 1인칭 투명인간. WASD로 걷고(Shift 달리기) 마우스로 둘러보며, 지나온 길에 시간이 지나면 사라지는 발자국을 남긴다.
-- 커서 해제(Shift+F1)와 재진입(뷰포트 클릭), 종료(Esc)는 에디터가 처리한다. Pawn의 ScriptComponent에 붙이고, 카메라 컴포넌트를 루트 아래에 둔다.

local WALK_SPEED = 2.4
local RUN_SPEED = 4.5
local MOUSE_SENSITIVITY = 0.12 -- 픽셀당 각도
local MAX_PITCH = 85.0
local EYE_HEIGHT = 0.7         -- 캡슐 중심에서 눈까지

local STEP_LENGTH = 0.85       -- 이만큼 걸을 때마다 한 발
local FOOT_OFFSET = 0.17       -- 몸 중심에서 발까지의 옆 거리
local TOE_OUT = 7.0            -- 발끝이 바깥으로 벌어지는 각도
local FLOOR_Z = 0.0
local FOOTPRINT_SIZE = FVector.new(0.1, 0.16, 0.32) -- 투영 깊이, 반 폭, 반 길이 (텍스처 비율 1:2)

local FADE_IN = 0.08           -- 찍히는 시간
local HOLD = 3.0               -- 선명하게 남는 시간
local FADE_OUT = 4.0           -- 사라지는 시간

local Pitch, Yaw = 0.0, 0.0
local Walked = 0.0
local bLeftFoot = true
local Camera = nil

local function SetMouseLocked(bLocked)
    SetCursorMode(bLocked and CursorMode.Locked or CursorMode.Free)
end

local function StampFootprint(Location)
    local Rad = math.rad(Yaw)
    local Side = bLeftFoot and -FOOT_OFFSET or FOOT_OFFSET
    local FootLocation = FVector.new(
        Location.X - math.sin(Rad) * Side,
        Location.Y + math.cos(Rad) * Side,
        FLOOR_Z)

    -- 텍스처는 왼발이다. 데칼 상자는 로컬 X 양쪽으로 투영되므로
    -- Pitch -90, Yaw +180으로 뒤집으면 발끝 방향은 같고 좌우만 뒤집혀 오른발이 된다.
    local Rotation = bLeftFoot
        and FRotator.new(90.0, Yaw - TOE_OUT, 0.0)
        or FRotator.new(-90.0, Yaw + TOE_OUT + 180.0, 0.0)

    SpawnDecal({
        Location = FootLocation,
        Rotation = Rotation,
        Size = FOOTPRINT_SIZE,
        Texture = "Assets/Textures/T_Footprint.png",
        NormalTexture = "Assets/Textures/T_Footprint_N.png",
        SpecularFactor = 0.1,
        FadeInDuration = FADE_IN,
        FadeStartDelay = FADE_IN + HOLD,
        FadeDuration = FADE_OUT,
    })
    bLeftFoot = not bLeftFoot
end

local function UpdateLook()
    Yaw = Yaw + GetMouseDeltaX() * MOUSE_SENSITIVITY
    Pitch = math.max(-MAX_PITCH, math.min(MAX_PITCH, Pitch + GetMouseDeltaY() * MOUSE_SENSITIVITY))
    if Camera then
        Camera:SetRelativeRotation(FRotator.new(Pitch, 0.0, 0.0))
    end
end

local function UpdateMove(DeltaTime)
    local Forward = (IsKeyDown(Key.W) and 1.0 or 0.0) - (IsKeyDown(Key.S) and 1.0 or 0.0)
    local Right = (IsKeyDown(Key.D) and 1.0 or 0.0) - (IsKeyDown(Key.A) and 1.0 or 0.0)
    local Rotation = FRotator.new(0.0, Yaw, 0.0)
    local Before = self:GetActorLocation()

    local Length = math.sqrt(Forward * Forward + Right * Right)
    if Length <= 0.0 then
        self:SetActorRotation(Rotation)
        return
    end

    -- WM_KEYDOWN은 좌우 구분 없이 VK_SHIFT로 들어오므로 Key.LShift로는 잡히지 않는다.
    local Speed = IsKeyDown(Key.Shift) and RUN_SPEED or WALK_SPEED
    local Scale = Speed * DeltaTime / Length
    local Rad = math.rad(Yaw)
    local DeltaX = (math.cos(Rad) * Forward - math.sin(Rad) * Right) * Scale
    local DeltaY = (math.sin(Rad) * Forward + math.cos(Rad) * Right) * Scale

    -- 축을 나눠 움직여 벽에 막혀도 벽을 따라 미끄러지게 한다.
    self:Move(FVector.new(DeltaX, 0.0, 0.0), Rotation)
    self:Move(FVector.new(0.0, DeltaY, 0.0), Rotation)

    -- 벽에 막혀 제자리걸음일 때는 발자국을 찍지 않도록 실제로 움직인 거리만 센다.
    local After = self:GetActorLocation()
    local DX, DY = After.X - Before.X, After.Y - Before.Y
    Walked = Walked + math.sqrt(DX * DX + DY * DY)
    if Walked >= STEP_LENGTH then
        Walked = Walked - STEP_LENGTH
        StampFootprint(After)
    end
end

function BeginPlay()
    Yaw = self:GetActorRotation().Yaw
    Camera = self:GetCamera()
    if Camera then
        Camera:SetRelativeLocation(FVector.new(0.0, 0.0, EYE_HEIGHT))
    else
        print("[FootprintPlayer] 카메라 컴포넌트가 없습니다.\n")
    end
    SetMouseLocked(true)
end

function Tick(DeltaTime)
    -- 커서를 꺼낸 동안에는 툴바로 가는 마우스에 시점이 돌지 않게 입력을 무시한다.
    if GetCursorMode() == CursorMode.Locked then
        UpdateLook()
        UpdateMove(DeltaTime)
    end
end

function EndPlay()
    SetMouseLocked(false)
end
