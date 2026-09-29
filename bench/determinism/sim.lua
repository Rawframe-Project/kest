-- `sim.kest` in the Lua that Lua 5.4, LuaJIT and Luau all read: the same
-- steps through the platform's own `math`. See D1274.
local x, y, z = 0.5, 1.25, 0.0
local sin, cos, atan2, sqrt, abs = math.sin, math.cos, math.atan2 or math.atan,
    math.sqrt, math.abs
for i = 0, 99999 do
    local a = sin(x * 1.7 + y)
    local b = cos(y * 0.3 - x)
    x = a * b + 0.001 * (i % 17)
    y = atan2(x, y + 1.1) + sqrt(abs(x) + 0.5)
    z = z * 0.5 + (abs(y) + 0.5) ^ 1.3
end
print(string.format("%.17g %.17g %.17g", x, y, z))
