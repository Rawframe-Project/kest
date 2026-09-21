--!strict
export type Thing = { name: string, worth: number }
export type Hit = { who: number, by: number }
export type World = { [number]: Thing }

local stale = {}

function stale.fell(world: World, hits: { Hit }): number
    local gone = 0
    for _, hit in hits do
        -- A hit may name something an earlier hit took out, so what is there
        -- is asked rather than assumed.
        local one = world[hit.who]
        if one ~= nil then
            one.worth -= hit.by
            if one.worth <= 0 then
                world[hit.who] = nil
                gone += 1
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
