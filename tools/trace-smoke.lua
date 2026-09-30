-- FCEUX 2.6.5. Set MM_TRACE_OUTPUT to an absolute ignored build/ path.
-- Outputs contain only counters/address metadata; all results stay in build/.
-- CPU hooks do not identify physical banks; only fixed-bank addresses are counted.
local output = assert(io.open(assert(os.getenv("MM_TRACE_OUTPUT"),
  "Set MM_TRACE_OUTPUT to an absolute path inside ignored build/"), "w"))
local counts = {}
local symbols = {
  reset_entry=0xD799, nmi_dispatch=0xE362, nmi_game=0xE365,
  mmc1_initialize=0xFFD0, mmc1_write_prg=0xFFA0,
  vm_dispatch=0xC423, vm_fetch_byte=0xC44F, read_controllers=0xFB68
}
for name, address in pairs(symbols) do
  counts[name] = 0
  memory.registerexec(address, function() counts[name] = counts[name] + 1 end)
end
local writes = {ppu=0, apu=0, controller=0, mapper=0}
memory.registerwrite(0x2000, 8, function() writes.ppu = writes.ppu + 1 end)
memory.registerwrite(0x4000, 0x16, function() writes.apu = writes.apu + 1 end)
memory.registerwrite(0x4016, function() writes.controller = writes.controller + 1 end)
for _, address in ipairs({0x8000, 0xA000, 0xC000, 0xE000}) do
  memory.registerwrite(address, function() writes.mapper = writes.mapper + 1 end)
end
emu.poweron()
emu.speedmode("maximum")
local frames = tonumber(os.getenv("MM_TRACE_FRAMES")) or 900
for frame=1,frames do
  -- Reproducible startup/selection exploration; no RAM/state mutation.
  local input = {up=false, down=false, left=false, right=false,
                 A=false, B=false, start=false, select=false}
  if frame == 180 or frame == 360 or frame == 600 then input.start = true end
  if frame == 420 or frame == 540 or frame == 660 then input.A = true end
  if frame >= 450 and frame <= 465 then input.right = true end
  joypad.set(1, input)
  emu.frameadvance()
end
local function object(values)
  local names = {}
  for name in pairs(values) do table.insert(names, name) end
  table.sort(names)
  local fields = {}
  for _, name in ipairs(names) do
    table.insert(fields, string.format('"%s":%d', name, values[name]))
  end
  return "{" .. table.concat(fields, ",") .. "}"
end
output:write(string.format('{"frames":%d,"pc":%d,"routine_hits":%s,"writes":%s}\n',
  frames, memory.getregister("pc"), object(counts), object(writes)))
output:close()
emu.exit()
