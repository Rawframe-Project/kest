--!strict
-- What the task is judged by, which whoever does it does not see. The same
-- checks the Kest side makes, in the same order and with the same numbers. It
-- writes one line: `checks 0` for every check passing.

local frail = require("./frail")

local function readsAs(line: string, name: string, worth: number): boolean
    local r = frail.read(line)
    return r ~= nil and r.name == name and r.worth == worth
end

local function readsNothing(line: string): boolean
    return frail.read(line) == nil
end

local function checks(): number
    if not readsAs("name=oak;worth=10", "oak", 10) then
        return 1
    end

    if not readsAs("name=ash;worth=-4", "ash", -4) then
        return 2
    end
    if not readsAs("name=elm;worth=0", "elm", 0) then
        return 3
    end

    if not readsNothing("") then
        return 4
    end
    if not readsNothing("name=oak") then
        return 5
    end
    if not readsNothing("worth=10") then
        return 6
    end

    if not readsNothing("worth=10;name=oak") then
        return 7
    end
    if not readsNothing("name=oak;worth=10;extra=1") then
        return 8
    end

    if not readsNothing("name=oak;worth=ten") then
        return 9
    end
    if not readsNothing("name=oak;worth=") then
        return 10
    end
    if not readsNothing("name=oak;worth=2147483648") then
        return 11
    end

    if not readsNothing("name=;worth=1") then
        return 12
    end
    if not readsAs("name=an oak;worth=1", "an oak", 1) then
        return 13
    end

    local t = frail.readAll({
        "name=oak;worth=10",
        "nonsense",
        "name=ash;worth=-4",
        "name=elm;worth=ten",
        "name=yew;worth=0",
    })
    if t.got ~= 3 then
        return 14
    end
    if t.dropped ~= 2 then
        return 15
    end
    if t.worth ~= 6 then
        return 16
    end

    local empty = frail.readAll({})
    if empty.got ~= 0 or empty.dropped ~= 0 or empty.worth ~= 0 then
        return 17
    end
    return 0
end

print(string.format("checks %d", checks()))
