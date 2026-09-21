--!strict
-- Doors somebody else wrote, handed in rather than required. What each
-- function has to do is in `ask.md`; the shapes and the signatures here are
-- the task's and are not to be changed.

export type Doors = {
    -- Called once for each thing, before any of them moves.
    spawned: ({ number }, number) -> (),
    -- Called for each step of each thing. Answers whether it may go on.
    moved: ({ number }, number, number) -> boolean,
}

local called = {}

-- Drive the doors over `many` things for `steps` steps each, writing what
-- happened into `log`. Answers how many things took all their steps.
function called.marched(doors: Doors, log: { number }, many: number,
                        steps: number): number
    return 0
end

return called
