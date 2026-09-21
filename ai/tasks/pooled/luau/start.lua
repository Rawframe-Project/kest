--!strict
-- A pool of slots somebody else wrote. What the one function left undone has
-- to do is in `ask.md`; the shapes and the signatures here are the task's and
-- are not to be changed.

export type Pool = {
    -- Slots free to be handed out. The next one out is the last one in.
    free: { number },
    -- Every slot handed out since the pool was made, in the order it went out.
    handed: { number },
}

local pooled = {}

-- Take one slot, or nothing where there are none left. Not yours to write.
function pooled.take(pool: Pool): number?
    local slot = table.remove(pool.free)
    if slot == nil then
        return nil
    end
    table.insert(pool.handed, slot)
    return slot
end

-- Give one back, so that it is the next one out again. Not yours to write.
function pooled.give(pool: Pool, slot: number)
    table.insert(pool.free, slot)
end

-- Fill `into` with `want` slots out of the pool, all of them or none of them.
function pooled.refill(pool: Pool, want: number, into: { number }): number
    return 0
end

return pooled
