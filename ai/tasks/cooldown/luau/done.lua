--!strict
export type Ability = { name: string, cools: number, left: number }
export type Actor = { name: string, energy: number, abilities: { Ability } }

local cooldown = {}

function cooldown.tick(who: Actor, by: number): Actor
    for at = 1, #who.abilities do
        local one = who.abilities[at]
        if one.left > by then
            one.left -= by
        else
            one.left = 0
        end
    end
    return who
end

function cooldown.ready(who: Actor, which: number, costs: number): boolean
    if which < 1 or which > #who.abilities then
        return false
    end
    return who.abilities[which].left == 0 and who.energy >= costs
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
