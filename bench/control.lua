local many = 5000
local rounds = 200

local function decide(state, health, seen, cover)
    if health < 20 then
        if cover > 0 then return 3 end
        return 4
    end
    if state == 0 then
        if seen > 0 then return 1 end
        return 0
    end
    if state == 1 then
        if seen == 0 then return 2 end
        if health > 60 then return 1 end
        return 3
    end
    if state == 2 then
        if seen > 0 then return 1 end
        if cover > 2 then return 0 end
        return 2
    end
    if state == 3 then
        if health > 40 then return 1 end
        return 3
    end
    if seen > 0 and health > 30 then return 1 end
    return 4
end

local state, health = {}, {}
for i = 0, many - 1 do
    state[i] = i % 5
    health[i] = i % 100
end

local taken = 0
for round = 0, rounds - 1 do
    for at = 0, many - 1 do
        local seen = (at + round) % 7 - 3
        local cover = (at * 3 + round) % 5
        local next_ = decide(state[at], health[at], seen, cover)
        state[at] = next_
        health[at] = (health[at] + next_ * 3 + 1) % 100
        taken = taken + next_
    end
end

print(string.format("control %d", taken))
