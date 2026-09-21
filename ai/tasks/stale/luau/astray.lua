--!strict
-- The same plausible wrong answer as the Kest one: what is left is worked out
-- where the thing may not be there, so a hit that names something an earlier
-- hit took out is taken out again and counted again. Here the nought comes
-- from a nil rather than from a scope, and nothing says so.
export type Thing = { name: string, worth: number }
export type Hit = { who: number, by: number }
export type World = { [number]: Thing }

local stale = {}

function stale.fell(world: World, hits: { Hit }): number
    local gone = 0
    for _, hit in hits do
        local one = world[hit.who]
        local left = 0
        if one ~= nil then
            left = one.worth - hit.by
        end
        if left <= 0 then
            world[hit.who] = nil
            gone += 1
        else
            if one ~= nil then
                one.worth = left
            end
        end
    end
    return gone
end

function stale.standing(world: World): number
    local total = 0
    for _, one in world do
        total += one.worth
    end
    return total
end

return stale
