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

-- Advance `values` by however many whole steps `elapsed` pays for, and answer
-- the clock afterwards.
function stepped.advance(values: { number }, clock: Clock,
                         elapsed: number): Clock
    return clock
end

return stepped
