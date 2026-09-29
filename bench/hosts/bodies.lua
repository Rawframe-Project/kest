-- `bodies.kest` in the Lua that Lua 5.4, LuaJIT and Luau all read: one body
-- moved, a frame of them, and a program asking its host `times` times. The
-- frame is over wherever the engine keeps bodies fastest. LuaJIT's FFI reaches
-- the host's own run of doubles, so its frame works on the host's memory the
-- way Kest's lend does. Lua 5.4 cannot reach it, and Luau can only through a
-- `buffer`, which was measured at four times the cost of its own tables (166
-- against 42 ns a body); so for both of them the bodies are the program's own
-- tables, filled once, and the host hands over nothing but the call. That is
-- the cheaper frame -- nothing is written back to the host -- and it is theirs.
-- See D1272.

local function moved(x, y, dx, dy, wall)
    x = x + dx
    y = y + dy
    if x < 0 then
        x = -x
        dx = -dx
    elseif x > wall then
        x = wall - (x - wall)
        dx = -dx
    end
    if y < 0 then
        y = -y
        dy = -dy
    elseif y > wall then
        y = wall - (y - wall)
        dy = -dy
    end
    return x, y, dx, dy
end

-- A crossing a body: four numbers and the wall in, four numbers out.
function one(x, y, dx, dy, wall)
    return moved(x, y, dx, dy, wall)
end

-- The bodies as the program's own tables, for an engine that cannot reach the
-- host's memory: made once, from the same numbers the host fills its run with.
local world = {}
function fill(many)
    world = {}
    for i = 0, many - 1 do
        -- `+ 0.0` because Lua 5.4 keeps whole numbers whole, and the work is
        -- the same arithmetic on doubles in every engine.
        world[i + 1] = {
            x = i % 100 + 0.0,
            y = (i * 3) % 100 + 0.0,
            dx = 1 + i % 3 + 0.0,
            dy = 2.0,
        }
    end
end

-- A frame with one crossing, over the tables.
function step(wall)
    local sum = 0
    for i = 1, #world do
        local b = world[i]
        local x, y, dx, dy = moved(b.x, b.y, b.dx, b.dy, wall)
        b.x, b.y, b.dx, b.dy = x, y, dx, dy
        sum = sum + x + y
    end
    return sum
end

-- The same frame over the host's own run of doubles, where the language can
-- reach it: LuaJIT through its FFI, given the address.
if jit then
    local ffi = require("ffi")
    ffi.cdef("typedef struct { double x, y, dx, dy; } Body;")
    local cast = ffi.cast
    function steplent(at, many, wall)
        local run = cast("Body *", at)
        local sum = 0
        for i = 0, many - 1 do
            local b = run[i]
            local x, y, dx, dy = moved(b.x, b.y, b.dx, b.dy, wall)
            b.x, b.y, b.dx, b.dy = x, y, dx, dy
            sum = sum + x + y
        end
        return sum
    end
end

-- The other direction: `times` calls of the host.
function asks(times)
    local sum = 0
    local add = add
    for at = 0, times - 1 do
        sum = add(sum, at)
    end
    return sum
end
