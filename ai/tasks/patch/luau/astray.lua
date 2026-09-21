--!strict
-- A bag that holds stacks of things. What each function has to do is in
-- `ask.md`; the shapes and the signatures here are the task's and are not to
-- be changed.

export type Stack = { kind: string, many: number }
export type Bag = { Stack }

-- The most of one kind that fits in a stack.
local MOST = 20

local patch = {}

patch.MOST = MOST

-- How many of `kind` the bag holds altogether.
function patch.held(bag: Bag, kind: string): number
    local sum = 0
    for i = 1, #bag do
        if bag[i].kind == kind then
            sum += bag[i].many
        end
    end
    return sum
end

-- Put `many` of `kind` in, and answer how many would not go.
function patch.put(bag: Bag, room: number, kind: string, many: number): number
    local left = many
    for i = 1, #bag do
        if left == 0 then
            return 0
        end
        if bag[i].kind == kind and bag[i].many < MOST then
            local space = MOST - bag[i].many
            local fits = if space < left then space else left
            bag[i].many += fits
            left -= fits
        end
    end
    while left > 0 and #bag < room do
        local fits = if left < MOST then left else MOST
        table.insert(bag, { kind = kind, many = fits })
        left -= MOST
    end
    return left
end

return patch
