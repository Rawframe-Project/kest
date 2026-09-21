--!strict
-- A world written down and read back. What each function has to do is in
-- `ask.md`; the shapes and the signatures here are the task's and are not to
-- be changed.

export type Thing = { name: string, worth: number, alive: boolean }

local saved = {}

-- The world, written down.
function saved.written(all: { Thing }): string
    return ""
end

-- And read back, or nothing where the text is not a save this can read.
function saved.read(save: string): { Thing }?
    return nil
end

return saved
