--!strict
-- Abilities that cool down. What each function has to do is in `ask.md`; the
-- shapes and the signatures here are the task's and are not to be changed.

export type Ability = { name: string, cools: number, left: number }
export type Actor = { name: string, energy: number, abilities: { Ability } }

local cooldown = {}

-- Every ability counts down by `by`, and a cooldown never goes below nought.
function cooldown.tick(who: Actor, by: number): Actor
    return who
end

-- Whether the ability at `which` can be used now. One-based, the way a table
-- in this language is.
function cooldown.ready(who: Actor, which: number, costs: number): boolean
    return false
end

-- Use it if it is ready, and answer the actor either way.
function cooldown.spend(who: Actor, which: number, costs: number): Actor
    return who
end

return cooldown
