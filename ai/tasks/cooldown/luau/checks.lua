--!strict
-- What the task is judged by, which whoever does it does not see. The same
-- checks the Kest side makes, in the same order and with the same numbers, so
-- that what a run says about one language says it about the other. It writes
-- one line: `checks 0` for every check passing.

local cooldown = require("./cooldown")

local function three(): cooldown.Actor
    return {
        name = "who",
        energy = 10,
        abilities = {
            { name = "swing", cools = 3, left = 0 },
            { name = "shout", cools = 5, left = 2 },
            { name = "heal", cools = 7, left = 7 },
        },
    }
end

local function checks(): number
    local who = cooldown.tick(three(), 1)
    if who.abilities[1].left ~= 0 then
        return 1
    end
    if who.abilities[2].left ~= 1 then
        return 2
    end
    if who.abilities[3].left ~= 6 then
        return 3
    end

    who = cooldown.tick(who, 100)
    if who.abilities[2].left ~= 0 or who.abilities[3].left ~= 0 then
        return 4
    end

    if who.energy ~= 10 or #who.abilities ~= 3 then
        return 5
    end
    if who.abilities[1].cools ~= 3 or who.abilities[3].cools ~= 7 then
        return 6
    end

    local same = cooldown.tick(three(), 0)
    if same.abilities[2].left ~= 2 then
        return 7
    end

    local ours = three()
    if not cooldown.ready(ours, 1, 10) then
        return 8
    end
    if cooldown.ready(ours, 1, 11) then
        return 9
    end
    if cooldown.ready(ours, 2, 1) then
        return 10
    end

    if cooldown.ready(ours, 4, 1) then
        return 11
    end
    if cooldown.ready(ours, 0, 1) then
        return 12
    end

    local after = cooldown.spend(three(), 1, 4)
    if after.energy ~= 6 then
        return 13
    end
    if after.abilities[1].left ~= 3 then
        return 14
    end
    if after.abilities[2].left ~= 2 then
        return 15
    end

    local no = cooldown.spend(three(), 2, 1)
    if no.energy ~= 10 or no.abilities[2].left ~= 2 then
        return 16
    end
    local past = cooldown.spend(three(), 10, 1)
    if past.energy ~= 10 then
        return 17
    end

    local poor = three()
    poor.energy = 2
    local tried = cooldown.spend(poor, 1, 5)
    if tried.energy ~= 2 then
        return 18
    end
    return 0
end

print(string.format("checks %d", checks()))
