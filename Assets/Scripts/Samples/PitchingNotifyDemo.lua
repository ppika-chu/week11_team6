-- 투수 Actor에 ScriptComponent 하나로 연결한다.
-- 비교: Default Anim Instance / Custom UPitchingAnimInstance. 두 Actor 모두 같은 Sequence 사용.
-- Viewer에서 이 Sequence에 Named Notify "ReleaseBall"을 배치하고 저장한다.
local SequencePath = "Imported/Baseball Pitching/ANIM_Baseball Pitching_Anim.fbxbin"

-- 서 있는 투수의 Yaw 기준 오프셋. 본 추적 없이 놓는 위치를 맞추는 시연 설정이다.
local ReleaseForward = 0.5
local ReleaseRight = 0.3
local ReleaseHeight = 1.5
local ThrowPitch = 0.0
local ThrowYawOffset = 0.0
local Anim

function BeginPlay()
    Anim = self:GetSkeletalMesh():CreateAnimInstance()
    local Player = Anim:CreateNode("SequencePlayer")
    Player:SetSequencePlayer(SequencePath)
    Player:SetLooping(true)
    local Root = Anim:CreateNode("Root")
    Root:SetInputNode(Player)
    Anim:SetRootNode(Root)
end

function OnAnimNotify(NotifyName)
    if NotifyName ~= "ReleaseBall" then return end

    local Rotation = self:GetActorRotation()
    local Yaw = math.rad(Rotation.Yaw)
    local Forward = FVector(math.cos(Yaw), math.sin(Yaw), 0)
    local Right = FVector(-math.sin(Yaw), math.cos(Yaw), 0)
    local Location = self:GetActorLocation() + Forward * ReleaseForward + Right * ReleaseRight + FVector(0, 0, ReleaseHeight)
    local Ball = SpawnActor("ABaseballActor", Location, FRotator(ThrowPitch, Rotation.Yaw + ThrowYawOffset, 0))
    if Ball then
        print("[Pitching Notify]", self:GetName(), NotifyName, "-> Baseball spawned")
    end
end

function EndPlay()
    if Anim and Anim:IsValid() then Anim:SetRootNode(nil) end
    Anim = nil
end
