--!strict
-- What the task is judged by, which whoever does it does not see. The same
-- checks the Kest side makes, in the same order and with the same numbers. It
-- writes one line: `checks 0` for every check passing.
--
-- What most of these hold is that the right one of two names that are nearly
-- the same was called.

local nearby = require("./nearby")

local function counted(board: { [string]: number }): number
    local many = 0
    for _ in board do
        many += 1
    end
    return many
end

local function worth(board: { [string]: number }, name: string): number
    return board[name] or -999
end

local function named(got: string?, wanted: string): boolean
    return got ~= nil and got == wanted
end

local function checks(): number
    -- A plain board, and the greatest on it.
    local plain = nearby.tally({ "gil:12", "ada:5", "rex:9" })
    if counted(plain) ~= 3 then
        return 1
    end
    if worth(plain, "gil") ~= 12 or worth(plain, "ada") ~= 5
            or worth(plain, "rex") ~= 9 then
        return 2
    end
    if not named(nearby.best(plain), "gil") then
        return 3
    end

    -- A name that turns up three times adds up.
    local again = nearby.tally({ "gil:12", "gil:5", "gil:1" })
    if counted(again) ~= 1 then
        return 4
    end
    if worth(again, "gil") ~= 18 then
        return 5
    end
    if not named(nearby.best(again), "gil") then
        return 6
    end

    -- Two names, each of them twice, and the one that adds up higher wins.
    local mixed = nearby.tally({ "ada:10", "gil:12", "ada:9", "gil:1" })
    if counted(mixed) ~= 2 then
        return 7
    end
    if worth(mixed, "ada") ~= 19 or worth(mixed, "gil") ~= 13 then
        return 8
    end
    if not named(nearby.best(mixed), "ada") then
        return 9
    end

    -- Spaces at either end of the name and of the number are not part of them.
    local spaced = nearby.tally({ " gil : 12 ", "  ada:5" })
    if counted(spaced) ~= 2 then
        return 10
    end
    if worth(spaced, "gil") ~= 12 or worth(spaced, "ada") ~= 5 then
        return 11
    end

    -- And the ways a line is not a line: no colon, two colons, no name, no
    -- number, a number with a fraction in it, and a number that is not one.
    local wrong = nearby.tally({ "gil", "gil:12:3", ":9", "   :9", "gil:",
                                 "gil:3.5", "gil:x", "" })
    if counted(wrong) ~= 0 then
        return 12
    end
    if nearby.best(wrong) ~= nil then
        return 13
    end

    -- A line that is not a line is passed over rather than stopping the ones
    -- after it.
    local among = nearby.tally({ "gil:12", "rubbish", "ada:5" })
    if counted(among) ~= 2 or worth(among, "ada") ~= 5 then
        return 14
    end

    -- The same greatest score twice goes to the name that comes first, and the
    -- one that comes last is the one read first here.
    local tied = nearby.tally({ "rex:7", "ada:7", "gil:3" })
    if not named(nearby.best(tied), "ada") then
        return 15
    end

    -- Scores below nought are scores.
    local below = nearby.tally({ "gil:-4", "ada:-9" })
    if worth(below, "gil") ~= -4 or worth(below, "ada") ~= -9 then
        return 16
    end
    if not named(nearby.best(below), "gil") then
        return 17
    end
    local crossing = nearby.tally({ "gil:-4", "gil:10" })
    if worth(crossing, "gil") ~= 6 then
        return 18
    end

    -- A board with nothing on it has nobody at the top of it.
    local empty = nearby.tally({})
    if counted(empty) ~= 0 then
        return 19
    end
    if nearby.best(empty) ~= nil then
        return 20
    end
    return 0
end

print("checks " .. tostring(checks()))
