--!strict
-- A world advanced in whole steps of a fixed size. What the function has to do
-- is in `ask.md`; the shapes, the names and the two numbers here are the
-- task's and are not to be changed.

export type Clock = {
    -- Time handed in that has not been spent on a whole step yet.
    left: number,
    -- How many whole steps this world has taken.
    steps: number,
}

local stepped = {}

-- One step is twenty milliseconds, and no one call runs more than five.
stepped.STEP = 20
stepped.MOST = 5

-- One step, written into the world as it walks it -- so a thing late in the
-- run reads what this step has already given the thing before it.
local function stepOnce(values: { number })
    local many = #values
    for i = 1, many do
        values[i] = values[i] + values[i % many + 1]
    end
end

-- Advance `values` by however many whole steps `elapsed` pays for, and answer
-- the clock afterwards.
function stepped.advance(values: { number }, clock: Clock,
                         elapsed: number): Clock
    local left = clock.left + elapsed
    local steps = clock.steps
    local ran = 0
    while left >= stepped.STEP and ran < stepped.MOST do
        stepOnce(values)
        left -= stepped.STEP
        steps += 1
        ran += 1
    end
    if ran == stepped.MOST then
        left = 0
    end
    return { left = left, steps = steps }
end

return stepped
