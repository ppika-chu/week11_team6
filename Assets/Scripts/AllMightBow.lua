-- 올마이트가 배꼽 앞에 손을 모으고 인사한다.
-- FRotator.new(Pitch, Yaw, Roll)

local BOW_ANGLE = 35.0 -- 허리를 숙이는 각도
local CYCLE = 5.0      -- 인사 한 번의 길이 (초)

local Time = 0.0

function BeginPlay()
    -- 팔을 내리고 팔꿈치를 굽혀 손을 배꼽 앞에 모은다
    self:SetBoneRotation("L_arm", FRotator.new(82.0, 46.0, 0.0))
    self:SetBoneRotation("L_arm_EX", FRotator.new(82.0, 46.0, 0.0))
    self:SetBoneRotation("R_arm", FRotator.new(-82.0, -46.0, 0.0))
    self:SetBoneRotation("R_arm_EX", FRotator.new(-82.0, -46.0, 0.0))
    self:SetBoneRotation("L_elbow", FRotator.new(0.0, 90.0, 0.0))
    self:SetBoneRotation("R_elbow", FRotator.new(0.0, -90.0, 0.0))
end

function Tick(DeltaTime)
    Time = Time + DeltaTime

    -- 0에서 1로 갔다가 다시 0으로 돌아오는 값
    local Bow = (1.0 - math.cos(Time / CYCLE * math.pi * 2.0)) * 0.5

    self:SetBoneRotation("waist", FRotator.new(0.0, 0.0, -BOW_ANGLE * Bow))
    self:SetBoneRotation("head", FRotator.new(0.0, 0.0, -10.0 * Bow))
end
