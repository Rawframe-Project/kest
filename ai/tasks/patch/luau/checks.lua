--!strict
-- What the task is judged by, which whoever does it does not see. The same
-- checks the Kest side makes, in the same order and with the same numbers. It
-- writes one line: `checks 0` for every check passing.

local patch = require("./patch")

local function overfull(bag: patch.Bag): boolean
    for i = 1, #bag do
        if bag[i].many > 20 or bag[i].many < 1 then
            return true
        end
    end
    return false
end

local function checks(): number
    local bag: patch.Bag = {}

    if patch.held(bag, "ore") ~= 0 then
        return 1
    end

    if patch.put(bag, 4, "ore", 5) ~= 0 then
        return 2
    end
    if patch.held(bag, "ore") ~= 5 or #bag ~= 1 then
        return 3
    end

    if patch.put(bag, 4, "ore", 30) ~= 0 then
        return 4
    end
    if patch.held(bag, "ore") ~= 35 then
        return 5
    end
    if #bag ~= 2 then
        return 6
    end
    if overfull(bag) then
        return 7
    end

    if patch.put(bag, 4, "wood", 3) ~= 0 then
        return 8
    end
    if patch.held(bag, "ore") ~= 35 or patch.held(bag, "wood") ~= 3 then
        return 9
    end

    if patch.put(bag, 4, "ore", 30) ~= 5 then
        return 10
    end
    if patch.held(bag, "ore") ~= 60 then
        return 11
    end
    if overfull(bag) then
        return 12
    end

    if patch.put(bag, 4, "ore", 0) ~= 0 then
        return 13
    end
    if #bag ~= 4 or patch.held(bag, "ore") ~= 60 then
        return 14
    end

    if patch.put(bag, 4, "iron", 2) ~= 2 then
        return 15
    end
    if patch.held(bag, "iron") ~= 0 then
        return 16
    end
    return 0
end

print(string.format("checks %d", checks()))
