-- 공 Actor의 ScriptComponent에 연결한다. 재생 시간 기준 5초 후 삭제한다.
local LifetimeSeconds = 5.0
local ElapsedSeconds = 0.0

function BeginPlay()
    ElapsedSeconds = 0.0
end

function Tick(DeltaTime)
    ElapsedSeconds = ElapsedSeconds + DeltaTime
    if ElapsedSeconds >= LifetimeSeconds then
        self:Destroy()
    end
end
