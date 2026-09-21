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

-- One step: every thing takes on its own value plus the one the thing after it
-- around the ring held at the start of the step, so what is read is what the
-- step began with and never what this step has already written.
local function stepOnce(values: { number })
    local many = #values
    if many == 0 then
        return
    end
    local was = table.create(many)
    for i = 1, many do
        was[i] = values[i]
    end
    for i = 1, many do
        values[i] = was[i] + was[i % many + 1]
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
        -- Time five steps could not pay for is dropped rather than carried,
        -- or a frame that arrived late makes every frame after it later.
        left = 0
    end
    return { left = left, steps = steps }
end

return stepped
