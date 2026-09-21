--!strict
-- The same application-state workload in Luau, written the way a gameplay
-- programmer would write it there: tables with named fields, numbers for the
-- bits, and a tag beside a value where Kest has an enum with a payload.
--
-- One difference is the languages' own and is not hidden: an `Actor` is a
-- value in Kest, so `decide` is handed a copy of the scalars and writes the
-- one it answers with back into the array. A table in Luau is a reference, so
-- the same code mutates in place. That is what each language is, and what the
-- two rows measure.

local ACTORS = 4000
local ROUNDS = 200
local KINDS = 8

local HUNGRY = 1
local TIRED = 2
local HURT = 4
local RICH = 8

-- Kinds: 0 food(fills), 1 tool(power), 2 coin, 3 note(which)
type Item = { name: string, kind: number, value: number, many: number }
type Actor = {
    name: string,
    state: number,
    hp: number,
    coins: number,
    cools: { number },
    bag: { Item },
    task: number,
    done: number,
}

local function nameOf(names: { string }, at: number): string
    return names[(at % #names) + 1]
end

local function kindOf(seed: number): (number, number)
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

local function worthOf(item: Item): number
    local kind = item.kind
    if kind == 0 then
        return item.value * item.many
    elseif kind == 1 then
        return item.value * 3
    elseif kind == 2 then
        return item.many
    end
    return if item.value == 0 then 0 else 1
end

local function start(many: number): ({ Actor }, { string })
    local names = { "bread", "hammer", "coin", "letter", "rope", "lamp" }
    local who: { Actor } = table.create(many)
    for i = 0, many - 1 do
        local cools = table.create(KINDS, 0)
        local bag: { Item } = {}
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
            state = bit32.bor(state, TIRED)
        end
        if i % 7 == 0 then
            state = bit32.bor(state, HURT)
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
local function decide(one: Actor, round: number, names: { string })
    local worth = 0
    for _, item in one.bag do
        worth += worthOf(item)
    end

    local ready = 0
    local cools = one.cools
    for at = 1, #cools do
        if cools[at] > 0 then
            cools[at] -= 1
        else
            ready += 1
        end
    end

    if bit32.band(one.state, HUNGRY) == HUNGRY then
        if worth > 6 then
            one.state = bit32.band(one.state, bit32.bnot(HUNGRY))
            one.hp += 1
        else
            one.hp -= 1
        end
    end
    if bit32.band(one.state, HURT) == HURT and ready > 4 then
        one.hp += 2
        one.state = bit32.band(one.state, bit32.bnot(HURT))
    end
    if one.hp > 20 then
        one.hp = 20
    end

    local doing = one.task
    local next = -1
    local did = 0
    if doing < 0 then
        next = if worth < 4 then round % KINDS else 100 + round % KINDS
    elseif doing >= 1000 then
        local left = doing - 1000
        next = if left <= 1 then -1 else 1000 + left - 1
    elseif doing >= 100 then
        local which = doing - 100
        if #one.bag > 0 then
            local last = table.remove(one.bag) :: Item
            one.coins += worthOf(last)
            did = 1
        end
        next = if one.coins > 40 then 1002 else which
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
    one.done += did
    if one.coins > 60 then
        one.state = bit32.bor(one.state, RICH)
    end
    one.hp += worth % 3
end

local function round(who: { Actor }, names: { string }, at: number): number
    local sum = 0
    for i = 1, #who do
        local one = who[i]
        decide(one, at, names)
        sum += one.hp + one.done + one.coins
    end
    return sum
end

local function worth(who: { Actor }): number
    local sum = 0
    for _, one in who do
        sum += one.hp * 3 + one.coins
        sum += #one.bag * 2 + one.done
        sum += #one.name
        for _, item in one.bag do
            sum += worthOf(item) + #item.name
        end
        for _, cool in one.cools do
            sum += cool
        end
        local task = one.task
        if task < 0 then
            sum += 0
        elseif task >= 1000 then
            sum += (task - 1000) + 1000
        elseif task >= 100 then
            sum += (task - 100) + 100
        else
            sum += task + 1
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
    sum += round(who, names, at)
end
local after = worth(who)
if before <= 0 or sum <= 0 or after == before then
    error("the workload did not do its work")
end
print(`rules {sum} worth {after} of {many} over {turns}`)
