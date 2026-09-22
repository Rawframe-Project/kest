--!strict
-- What the task is judged by, which whoever does it does not see. The same
-- checks the Kest side's host makes, in the same order and with the same
-- numbers. It writes one line: `checks 0` for every check passing.
--
-- The engine here is a table the host hands in, which is how a Luau embedder
-- gives a script the API it is allowed to call. Every door writes down what it
-- was called with, so what the log holds afterwards is the order the calls
-- were made in.

local crossing = require("./crossing")

local MOST = 32

type Engine = {
    alive: { boolean },
    handed: number,
    ceiling: number,
    log: { number },
}

local function living(engine: Engine): number
    local many = 0
    for i = 1, MOST do
        if engine.alive[i] then
            many += 1
        end
    end
    return many
end

-- The three doors, over an engine of this shape.
local function doors(engine: Engine)
    return {
        spawn = function(kind: number): number
            table.insert(engine.log, if kind > 0 then kind else 1)
            if living(engine) >= engine.ceiling or engine.handed >= MOST then
                return -1
            end
            local id = engine.handed
            engine.handed += 1
            engine.alive[id + 1] = true
            return id
        end,
        despawn = function(id: number): boolean
            table.insert(engine.log, -(id + 1))
            local was = id >= 0 and id < MOST and engine.alive[id + 1] == true
            if id >= 0 and id < MOST then
                engine.alive[id + 1] = false
            end
            return was
        end,
        alive = function(): number
            table.insert(engine.log, 0)
            return living(engine)
        end,
    }
end

local function world(alive: number, ceiling: number): Engine
    local engine: Engine = { alive = {}, handed = 0, ceiling = ceiling,
                             log = {} }
    for i = 1, MOST do
        engine.alive[i] = false
    end
    for i = 1, alive do
        engine.alive[i] = true
        engine.handed = i
    end
    return engine
end

local function logged(engine: Engine, wanted: { number }): boolean
    if #engine.log ~= #wanted then
        return false
    end
    for i = 1, #wanted do
        if engine.log[i] ~= wanted[i] then
            return false
        end
    end
    return true
end

local function checks(): number
    -- Nothing alive, three wanted, and room for them.
    local engine = world(0, MOST)
    local mine: { number } = {}
    local answered = crossing.settle(doors(engine), 7, mine, 0, 8, 3)
    if not logged(engine, { 0, 7, 7, 7 }) then
        return 2
    end
    if answered ~= 3 or living(engine) ~= 3 then
        return 3
    end
    if mine[1] ~= 0 or mine[2] ~= 1 or mine[3] ~= 2 then
        return 4
    end

    -- Already there: nothing but the one question.
    engine = world(2, MOST)
    mine = { 0, 1 }
    answered = crossing.settle(doors(engine), 7, mine, 2, 8, 2)
    if not logged(engine, { 0 }) then
        return 6
    end
    if answered ~= 2 or living(engine) ~= 2 then
        return 7
    end

    -- Too many: the last one this program is holding goes first.
    engine = world(3, MOST)
    mine = { 0, 1, 2 }
    answered = crossing.settle(doors(engine), 7, mine, 3, 8, 1)
    if not logged(engine, { 0, -3, -2 }) then
        return 9
    end
    if answered ~= 1 or living(engine) ~= 1 then
        return 10
    end

    -- An engine that will not make another: it is asked once and answers that
    -- it made nothing, and asking again would answer the same.
    engine = world(2, 2)
    mine = { 0, 1 }
    answered = crossing.settle(doors(engine), 7, mine, 2, 8, 5)
    if not logged(engine, { 0, 7 }) then
        return 12
    end
    if answered ~= 2 or living(engine) ~= 2 then
        return 13
    end

    -- An id this program is holding that the engine has already lost.
    engine = world(3, MOST)
    engine.alive[3] = false
    mine = { 0, 1, 2 }
    answered = crossing.settle(doors(engine), 7, mine, 3, 8, 1)
    if not logged(engine, { 0, -3, -2 }) then
        return 15
    end
    if living(engine) ~= 1 or answered ~= 1 then
        return 16
    end

    -- And a run with no room left in it.
    engine = world(2, MOST)
    mine = { 0, 1 }
    answered = crossing.settle(doors(engine), 7, mine, 2, 2, 5)
    if not logged(engine, { 0 }) then
        return 18
    end
    if answered ~= 2 or living(engine) ~= 2 then
        return 19
    end
    return 0
end

print("checks " .. tostring(checks()))
