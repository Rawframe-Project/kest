--!strict
-- The same plausible wrong answer as the Kest one: the cooldown is taken down
-- without stopping at nought, and `ready` reads the ability before asking
-- whether there is one.
export type Ability = { name: string, cools: number, left: number }
export type Actor = { name: string, energy: number, abilities: { Ability } }

local cooldown = {}

function cooldown.tick(who: Actor, by: number): Actor
    for at = 1, #who.abilities do
        who.abilities[at].left -= by
    end
    return who
end

function cooldown.ready(who: Actor, which: number, costs: number): boolean
    if which > #who.abilities then
        return false
    end
    return who.abilities[which].left <= 0 and who.energy >= costs
end

function cooldown.spend(who: Actor, which: number, costs: number): Actor
    if not cooldown.ready(who, which, costs) then
        return who
    end
    who.energy -= costs
    who.abilities[which].left = who.abilities[which].cools
    return who
end

return cooldown
