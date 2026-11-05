local Runtime = {
    mOrigin = false,
    mElapsed = 0.0,
    mRotationSpeed = 120.0,
    mBobSpeed = 2.0,
    mBobHeight = 0.35,
    mPhase = 0.0,
}

local function ConfigureFromName()
    local Name = self:GetName()
    if Name == "TimeDemoSpinnerLeft" then
        Runtime.mRotationSpeed = 95.0
        Runtime.mBobSpeed = 1.5
        Runtime.mBobHeight = 0.3
        Runtime.mPhase = 0.0
    elseif Name == "TimeDemoSpinnerRight" then
        Runtime.mRotationSpeed = -135.0
        Runtime.mBobSpeed = 2.1
        Runtime.mBobHeight = 0.42
        Runtime.mPhase = 2.1
    else
        Runtime.mRotationSpeed = 165.0
        Runtime.mBobSpeed = 1.8
        Runtime.mBobHeight = 0.5
        Runtime.mPhase = 4.2
    end
end

local function Initialize()
    Runtime.mOrigin = self:GetActorLocation()
    Runtime.mElapsed = 0.0
    ConfigureFromName()
end

function BeginPlay()
    Initialize()
end

function Tick(DeltaTime)
    -- DeltaTime은 Global Time Dilation이 반영된 값이므로 슬로모와 히트 스톱을 그대로 시각화한다.
    Runtime.mElapsed = Runtime.mElapsed + DeltaTime

    local Bob = math.sin(Runtime.mElapsed * Runtime.mBobSpeed + Runtime.mPhase) * Runtime.mBobHeight
    local Roll = Runtime.mElapsed * Runtime.mRotationSpeed

    self:SetActorLocation(FVector.new(
        Runtime.mOrigin.X,
        Runtime.mOrigin.Y,
        Runtime.mOrigin.Z + Bob))
    self:SetActorRotation(FRotator.new(0.0, 0.0, Roll))
end

function EndPlay()
    if Runtime.mOrigin then
        self:SetActorLocation(Runtime.mOrigin)
        self:SetActorRotation(FRotator.new(0.0, 0.0, 0.0))
    end
end

function OnReload()
    Initialize()
end
