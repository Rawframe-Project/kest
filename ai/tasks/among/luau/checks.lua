--!strict
-- What the task is judged by, which whoever does it does not see. The same
-- checks the Kest side makes, in the same order and with the same numbers. It
-- writes one line: `checks 0` for every check passing.
--
-- Every one of the three is asked of two sets that share nothing -- numbers
-- and strings -- because one body for any set is the whole of what this task
-- is about, and a body written for one of them passes half of this.

local among = require("./among")

local function big(n: number): boolean
    return n > 2
end

local function never(n: number): boolean
    return false
end

local function long(t: string): boolean
    return #t > 2
end

local function checks(): number
    local numbers = { 1, 5, 2, 9, 3 }
    local words = { "a", "abc", "bb", "abcd" }

    -- The first, and it is the first rather than any of the others.
    local one = among.firstThat(numbers, big)
    if one == nil then
        return 2
    elseif one ~= 5 then
        return 1
    end
    local word = among.firstThat(words, long)
    if word == nil then
        return 4
    elseif word ~= "abc" then
        return 3
    end

    -- And nothing, which is what there is rather than a value of the set.
    if among.firstThat(numbers, never) ~= nil then
        return 5
    end
    if among.firstThat({} :: { number }, big) ~= nil then
        return 6
    end

    -- How many, over both sets and over none.
    if among.howMany(numbers, big) ~= 3 then
        return 7
    end
    if among.howMany(words, long) ~= 2 then
        return 8
    end
    if among.howMany(numbers, never) ~= 0 then
        return 9
    end

    -- Every one, in the order they are in.
    local four: { number } = { 0, 0, 0, 0 }
    if among.allThat(numbers, big, four, 4) ~= 3 then
        return 10
    end
    if four[1] ~= 5 or four[2] ~= 9 or four[3] ~= 3 then
        return 11
    end
    local three: { string } = { "", "", "" }
    if among.allThat(words, long, three, 3) ~= 2 then
        return 12
    end
    if three[1] ~= "abc" or three[2] ~= "abcd" then
        return 13
    end

    -- And a run with room for fewer than there are.
    local two: { number } = { 0, 0 }
    if among.allThat(numbers, big, two, 2) ~= 2 then
        return 14
    end
    if two[1] ~= 5 or two[2] ~= 9 then
        return 15
    end
    if among.allThat(numbers, big, {} :: { number }, 0) ~= 0 then
        return 16
    end
    return 0
end

print("checks " .. tostring(checks()))
