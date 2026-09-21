--!strict
-- A scoreboard read out of lines somebody else wrote. What each function has
-- to do is in `ask.md`; the shapes and the signatures here are the task's and
-- are not to be changed.

export type Board = { [string]: number }

local nearby = {}

-- Every line that is a name and a whole number, added up by name.
function nearby.tally(lines: { string }): Board
    local board: Board = {}
    return board
end

-- The greatest score on the board, by name, or nothing where it is empty.
function nearby.best(board: Board): string?
    return nil
end

return nearby
