-- Mesen2 reference dumper (primary reference). Run via ref-run.sh, which
-- installs mesen-settings.json: a 512-entry "user palette" whose colour i
-- is RGB 0x000000 + i, so each output pixel carries the PPU's palette
-- index | emphasis << 6 exactly. Writes 2 bytes/pixel to MM_RAW.
local names = {"a", "b", "select", "start", "up", "down", "left", "right"}
local bits = {A=1, B=2, SELECT=4, START=8, UP=16, DOWN=32, LEFT=64, RIGHT=128}

local events, len = {}, nil
local input = os.getenv("MM_INPUT")
if input and input ~= "" then
  for line in io.lines(input) do
    line = line:gsub("#.*", "")
    local a, b = line:match("^%s*(%S+)%s+(%S+)")
    if a == "end" then len = tonumber(b)
    elseif a then
      local pad = 0
      if b:sub(1, 2) == "2:" then pad = 1; b = b:sub(3)
      elseif b:sub(1, 2) == "1:" then b = b:sub(3) end
      local mask = 0
      if b ~= "-" then
        for n in b:gmatch("[^+]+") do mask = mask | assert(bits[n:upper()], n) end
      end
      table.insert(events, {frame = tonumber(a), pad = pad, mask = mask})
    end
  end
end
local frames = tonumber(os.getenv("MM_FRAMES")) or len or (#events > 0 and events[#events].frame + 1) or 600
local offset = tonumber(os.getenv("MM_INPUT_OFFSET")) or 0

local out = assert(io.open(assert(os.getenv("MM_RAW"), "set MM_RAW"), "wb"))
local cursor, cur = 1, {[0] = 0, [1] = 0}
local frame = 0 -- index of the frame being emulated

local dbg = os.getenv("MM_DBG") and io.open(os.getenv("MM_DBG"), "w")
local function apply()
  local f = frame + offset
  while cursor <= #events and events[cursor].frame <= f do
    cur[events[cursor].pad] = events[cursor].mask
    cursor = cursor + 1
  end
  -- Mesen 2.1.1 quirk: setInput(t, 1) also overwrites port 0's buttons, so
  -- port 1 is set first and port 0 last.
  for port = 1, 0, -1 do
    local t = {}
    for i, n in ipairs(names) do t[n] = (cur[port] >> (i - 1)) & 1 == 1 end
    if dbg and cur[port] ~= 0 then dbg:write("frame ", frame, " port ", port, " mask ", cur[port], "\n"); dbg:flush() end
    emu.setInput(t, port)
  end
end

-- Optional: MM_RAMDUMP=/abs/file writes the 2 KiB CPU RAM after every frame.
local rd_path = os.getenv("MM_RAMDUMP")
local ramdump = rd_path and rd_path ~= "" and assert(io.open(rd_path, "wb"))

-- Optional: MM_WRITELOG=/abs/file with MM_WATCH=4000-4017 logs CPU writes in
-- that range, one line per frame, in mm-headless --watch format (writes only).
local wl_path = os.getenv("MM_WRITELOG")
local writelog = wl_path and wl_path ~= "" and assert(io.open(wl_path, "w"))
local wlines = {}
if writelog then
  local lo, hi = (os.getenv("MM_WATCH") or "4000-4017"):match("(%x+)-(%x+)")
  emu.addMemoryCallback(function(addr, value)
    wlines[#wlines + 1] = string.format(" W%04X=%02X", addr, value)
  end, emu.callbackType.write, tonumber(lo, 16), tonumber(hi, 16))
end

-- Optional: MM_NMIDUMP=/abs/file writes the 2 KiB CPU RAM each time the CPU
-- is about to execute the NMI handler's first instruction (an exact,
-- instruction-aligned sample point, unlike the end-of-frame event).
-- MM_NOSCREEN=1 skips the screen capture (much faster).
local nd_path = os.getenv("MM_NMIDUMP")
local nmidump = nd_path and nd_path ~= "" and assert(io.open(nd_path, "wb"))
local noscreen = os.getenv("MM_NOSCREEN") == "1"
-- Optional: MM_PPUDUMP=/abs/file dumps PPU memory at every MM_PPUEVERY-th
-- NMI entry: nametable RAM (2 KiB), palette (32), OAM (256), CHR-RAM (8 KiB).
local pd_path = os.getenv("MM_PPUDUMP")
local ppudump = pd_path and pd_path ~= "" and assert(io.open(pd_path, "wb"))
local ppuevery = tonumber(os.getenv("MM_PPUEVERY")) or 30
local nmicount = 0
local function dump_mem(fh, mt, len)
  local r = {}
  for a = 0, len - 1 do r[a + 1] = emu.read(a, mt) end
  for i = 1, len, 1024 do fh:write(string.char(table.unpack(r, i, math.min(i + 1023, len)))) end
end
if ppudump then
  local vec = emu.read(0xFFFA, emu.memType.nesMemory) | (emu.read(0xFFFB, emu.memType.nesMemory) << 8)
  emu.addMemoryCallback(function()
    nmicount = nmicount + 1
    if nmicount % ppuevery == 0 then
      dump_mem(ppudump, emu.memType.nesNametableRam, 2048)
      dump_mem(ppudump, emu.memType.nesPaletteRam, 32)
      dump_mem(ppudump, emu.memType.nesSpriteRam, 256)
      dump_mem(ppudump, emu.memType.nesChrRam, 8192)
    end
  end, emu.callbackType.exec, vec, vec)
end
if nmidump then
  local vec = emu.read(0xFFFA, emu.memType.nesMemory) | (emu.read(0xFFFB, emu.memType.nesMemory) << 8)
  emu.addMemoryCallback(function()
    local r = {}
    for a = 0, 2047 do r[a + 1] = emu.read(a, emu.memType.nesInternalRam) end
    nmidump:write(string.char(table.unpack(r, 1, 1024)), string.char(table.unpack(r, 1025, 2048)))
  end, emu.callbackType.exec, vec, vec)
end

emu.addEventCallback(apply, emu.eventType.inputPolled)
emu.addEventCallback(function()
  if not noscreen then
    local buf = emu.getScreenBuffer()
    assert(#buf == 256 * 240, "unexpected screen size " .. #buf)
    local parts = {}
    for y = 0, 239 do
      local row = {}
      for x = 1, 256 do
        row[x] = buf[y * 256 + x] & 0x1FF
      end
      parts[#parts + 1] = string.pack(string.rep("<I2", 256), table.unpack(row))
    end
    out:write(table.concat(parts))
  end
  if ramdump then
    local r = {}
    for a = 0, 2047 do r[a + 1] = emu.read(a, emu.memType.nesInternalRam) end
    ramdump:write(string.char(table.unpack(r, 1, 1024)), string.char(table.unpack(r, 1025, 2048)))
  end
  if writelog then
    writelog:write(frame, " watch:", #wlines > 0 and table.concat(wlines) or " -", "\n")
    wlines = {}
  end
  frame = frame + 1
  if frame >= frames then
    out:close()
    if ramdump then ramdump:close() end
    if nmidump then nmidump:close() end
    if ppudump then ppudump:close() end
    if writelog then writelog:close() end
    emu.exit(0)
  end
end, emu.eventType.endFrame)
