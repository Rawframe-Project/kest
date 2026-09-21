--!strict
-- What the task is judged by, which whoever does it does not see. The same
-- checks the Kest side makes, in the same order and with the same numbers. It
-- writes one line: `checks 0` for every check passing.
--
-- The doors write what they were called with into the log, so what the log
-- holds afterwards is the order the calls were made in.

local called = require("./called")

local function spawned(log: { number }, id: number)
    table.insert(log, id)
end

local function twice(log: { number }, id: number, step: number): boolean
    table.insert(log, 100 + id * 10 + step)
    return step < 3
end

local function never(log: { number }, id: number, step: number): boolean
    table.insert(log, 200 + id * 10 + step)
    return false
end

local function always(log: { number }, id: number, step: number): boolean
    table.insert(log, 300 + id * 10 + step)
    return true
end

local function holds(log: { number }, wanted: { number }): boolean
    if #log ~= #wanted then
        return false
    end
    for i = 1, #log do
        if log[i] ~= wanted[i] then
            return false
        end
    end
    return true
end

local function checks(): number
    local log: { number } = {}
    if called.marched({ spawned = spawned, moved = twice }, log, 2, 4) ~= 0 then
        return 1
    end
    if not holds(log, { 0, 1, 101, 102, 103, 111, 112, 113 }) then
        return 2
    end

    local stuck: { number } = {}
    if called.marched({ spawned = spawned, moved = never }, stuck, 3, 2) ~= 0 then
        return 3
    end
    if not holds(stuck, { 0, 1, 2, 201, 211, 221 }) then
        return 4
    end

    local all: { number } = {}
    if called.marched({ spawned = spawned, moved = always }, all, 2, 3) ~= 2 then
        return 5
    end
    if not holds(all, { 0, 1, 301, 302, 303, 311, 312, 313 }) then
        return 6
    end

    local empty: { number } = {}
    if called.marched({ spawned = spawned, moved = always }, empty, 0, 3) ~= 0 then
        return 7
    end
    if #empty ~= 0 then
        return 8
    end
    local still: { number } = {}
    if called.marched({ spawned = spawned, moved = always }, still, 2, 0) ~= 2 then
        return 9
    end
    if not holds(still, { 0, 1 }) then
        return 10
    end
    return 0
end

print(string.format("checks %d", checks()))
