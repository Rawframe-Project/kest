--!strict
-- A world of things, and a round that takes some of them out. What each
-- function has to do is in `ask.md`; the shapes and the signatures here are
-- the task's and are not to be changed.

export type Thing = { name: string, worth: number }
export type Hit = { who: number, by: number }
export type World = { [number]: Thing }

local stale = {}

-- Every hit takes its worth off what it names. A thing at nought or less goes
-- out of the world. Answers how many went.
function stale.fell(world: World, hits: { Hit }): number
    return 0
end

-- What everything still standing is worth, added up.
function stale.standing(world: World): number
    return 0
end

return stale
