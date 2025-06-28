do
  local addr, invoke = computer.getBootAddress(), component.invoke
  local function loadfile(file)
    local handle = assert(invoke(addr, "open", file))
    local buffer = ""
    repeat
      local data = invoke(addr, "read", handle, math.maxinteger or math.huge)
      buffer = buffer .. (data or "")
    until not data
    invoke(addr, "close", handle)
    return load(buffer, "=" .. file, "bt", _G)
  end
  loadfile("/lib/core/boot.lua")(loadfile)
end

while true do
    debugprint("grabbing shell")
  local result, reason = xpcall(assert(require("shell").getShell()), function(msg)
    return tostring(msg).."\n"..debug.traceback()
  end)
    debugprint("resumed", result, reason)
  if not result then
    debugprint((reason ~= nil and tostring(reason) or "unknown error") .. "\n")
    io.stderr:write((reason ~= nil and tostring(reason) or "unknown error") .. "\n")
    io.write("Press any key to continue.\n")
    os.sleep(0.5)
    require("event").pull("key")
  end
end
