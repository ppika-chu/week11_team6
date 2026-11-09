-- 스켈레탈 메시 애니메이션 재생 확인용 스크립트.
-- 액터의 메시를 Details에서 애니메이션과 같은 스켈레톤의 메시로 고른 뒤 PIE로 실행한다.
--   예) 메시 Imported/Dancing/SK_Dancing_Hips.fbxbin + 아래 ANIMATION_PATH
--
-- 키
--   1      처음부터 반복 재생 (PlayAnimation)
--   2      정지 (Stop)
--   3      이어서 재생 (Play)
--   4      한 번만 재생 (PlayAnimation, bLooping = false)
--   Left   0.5초 뒤로 (SetPosition)
--   Right  0.5초 앞으로 (SetPosition)
--   Up     재생 속도 x2 (SetPlayRate)
--   Down   재생 속도 x0.5 (SetPlayRate)
--   Num0   재생 속도 1로 되돌림
--   L      현재 상태 출력

local ANIMATION_PATH = "Imported/Dancing/ANIM_Dancing_Anim.fbxbin"
local SEEK_STEP = 0.5
local LOG_INTERVAL = 1.0 -- 재생 중 상태를 자동으로 출력하는 간격 (초). 0이면 끔

local Mesh = nil
local LogElapsed = 0.0

local function PrintState(Label)
    print(string.format("[AnimTest][%s] Playing=%s Position=%.3f PlayRate=%.2f",
        Label, tostring(Mesh:IsPlaying()), Mesh:GetPosition(), Mesh:GetPlayRate()))
end

local function Play(bLooping)
    if Mesh:PlayAnimation(ANIMATION_PATH, bLooping) then
        PrintState(bLooping and "PlayAnimation(Loop)" or "PlayAnimation(Once)")
    else
        -- 실패 이유(에셋 없음 / 스켈레톤 불일치)는 Output Log의 [Lua] 경고에 나온다
        print("[AnimTest] PlayAnimation failed: " .. ANIMATION_PATH)
    end
end

function BeginPlay()
    Mesh = self:GetSkeletalMesh()
    if not Mesh then
        print("[AnimTest] This actor has no SkeletalMeshComponent")
        return
    end
    Play(true)
end

function Tick(DeltaTime)
    if not Mesh or not Mesh:IsValid() then
        return
    end

    if IsKeyPressed(Key.Num1) then Play(true) end
    if IsKeyPressed(Key.Num4) then Play(false) end
    if IsKeyPressed(Key.Num2) then
        Mesh:Stop()
        PrintState("Stop")
    end
    if IsKeyPressed(Key.Num3) then
        Mesh:Play(true)
        PrintState("Play")
    end
    if IsKeyPressed(Key.Left) then
        Mesh:SetPosition(Mesh:GetPosition() - SEEK_STEP)
        PrintState("SetPosition -")
    end
    if IsKeyPressed(Key.Right) then
        Mesh:SetPosition(Mesh:GetPosition() + SEEK_STEP)
        PrintState("SetPosition +")
    end
    if IsKeyPressed(Key.Up) then
        Mesh:SetPlayRate(Mesh:GetPlayRate() * 2.0)
        PrintState("SetPlayRate x2")
    end
    if IsKeyPressed(Key.Down) then
        Mesh:SetPlayRate(Mesh:GetPlayRate() * 0.5)
        PrintState("SetPlayRate x0.5")
    end
    if IsKeyPressed(Key.Num0) then
        Mesh:SetPlayRate(1.0)
        PrintState("SetPlayRate 1")
    end
    if IsKeyPressed(Key.L) then
        PrintState("State")
    end

    if LOG_INTERVAL > 0.0 and Mesh:IsPlaying() then
        LogElapsed = LogElapsed + DeltaTime
        if LogElapsed >= LOG_INTERVAL then
            LogElapsed = 0.0
            PrintState("Tick")
        end
    end
end

function EndPlay()
    if Mesh and Mesh:IsValid() then
        Mesh:Stop()
    end
end
