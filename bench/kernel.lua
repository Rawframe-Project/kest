-- The same work in Luau. A table of four numbers a body, walked and moved,
-- with the same checksum at the end.
local many = 20000
local rounds = 100

local x, y, dx, dy = {}, {}, {}, {}
for i = 0, many - 1 do
    local f = i + 0.0
    x[i] = f % 1000.0
    y[i] = (f * 7.0) % 1000.0
    dx[i] = 1.0 + f % 3.0
    dy[i] = 1.0 + f % 5.0
end

for _ = 1, rounds do
    for at = 0, many - 1 do
        local ax = x[at] + dx[at]
        local ay = y[at] + dy[at]
        if ax < 0.0 or ax > 1000.0 then dx[at] = -dx[at] end
        if ay < 0.0 or ay > 1000.0 then dy[at] = -dy[at] end
        x[at] = ax
        y[at] = ay
    end
end

local sum = 0.0
for at = 0, many - 1 do
    sum = sum + x[at] + y[at]
end
print(string.format("kernel %.0f", sum))
