-- `rules.lua` as Lua 5.4 takes it: Luau's types, `+=`, `table.create`, `if`
-- as an expression, a table walked without `ipairs` and backquoted text are
-- written the way Lua has them, and `bit32`, which Lua 5.4 no longer has, as
-- its own `&`, `|` and `~`. Nothing else changed, and it answers what
-- `rules.lua` answers. Luau keeps `rules.lua`, whose types its native tier
-- reads, and LuaJIT `rules.luajit.lua`.

local ACTORS = 4000
local ROUNDS = 200
local KINDS = 8

local HUNGRY = 1
local TIRED = 2
local HURT = 4
local RICH = 8

-- Kinds: 0 food(fills), 1 tool(power), 2 coin, 3 note(which)

local function nameOf(names, at)
    return names[(at % #names) + 1]
end

local function kindOf(seed)
    local which = seed % 4
    if which == 0 then
        return 0, 1 + seed % 5
    elseif which == 1 then
        return 1, 1 + seed % 3
    elseif which == 2 then
        return 2, 0
    end
    return 3, seed % 11
end

local function worthOf(item)
    local kind = item.kind
    if kind == 0 then
        return item.value * item.many
    elseif kind == 1 then
        return item.value * 3
    elseif kind == 2 then
        return item.many
    end
    if item.value == 0 then
        return 0
    end
    return 1
end

local function start(many)
    local names = { "bread", "hammer", "coin", "letter", "rope", "lamp" }
    local who = {}
    for i = 0, many - 1 do
        local cools = {}
        for k = 1, KINDS do
            cools[k] = 0
        end
        local bag = {}
        for j = 0, i % 4 do
            local kind, value = kindOf(i + j)
            table.insert(bag, {
                name = nameOf(names, i + j),
                kind = kind,
                value = value,
                many = 1 + j,
            })
        end
        local state = HUNGRY
        if i % 3 == 0 then
            state = state | TIRED
        end
        if i % 7 == 0 then
            state = state | HURT
        end
        table.insert(who, {
            name = nameOf(names, i),
            state = state,
            hp = 10 + i % 9,
            coins = i % 13,
            cools = cools,
            bag = bag,
            task = -1,
            done = 0,
        })
    end
    return who, names
end

-- One actor, one round: read what is carried, test a flag, run a timer down,
-- decide what to do next. The task is the same number Kest's `taskNumber`
-- answers with, so the rules below are the same rules.
local function decide(one, round, names)
    local worth = 0
    for _, item in ipairs(one.bag) do
        worth = worth + worthOf(item)
    end

    local ready = 0
    local cools = one.cools
    for at = 1, #cools do
        if cools[at] > 0 then
            cools[at] = cools[at] - 1
        else
            ready = ready + 1
        end
    end

    if one.state & HUNGRY == HUNGRY then
        if worth > 6 then
            one.state = one.state & ~HUNGRY
            one.hp = one.hp + 1
        else
            one.hp = one.hp - 1
        end
    end
    if one.state & HURT == HURT and ready > 4 then
        one.hp = one.hp + 2
        one.state = one.state & ~HURT
    end
    if one.hp > 20 then
        one.hp = 20
    end

    local doing = one.task
    local next = -1
    local did = 0
    if doing < 0 then
        if worth < 4 then
            next = round % KINDS
        else
            next = 100 + round % KINDS
        end
    elseif doing >= 1000 then
        local left = doing - 1000
        if left <= 1 then
            next = -1
        else
            next = 1000 + left - 1
        end
    elseif doing >= 100 then
        local which = doing - 100
        if #one.bag > 0 then
            local last = table.remove(one.bag)
            one.coins = one.coins + worthOf(last)
            did = 1
        end
        if one.coins > 40 then
            next = 1002
        else
            next = which
        end
    else
        local which = doing
        if cools[(which % KINDS) + 1] == 0 then
            local kind, value = kindOf(round + which)
            table.insert(one.bag, {
                name = nameOf(names, round + which),
                kind = kind,
                value = value,
                many = 1,
            })
            cools[(which % KINDS) + 1] = 3 + which % 4
            next = 100 + which
            did = 1
        else
            next = 1000 + 1 + which % 3
        end
    end
    one.task = next
    one.done = one.done + did
    if one.coins > 60 then
        one.state = one.state | RICH
    end
    one.hp = one.hp + (worth % 3)
end

local function round(who, names, at)
    local sum = 0
    for i = 1, #who do
        local one = who[i]
        decide(one, at, names)
        sum = sum + (one.hp + one.done + one.coins)
    end
    return sum
end

local function worth(who)
    local sum = 0
    for _, one in ipairs(who) do
        sum = sum + (one.hp * 3 + one.coins)
        sum = sum + (#one.bag * 2 + one.done)
        sum = sum + #one.name
        for _, item in ipairs(one.bag) do
            sum = sum + (worthOf(item) + #item.name)
        end
        for _, cool in ipairs(one.cools) do
            sum = sum + cool
        end
        local task = one.task
        if task < 0 then
            sum = sum + 0
        elseif task >= 1000 then
            sum = sum + ((task - 1000) + 1000)
        elseif task >= 100 then
            sum = sum + ((task - 100) + 100)
        else
            sum = sum + (task + 1)
        end
    end
    return sum
end

local many = ACTORS
local turns = ROUNDS
local who, names = start(many)
local before = worth(who)
local sum = 0
for at = 0, turns - 1 do
    sum = sum + round(who, names, at)
end
local after = worth(who)
if before <= 0 or sum <= 0 or after == before then
    error("the workload did not do its work")
end
print(string.format("rules %d worth %d of %d over %d", sum, after, many, turns))
