--!strict
-- What the task is judged by, which whoever does it does not see. The same
-- checks the Kest side makes, in the same order and with the same numbers. It
-- writes one line: `checks 0` for every check passing.

local saved = require("./saved")

local function same(a: { saved.Thing }, b: { saved.Thing }): boolean
    if #a ~= #b then
        return false
    end
    for i = 1, #a do
        if a[i].name ~= b[i].name or a[i].worth ~= b[i].worth
                or a[i].alive ~= b[i].alive then
            return false
        end
    end
    return true
end

local function readsNothing(save: string): boolean
    return saved.read(save) == nil
end

local function checks(): number
    local world: { saved.Thing } = {
        { name = "oak", worth = 10, alive = true },
        { name = "ash", worth = -4, alive = false },
        { name = "elm", worth = 0, alive = true },
    }

    local back = saved.read(saved.written(world))
    if back == nil then
        return 2
    end
    if not same(back, world) then
        return 1
    end

    local whole = saved.written(world)
    local again = saved.read(whole)
    if again == nil then
        return 4
    end
    if saved.written(again) ~= whole then
        return 3
    end

    local empty = saved.read(saved.written({}))
    if empty == nil then
        return 6
    end
    if #empty ~= 0 then
        return 5
    end

    if not readsNothing("v2|oak,10,1") then
        return 7
    end
    if not readsNothing("") then
        return 8
    end
    if not readsNothing("oak,10,1") then
        return 9
    end

    if not readsNothing("v1|oak,10") then
        return 10
    end
    if not readsNothing("v1|oak,10,1,extra") then
        return 11
    end

    if not readsNothing("v1|oak,ten,1") then
        return 12
    end
    if not readsNothing("v1|oak,10,yes") then
        return 13
    end
    if not readsNothing("v1|oak,10,2") then
        return 14
    end

    if not readsNothing("v1|,10,1") then
        return 15
    end

    local only = saved.read("v1")
    if only == nil then
        return 17
    end
    if #only ~= 0 then
        return 16
    end
    return 0
end

print(string.format("checks %d", checks()))
