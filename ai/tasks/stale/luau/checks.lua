--!strict
-- What the task is judged by, which whoever does it does not see. The same
-- checks the Kest side makes, in the same order and with the same numbers. It
-- writes one line: `checks 0` for every check passing.

local stale = require("./stale")

local function world(): stale.World
    return {
        [1] = { name = "oak", worth = 10 },
        [2] = { name = "ash", worth = 4 },
        [3] = { name = "elm", worth = 7 },
    }
end

local function checks(): number
    local all = world()
    if stale.standing(all) ~= 21 then
        return 1
    end

    if stale.fell(all, { { who = 1, by = 3 } }) ~= 0 then
        return 2
    end
    if stale.standing(all) ~= 18 then
        return 3
    end

    if stale.fell(all, { { who = 2, by = 2 }, { who = 2, by = 2 } }) ~= 1 then
        return 4
    end
    if stale.standing(all) ~= 14 then
        return 5
    end

    if stale.fell(all, { { who = 2, by = 5 } }) ~= 0 then
        return 6
    end
    if stale.standing(all) ~= 14 then
        return 7
    end

    local count = 0
    for _ in all do
        count += 1
    end
    if count ~= 2 then
        return 8
    end

    if stale.fell(all, { { who = 1, by = 100 }, { who = 3, by = 100 } }) ~= 2 then
        return 9
    end
    if stale.standing(all) ~= 0 then
        return 10
    end
    return 0
end

print(string.format("checks %d", checks()))
