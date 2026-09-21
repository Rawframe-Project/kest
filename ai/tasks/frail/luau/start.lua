--!strict
-- Lines somebody else wrote, read into records. What each function has to do
-- is in `ask.md`; the shapes and the signatures here are the task's and are
-- not to be changed.

export type Record = { name: string, worth: number }
export type Tally = { got: number, dropped: number, worth: number }

local frail = {}

-- One line read, or nothing where the line is not a record.
function frail.read(line: string): Record?
    return nil
end

-- Every line read, and what came of it.
function frail.readAll(lines: { string }): Tally
    return { got = 0, dropped = 0, worth = 0 }
end

return frail
