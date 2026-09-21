--!strict
-- A scoreboard read out of lines somebody else wrote. What each function has
-- to do is in `ask.md`; the shapes and the signatures here are the task's and
-- are not to be changed.

export type Board = { [string]: number }

local nearby = {}

local function trimmed(piece: string): string
    return (string.match(piece, "^%s*(.-)%s*$")) or ""
end

-- Every line that is a name and a whole number, added up by name.
function nearby.tally(lines: { string }): Board
    local board: Board = {}
    for _, line in lines do
        local pieces = string.split(line, ":")
        if #pieces ~= 2 then
            continue
        end
        local name = trimmed(pieces[1])
        if name == "" then
            continue
        end
        -- `tonumber` and nothing else: it says nothing about the number it
        -- read having been a whole one.
        local worth = tonumber(trimmed(pieces[2]))
        if worth == nil then
            continue
        end
        board[name] = (board[name] or 0) + worth
    end
    return board
end

-- The greatest score on the board, by name, or nothing where it is empty.
function nearby.best(board: Board): string?
    local name: string? = nil
    local worth = 0
    for one, score in board do
        if name == nil or score > worth or (score == worth and one < name) then
            name = one
            worth = score
        end
    end
    return name
end

return nearby
