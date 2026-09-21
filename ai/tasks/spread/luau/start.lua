--!strict
-- A frame of a game, written as one function that does three things. What
-- each function has to do is in `ask.md`; the shapes and the signatures here
-- are the task's and are not to be changed.

export type Thing = {
    x: number, y: number,
    dx: number, dy: number,
    cool: number, score: number,
}

local spread = {}

-- Everything moves by its own speed.
function spread.moved(all: { Thing })
end

-- Anything that has gone outside the board comes back in, turns round, and
-- takes a point for it. Answers how many bounces there were.
function spread.bounced(all: { Thing }, wide: number): number
    return 0
end

-- Every cooldown counts down by one, and never below nought.
function spread.cooled(all: { Thing })
end

-- One frame, which is the three of them in that order. Answers how many
-- bounces there were.
function spread.tick(all: { Thing }, wide: number): number
    local many = 0
    for i = 1, #all do
        local one = all[i]
        one.x += one.dx
        one.y += one.dy
        if one.x < 0.0 then
            one.x = -one.x
            one.dx = -one.dx
            one.score += 1
            many += 1
        end
        if one.x > wide then
            one.x = wide - (one.x - wide)
            one.dx = -one.dx
            one.score += 1
            many += 1
        end
        if one.y < 0.0 then
            one.y = -one.y
            one.dy = -one.dy
            one.score += 1
            many += 1
        end
        if one.y > wide then
            one.y = wide - (one.y - wide)
            one.dy = -one.dy
            one.score += 1
            many += 1
        end
        if one.cool > 0 then
            one.cool -= 1
        end
    end
    return many
end

return spread
