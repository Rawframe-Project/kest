--!strict
-- What the task is judged by, which whoever does it does not see. The same
-- checks the Kest side makes, in the same order and with the same numbers. It
-- writes one line: `checks 0` for every check passing.
--
-- The pool writes down every slot it hands out, so what `handed` holds
-- afterwards is what was taken, and what `free` holds is whether it came back.

local pooled = require("./pooled")

local function pool(): pooled.Pool
    return { free = { 1, 2, 3 }, handed = {} }
end

local function holds(values: { number }, wanted: { number }): boolean
    if #values ~= #wanted then
        return false
    end
    for i = 1, #values do
        if values[i] ~= wanted[i] then
            return false
        end
    end
    return true
end

local function checks(): number
    -- Two out of three, newest first, and the pool keeps the rest.
    local p = pool()
    local into: { number } = {}
    if pooled.refill(p, 2, into) ~= 2 then
        return 1
    end
    if not holds(into, { 3, 2 }) then
        return 2
    end
    if not holds(p.free, { 1 }) then
        return 3
    end
    if not holds(p.handed, { 3, 2 }) then
        return 4
    end

    -- The last one, and `into` is what this call took and not what the last
    -- one did.
    if pooled.refill(p, 1, into) ~= 1 then
        return 5
    end
    if not holds(into, { 1 }) then
        return 6
    end
    if #p.free ~= 0 then
        return 7
    end

    -- And nothing left to take.
    if pooled.refill(p, 1, into) ~= 0 then
        return 8
    end
    if #into ~= 0 then
        return 9
    end
    if #p.free ~= 0 then
        return 10
    end
    if not holds(p.handed, { 3, 2, 1 }) then
        return 11
    end

    -- More than there is: none of it is kept, and the pool ends holding what
    -- it held, in the order it held it.
    local short = pool()
    local some: { number } = {}
    if pooled.refill(short, 5, some) ~= 0 then
        return 12
    end
    if #some ~= 0 then
        return 13
    end
    if not holds(short.free, { 1, 2, 3 }) then
        return 14
    end
    -- The takes happened, so the pool wrote them down.
    if not holds(short.handed, { 3, 2, 1 }) then
        return 15
    end
    -- And a pool that gave everything back gives it out again.
    if pooled.refill(short, 3, some) ~= 3 then
        return 16
    end
    if not holds(some, { 3, 2, 1 }) then
        return 17
    end

    -- Nothing asked for takes nothing, and empties what it was handed.
    local quiet = pool()
    local held: { number } = {}
    if pooled.refill(quiet, 2, held) ~= 2 then
        return 18
    end
    if pooled.refill(quiet, 0, held) ~= 0 then
        return 19
    end
    if #held ~= 0 then
        return 20
    end
    if not holds(quiet.free, { 1 }) then
        return 21
    end
    if pooled.refill(quiet, -3, held) ~= 0 then
        return 22
    end
    if #held ~= 0 or #quiet.free ~= 1 then
        return 23
    end

    -- The whole pool at once.
    local all = pool()
    local every: { number } = {}
    if pooled.refill(all, 3, every) ~= 3 then
        return 24
    end
    if not holds(every, { 3, 2, 1 }) then
        return 25
    end
    if #all.free ~= 0 then
        return 26
    end
    return 0
end

print("checks " .. tostring(checks()))
