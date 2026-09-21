--!strict
-- A world written down and read back. What each function has to do is in
-- `ask.md`; the shapes and the signatures here are the task's and are not to
-- be changed.

export type Thing = { name: string, worth: number, alive: boolean }

local saved = {}

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

-- The world, written down.
function saved.written(all: { Thing }): string
    local pieces = { "v1" }
    for i = 1, #all do
        local one = all[i]
        table.insert(pieces, string.format("%s,%d,%d", one.name, one.worth,
                                           if one.alive then 1 else 0))
    end
    return table.concat(pieces, "|")
end

-- And read back, or nothing where the text is not a save this can read.
function saved.read(save: string): { Thing }?
    local pieces = split(save, "|")
    if #pieces == 0 or pieces[1] ~= "v1" then
        return nil
    end
    local all: { Thing } = {}
    for i = 2, #pieces do
        local fields = split(pieces[i], ",")
        if #fields < 3 then
            return nil
        end
        if #fields[1] == 0 then
            return nil
        end
        local worth = whole(fields[2])
        if worth == nil then
            return nil
        end
        if fields[3] == "1" then
            table.insert(all, { name = fields[1], worth = worth, alive = true })
        elseif fields[3] == "0" then
            table.insert(all, { name = fields[1], worth = worth, alive = false })
        else
            return nil
        end
    end
    return all
end

return saved
