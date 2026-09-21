--!strict
-- Lines somebody else wrote, read into records. What each function has to do
-- is in `ask.md`; the shapes and the signatures here are the task's and are
-- not to be changed.

export type Record = { name: string, worth: number }
export type Tally = { got: number, dropped: number, worth: number }

local frail = {}

local function split(line: string, by: string): { string }
    local pieces = {}
    local at = 1
    while true do
        local found = string.find(line, by, at, true)
        if found == nil then
            table.insert(pieces, string.sub(line, at))
            break
        end
        table.insert(pieces, string.sub(line, at, found - 1))
        at = found + #by
    end
    return pieces
end

local function whole(digits: string): number?
    if not string.match(digits, "^%-?%d+$") then
        return nil
    end
    local value = tonumber(digits)
    if value == nil or value > 2147483647 or value < -2147483648 then
        return nil
    end
    return value
end

-- One line read, or nothing where the line is not a record.
function frail.read(line: string): Record?
    local pieces = split(line, ";")
    if #pieces < 2 then
        return nil
    end
    if string.sub(pieces[1], 1, 5) ~= "name=" then
        return nil
    end
    if string.sub(pieces[2], 1, 6) ~= "worth=" then
        return nil
    end
    local name = string.sub(pieces[1], 6)
    if #name == 0 then
        return nil
    end
    local worth = whole(string.sub(pieces[2], 7))
    if worth == nil then
        return nil
    end
    return { name = name, worth = worth }
end

-- Every line read, and what came of it.
function frail.readAll(lines: { string }): Tally
    local got = 0
    local dropped = 0
    local worth = 0
    for i = 1, #lines do
        local one = frail.read(lines[i])
        if one ~= nil then
            got += 1
            worth += one.worth
        else
            dropped += 1
        end
    end
    return { got = got, dropped = dropped, worth = worth }
end

return frail
