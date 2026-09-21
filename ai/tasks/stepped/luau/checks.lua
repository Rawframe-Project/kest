--!strict
-- What the task is judged by, which whoever does it does not see. The same
-- checks the Kest side makes, in the same order and with the same numbers. It
-- writes one line: `checks 0` for every check passing.
--
-- What this holds is the one thing a promise cannot: that the same elapsed
-- time, split into frames any way at all, gives the same world.

local stepped = require("./stepped")

local function world(a: number, b: number, c: number): { number }
    return { a, b, c }
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
    -- A hundred milliseconds in one call: five whole steps and nothing left.
    local once = world(1, 2, 3)
    local after = stepped.advance(once, { left = 0, steps = 0 }, 100)
    if after.steps ~= 5 or after.left ~= 0 then
        return 1
    end
    if not holds(once, { 64, 63, 65 }) then
        return 2
    end

    -- The same hundred as five frames of twenty.
    local evenly = world(1, 2, 3)
    local clock = { left = 0, steps = 0 }
    for _ = 1, 5 do
        clock = stepped.advance(evenly, clock, 20)
    end
    if clock.steps ~= 5 or clock.left ~= 0 then
        return 3
    end
    if not holds(evenly, { 64, 63, 65 }) then
        return 4
    end

    -- And as five uneven ones adding to the same hundred.
    local unevenly = world(1, 2, 3)
    local uneven = { left = 0, steps = 0 }
    for _, one in { 7, 13, 19, 21, 40 } do
        uneven = stepped.advance(unevenly, uneven, one)
    end
    if uneven.steps ~= 5 or uneven.left ~= 0 then
        return 5
    end
    if not holds(unevenly, { 64, 63, 65 }) then
        return 6
    end

    -- One step of [1, 2, 3] on its own, which is what the ring is about: a
    -- world written into as it is walked gives [3, 5, 6] here.
    local one = world(1, 2, 3)
    if stepped.advance(one, { left = 0, steps = 0 }, 20).steps ~= 1 then
        return 7
    end
    if not holds(one, { 3, 5, 4 }) then
        return 8
    end

    -- Time that does not pay for a step is held for the next call.
    local carried = world(1, 2, 3)
    local held = stepped.advance(carried, { left = 0, steps = 0 }, 30)
    if held.steps ~= 1 or held.left ~= 10 then
        return 9
    end
    held = stepped.advance(carried, held, 10)
    if held.steps ~= 2 or held.left ~= 0 then
        return 10
    end
    if not holds(carried, { 8, 9, 7 }) then
        return 11
    end
    local still = stepped.advance(carried, held, 19)
    if still.steps ~= 2 or still.left ~= 19 then
        return 12
    end
    if not holds(carried, { 8, 9, 7 }) then
        return 13
    end

    -- Five steps is all one call runs, and the time the sixth would have cost
    -- is dropped rather than carried into the call after it.
    local late = world(1, 2, 3)
    local limit = stepped.advance(late, { left = 0, steps = 0 }, 1000)
    if limit.steps ~= 5 then
        return 14
    end
    if limit.left ~= 0 then
        return 15
    end
    if not holds(late, { 64, 63, 65 }) then
        return 16
    end
    limit = stepped.advance(late, limit, 0)
    if limit.steps ~= 5 then
        return 17
    end

    -- A world with nothing in it still keeps time.
    local empty: { number } = {}
    local quiet = stepped.advance(empty, { left = 0, steps = 0 }, 45)
    if quiet.steps ~= 2 or quiet.left ~= 5 then
        return 18
    end
    if #empty ~= 0 then
        return 19
    end

    -- And a world of one thing, whose neighbour is itself.
    local alone = { 7 }
    if stepped.advance(alone, { left = 0, steps = 0 }, 60).steps ~= 3 then
        return 20
    end
    if alone[1] ~= 56 then
        return 21
    end
    return 0
end

print("checks " .. tostring(checks()))
