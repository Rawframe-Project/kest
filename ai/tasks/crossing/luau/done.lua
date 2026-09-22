--!strict
-- Doors the engine gives this program, handed in by the host that runs it.
-- What the one function left undone has to do is in `ask.md`; this shape is
-- the task's and is not to be changed.

export type Engine = {
    -- Make one thing of a kind. Answers the id it was given, or a number
    -- below nought where the engine would not make one.
    spawn: (number) -> number,
    -- Take one away by id. Answers whether there was one there to take.
    despawn: (number) -> boolean,
    -- How many are alive now.
    alive: () -> number,
}

local crossing = {}

-- Bring the world to `wanted` alive things, keeping the engine's own run of
-- ids in step with it, and answer how many ids are in it afterwards.
function crossing.settle(engine: Engine, kind: number, mine: { number },
                         many: number, room: number, wanted: number): number
    local held = many
    local alive = engine.alive()
    while alive < wanted do
        -- The engine lent this run, so what is left in it is the ceiling on
        -- what this can take on.
        if held == room then
            return held
        end
        local id = engine.spawn(kind)
        -- A full engine answers that it made nothing, and asking again
        -- answers the same thing.
        if id < 0 then
            return held
        end
        mine[held + 1] = id
        held += 1
        alive += 1
    end
    while alive > wanted and held > 0 do
        held -= 1
        -- And this answer says whether there was one to take: an id this
        -- program is holding that the engine has already lost is not one
        -- alive fewer.
        if engine.despawn(mine[held + 1]) then
            alive -= 1
        end
    end
    return held
end

return crossing
