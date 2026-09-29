-- `churn.kest` in the Lua that Lua 5.4, LuaJIT and Luau all read: five
-- thousand things, each with a name and a table of tags, every one moved every
-- frame and some replaced by new ones. See D1273.

local world = {}
local nextid = 0

local function made(id)
    return {
        id = id,
        name = "thing " .. id,
        x = id % 100 + 0.0,
        dx = 1 + id % 3 + 0.0,
        tags = { id, id, id, id },
    }
end

function begin(many)
    world = {}
    for i = 0, many - 1 do
        world[i + 1] = made(i)
    end
    nextid = many
end

function frame(replaced)
    local sum = 0.0
    for at = 1, #world do
        local t = world[at]
        t.x = t.x + t.dx
        if t.x > 100.0 then
            t.x = t.x - 100.0
        end
        sum = sum + t.x + #t.name + t.tags[1] % 7
    end
    for _ = 1, replaced do
        local id = nextid
        nextid = id + 1
        world[id % #world + 1] = made(id)
    end
    return sum
end
