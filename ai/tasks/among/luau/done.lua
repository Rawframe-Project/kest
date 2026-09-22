--!strict
-- One body, any set. What each function has to do is in `ask.md`; the shapes
-- and the signatures here are the task's and are not to be changed.

local among = {}

-- The first of `xs` that `keeps` answers yes to, or nothing.
function among.firstThat<T>(xs: { T }, keeps: (T) -> boolean): T?
    for _, one in xs do
        if keeps(one) then
            return one
        end
    end
    -- Nothing is what there is, rather than a value of `T` this body cannot
    -- make and the caller could not tell from an answer.
    return nil
end

-- How many of `xs` it answers yes to.
function among.howMany<T>(xs: { T }, keeps: (T) -> boolean): number
    local many = 0
    for _, one in xs do
        if keeps(one) then
            many += 1
        end
    end
    return many
end

-- Every one it answers yes to, written into `into` in the order they are in
-- `xs`, and how many were written. `into` is a run somebody else owns: it has
-- `room` places and may have room for fewer than there are.
function among.allThat<T>(xs: { T }, keeps: (T) -> boolean, into: { T },
                          room: number): number
    local put = 0
    for _, one in xs do
        if keeps(one) then
            if put == room then
                return put
            end
            into[put + 1] = one
            put += 1
        end
    end
    return put
end

return among
