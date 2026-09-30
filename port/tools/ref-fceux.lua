-- FCEUX 2.6.5 reference dumper (cross-check; FCEUX embeds Lua 5.1).
-- Writes each frame's 256x240 palette indices (one byte per pixel,
-- emphasis not included) to MM_RAW; framehash.py hashes them.
--   MM_RAW=/abs/build/port-ref/x.raw MM_INPUT=/abs/port/traces/t.mmin
--   MM_FRAMES=N fceux --no-config 1 --sound 0 --loadlua ref-fceux.lua ROM
local names = {"A", "B", "select", "start", "up", "down", "left", "right"}
local bits = {A=1, B=2, SELECT=4, START=8, UP=16, DOWN=32, LEFT=64, RIGHT=128}

local events, len = {}, nil
local input = os.getenv("MM_INPUT")
if input and input ~= "" then
  for line in io.lines(input) do
    line = line:gsub("#.*", "")
    local a, b = line:match("^%s*(%S+)%s+(%S+)")
    if a == "end" then len = tonumber(b)
    elseif a then
      local pad = 1
      if b:sub(1, 2) == "2:" then pad = 2; b = b:sub(3)
      elseif b:sub(1, 2) == "1:" then b = b:sub(3) end
      local mask = 0
      if b ~= "-" then
        for n in b:gmatch("[^+]+") do mask = mask + assert(bits[n:upper()], n) end
      end
      table.insert(events, {frame = tonumber(a), pad = pad, mask = mask})
    end
  end
end
local frames = tonumber(os.getenv("MM_FRAMES")) or len or (#events > 0 and events[#events].frame + 1) or 600

local function pad_table(mask)
  local t = {}
  for i, n in ipairs(names) do t[n] = math.floor(mask / 2 ^ (i - 1)) % 2 == 1 end
  return t
end

local out = assert(io.open(assert(os.getenv("MM_RAW"), "set MM_RAW"), "wb"))
local cursor, cur = 1, {0, 0}
local row = {}
emu.poweron()
emu.speedmode("maximum")
for f = 0, frames - 1 do
  while cursor <= #events and events[cursor].frame <= f do
    cur[events[cursor].pad] = events[cursor].mask
    cursor = cursor + 1
  end
  joypad.set(1, pad_table(cur[1]))
  joypad.set(2, pad_table(cur[2]))
  emu.frameadvance()
  for y = 0, 239 do
    for x = 0, 255 do
      local _, _, _, px = emu.getscreenpixel(x, y, true)
      row[x + 1] = px
    end
    out:write(string.char(unpack(row)))
  end
end
out:close()
os.exit(0)
