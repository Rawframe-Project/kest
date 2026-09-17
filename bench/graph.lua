-- The same world in Luau, with the index and the generation written by hand.
local many = 4000
local rounds = 60

local value, nextOf, generation, live = {}, {}, {}, {}
local spare = {}
local stamps = 0
local used = 0

local function add(v, n)
    local at
    if #spare > 0 then
        at = spare[#spare]
        spare[#spare] = nil
    else
        at = used
        used = used + 1
    end
    value[at] = v
    nextOf[at] = n
    stamps = stamps + 1
    generation[at] = stamps
    live[at] = true
    return generation[at] * 16777216 + at
end

local function place(handle)
    local at = handle % 16777216
    local stamp = (handle - at) / 16777216
    if not live[at] or generation[at] ~= stamp then
        return nil
    end
    return at
end

local function remove(handle)
    local at = place(handle)
    if at ~= nil then
        live[at] = false
        spare[#spare + 1] = at
    end
end

local made = {}
for i = 0, many - 1 do
    made[i] = add(i, 0)
end
for i = 0, many - 1 do
    nextOf[place(made[i])] = made[(i + 1) % many]
end

local total = 0
for round = 0, rounds - 1 do
    local at = made[0]
    for _ = 1, many do
        local slot = place(at)
        if slot == nil then return end
        total = total + value[slot]
        at = nextOf[slot]
    end
    if round % 8 == 0 then
        for i = 0, many - 1 do
            if i % 10 == round % 10 then
                local slot = place(made[i])
                if slot ~= nil then
                    local v, n = value[slot], nextOf[slot]
                    remove(made[i])
                    local again = add(v, n)
                    local before = place(made[(i + many - 1) % many])
                    if before ~= nil then
                        nextOf[before] = again
                    end
                    made[i] = again
                end
            end
        end
    end
end

print(string.format("graph %d", total))
