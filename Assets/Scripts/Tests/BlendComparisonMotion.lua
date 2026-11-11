-- Shared, analytic speed/distance curve: both lanes remain exactly side by side.
local M = { WalkSpeed = 0.5, RunSpeed = 3.2, RunEntry = 3.0, BlendDuration = 0.45 }
M.Stages = {
    { 1.5, 0.0, 0.0, "IDLE / TURN" },
    { 0.8, 0.0, 0.5, "START WALK" },
    { 1.0, 0.5, 0.5, "WALK" },
    { 3.0, 0.5, 2.8, "ACCELERATE" },
    { 1.6, 2.8, 2.8, "COMPARE: FAST WALK vs RUN BLEND" },
    { 0.6, 2.8, 3.2, "ENTER RUN" },
    { 0.7, 3.2, 3.2, "RUN" },
    { 1.5, 3.2, 0.0, "BRAKE" },
}
M.Duration, M.Distance = 0, 0
for _, stage in ipairs(M.Stages) do
    M.Duration = M.Duration + stage[1]
    M.Distance = M.Distance + stage[1] * (stage[2] + stage[3]) * 0.5
end

function M.Sample(time)
    time = math.max(0, time)
    local leg = math.floor(time / M.Duration)
    local t, distance = time % M.Duration, 0
    local direction = leg % 2 == 0 and 1 or -1
    for _, stage in ipairs(M.Stages) do
        local duration, startSpeed, endSpeed = stage[1], stage[2], stage[3]
        if t < duration then
            local acceleration = (endSpeed - startSpeed) / duration
            distance = distance + startSpeed * t + 0.5 * acceleration * t * t
            local x = direction * (distance - M.Distance * 0.5)
            -- Turn only while stopped at an endpoint. This keeps feet from sliding sideways.
            local yaw = direction == 1 and 0 or 180
            if stage == M.Stages[1] and leg > 0 then
                local alpha = math.min(1, t / 0.75)
                alpha = alpha * alpha * (3 - 2 * alpha)
                yaw = (direction == 1 and 180 or 0) + 180 * alpha
            end
            return startSpeed + acceleration * t, x, yaw, stage[4], direction
        end
        distance = distance + duration * (startSpeed + endSpeed) * 0.5
        t = t - duration
    end
end

function M.RunWeight(speed)
    return math.max(0, math.min(1, (speed - M.WalkSpeed) / (M.RunSpeed - M.WalkSpeed)))
end

return M
