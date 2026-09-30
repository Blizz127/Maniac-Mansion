-- Read-only USA SCUMM investigation, FCEUX 2.6.5. All outputs stay local.
-- MM_STATE_DIR: ignored output directory; MM_STATE_INPUT: local .mmin trace.
-- MM_STATE_STEPS: optional local pointer-command file after input replay.
-- No memory/register writes, cheats, or savestate loading.
local dir = assert(os.getenv('MM_STATE_DIR'))
local log = assert(io.open(dir .. '/events.tsv', 'w'))
local frame, hits = 0, {}
local watched = {0x63C6,0x63C5,0x6104,0x6124,0x6C33,0x6C31,0x6C32,
                 0x6C34,0x6C35,0x6C36,0x6117,0x6118,0x6314,0x6B08,0x6C6A}
local function snapshot(label)
  log:write(string.format('snapshot\t%d\t%s\tpc=%04X',frame,label,memory.getregister('pc')))
  for _, a in ipairs(watched) do log:write(string.format('\t%04X=%02X',a,memory.readbyte(a))) end
  log:write('\n'); log:flush()
end
local routines = {room_change=0xC13A,room_load=0xC185,room_header=0xC1C6,
 room_objects=0xC24A,room_exit_script=0xC38A,room_entry_script=0xC360,
 script_schedule=0xC3FD,vm_dispatch=0xC423,vm_yield=0xC870,
 load_room_opcode=0xCBC1,load_room_ego_opcode=0xCBD9,
 camera_tick=0xE001,camera_set=0xE0B1,actor_put=0xE114,
 actor_follow=0xD256,object_owner_set=0xCD41,inventory_pickup=0xCB4B,
 frame_wait=0xC126}
for name,a in pairs(routines) do
 hits[name]=0
 memory.registerexec(a,function()
  hits[name]=hits[name]+1
  if name=='room_change' or name=='room_load' or name=='load_room_opcode' or
     name=='load_room_ego_opcode' or name=='object_owner_set' then
   log:write(string.format('exec\t%d\t%s\tA=%02X\tX=%02X\tY=%02X\troom=%02X\tslot=%02X\n',
    frame,name,memory.getregister('a'),memory.getregister('x'),memory.getregister('y'),
    memory.readbyte(0x63C6),memory.readbyte(0x6314)))
  end
 end)
end
for _,a in ipairs({0x63C6,0x63C5,0x6104,0x6C33,0x6C31}) do
 memory.registerwrite(a,function(address,size,value)
  log:write(string.format('write\t%d\t%04X\t%02X\tpc=%04X\n',frame,address,value,memory.getregister('pc')))
 end)
end
local function step(input)
 joypad.set(1,input or {}); emu.frameadvance(); frame=frame+1
end
local function wait(n) for i=1,n do step({}) end end
local function dump(label)
 snapshot(label)
 -- RAM and framebuffer are game-derived and must never be staged.
 local ram=assert(io.open(dir..'/'..label..'.ram','wb'))
 ram:write(memory.readbyterange(0,0x8000)); ram:close()
 local screen=assert(io.open(dir..'/'..label..'.ppm','wb'))
 screen:write('P6\n256 240\n255\n')
 for y=0,239 do for x=0,255 do
  local r,g,b=emu.getscreenpixel(x,y,true); screen:write(string.char(r,g,b))
 end end
 screen:close()
end
emu.poweron(); emu.speedmode('maximum')
local changes,last={},0
local input_names={A='A',B='B',START='start',SELECT='select',UP='up',DOWN='down',LEFT='left',RIGHT='right'}
for line in io.lines(assert(os.getenv('MM_STATE_INPUT'))) do
 local finish=line:match('^end%s+(%d+)')
 if finish then last=tonumber(finish) end
 local f,buttons=line:match('^(%d+)%s+(%S+)')
 if f then
  local input={}; for b in buttons:gmatch('[^+]+') do
   if b~='-' then input[assert(input_names[b:upper()], 'Unknown input button: '..b)]=true end
  end
  changes[tonumber(f)]=input; last=math.max(last,tonumber(f)+1)
 end
end
local input={}
for i=0,last-1 do
 if changes[i] then input=changes[i] end
 step(input)
 if frame%1000==0 then snapshot('replay') end
end
wait(120); dump('replay-end')
local steps=os.getenv('MM_STATE_STEPS')
if steps and steps~='' then
 for line in io.lines(steps) do
  local cmd,a,b,c=line:match('^(%w+)%s+(%S+)%s*(%S*)%s*(%S*)')
  if cmd=='wait' then wait(tonumber(a))
  elseif cmd=='press' then step({[a]=true}); wait(tonumber(b) or 2)
  elseif cmd=='seek' then
   local tx,ty=tonumber(a),tonumber(b)
   local sprite=tonumber(c) or 1
   for i=1,1500 do
    local x=memory.readbyte(0x200+sprite*4+3)
    local y=memory.readbyte(0x200+sprite*4)
    local dx,dy=tx-x,ty-y
    if math.abs(dx)<=2 and math.abs(dy)<=2 then break end
    step({left=dx < -2,right=dx > 2,up=dy < -2,down=dy > 2})
    if math.abs(dx)<8 and math.abs(dy)<8 then wait(2) end
   end
   wait(3)
  elseif cmd=='dump' then dump(a) end
 end
end
snapshot('end')
for name,n in pairs(hits) do log:write(string.format('hits\t%s\t%d\n',name,n)) end
log:close(); emu.exit()
