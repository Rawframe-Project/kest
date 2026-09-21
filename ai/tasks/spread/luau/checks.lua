--!strict
-- What the task is judged by, which whoever does it does not see. The same
-- checks the Kest side makes, in the same order and with the same numbers. It
-- writes one line: `checks 0` for every check passing.
--
-- The three pieces are called on their own as well as through `tick`, which
-- is what says the work was moved into them rather than copied beside them.

local spread = require("./spread")

local function one(x: number, y: number, dx: number, dy: number,
                   cool: number): spread.Thing
    return { x = x, y = y, dx = dx, dy = dy, cool = cool, score = 0 }
end

local function near(a: number, b: number): boolean
    local d = a - b
    if d < 0.0 then
        return -d < 0.001
    end
    return d < 0.001
end

local function checks(): number
    local moving = { one(1.0, 2.0, 0.5, -0.25, 3) }
    spread.moved(moving)
    if not near(moving[1].x, 1.5) or not near(moving[1].y, 1.75) then
        return 1
    end
    if moving[1].cool ~= 3 or moving[1].score ~= 0 then
        return 2
    end

    local bouncing = {
        one(-1.0, 5.0, -2.0, 1.0, 0),
        one(11.0, 12.0, 1.0, 1.0, 0),
        one(5.0, 5.0, 1.0, 1.0, 0),
    }
    local many = spread.bounced(bouncing, 10.0)
    if many ~= 3 then
        return 3
    end
    if not near(bouncing[1].x, 1.0) or not near(bouncing[1].dx, 2.0) then
        return 4
    end
    if bouncing[1].score ~= 1 then
        return 5
    end
    if not near(bouncing[2].x, 9.0) or not near(bouncing[2].y, 8.0) then
        return 6
    end
    if bouncing[2].score ~= 2 then
        return 7
    end
    if bouncing[3].score ~= 0 then
        return 8
    end

    local cooling = {
        one(0.0, 0.0, 0.0, 0.0, 2),
        one(0.0, 0.0, 0.0, 0.0, 0),
    }
    spread.cooled(cooling)
    spread.cooled(cooling)
    spread.cooled(cooling)
    if cooling[1].cool ~= 0 or cooling[2].cool ~= 0 then
        return 9
    end

    local world = { one(9.5, 5.0, 1.0, 0.0, 1) }
    local bounces = spread.tick(world, 10.0)
    if bounces ~= 1 then
        return 10
    end
    if not near(world[1].x, 9.5) or not near(world[1].dx, -1.0) then
        return 11
    end
    if world[1].cool ~= 0 or world[1].score ~= 1 then
        return 12
    end

    if spread.tick({}, 10.0) ~= 0 then
        return 13
    end
    return 0
end

print(string.format("checks %d", checks()))
