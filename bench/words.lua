local many = 2000
local rounds = 20

local found = 0
for round = 0, rounds - 1 do
    local pieces = {}
    for i = 0, many - 1 do
        pieces[i + 1] = string.format("item %d of %d in round %d", i, many,
                                      round)
    end
    local whole = table.concat(pieces, ",")
    if string.find(whole, "item 1999 ", 1, true) then
        found = found + 1
    end
    local back = {}
    local from = 1
    while true do
        local at = string.find(whole, ",", from, true)
        if at == nil then
            back[#back + 1] = string.sub(whole, from)
            break
        end
        back[#back + 1] = string.sub(whole, from, at - 1)
        from = at + 1
    end
    if #back ~= many then return end
    for _, one in ipairs(back) do
        if string.sub(one, 1, 7) == "item 1 " then
            found = found + 1
        end
    end
    found = found + (#whole % 7)
end

print(string.format("words %d", found))
