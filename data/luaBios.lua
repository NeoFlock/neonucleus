--[[
Copyright (c) 2013-2015 Florian "Sangar" Nücke

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

-------------------------------------------------------------------------------

All images / textures and localization strings (resources) are put in the
public domain, unless explicitly excluded below. More specicially, see CC0 1.0
Universal:

        http://creativecommons.org/publicdomain/zero/1.0/

Contributions:
  PixelToast - Capacitor textures.
  asie - Disk drive inject/eject and floppy disk access sound samples.

Thanks a lot!

-------------------------------------------------------------------------------

The font used for screens and for monospace text in manual is unscii, made by
viznut, and was further expanded by asie. For more information, please see:

        https://github.com/asiekierka/unscii-asie

-------------------------------------------------------------------------------

Assets from other sources:
  HDD access samples based on this sample from freesound.org:
    https://www.freesound.org/people/artykris/sounds/117401/
]]

local init
do
  local component_invoke = component.invoke
  local function boot_invoke(address, method, ...)
    local result = table.pack(pcall(component_invoke, address, method, ...))
    if not result[1] then
      return nil, result[2]
    else
      return table.unpack(result, 2, result.n)
    end
  end

  -- backwards compatibility, may remove later
  local eeprom = component.list("eeprom")()
  local bootAddr = nil
  computer.getBootAddress = function()
    return bootAddr
  end
  computer.setBootAddress = function(address)
      bootAddr = address
  end

  do
    local screen = component.list("screen")()
    local gpu = component.list("gpu")()
    if gpu and screen then
      boot_invoke(gpu, "bind", screen)
    end
  end
  local function tryLoadFrom(address)
    local handle, reason = boot_invoke(address, "open", "/init.lua")
    if not handle then
      return nil, reason
    end
    local buffer = ""
    repeat
      local data, reason = boot_invoke(address, "read", handle, math.maxinteger or math.huge)
      if not data and reason then
        return nil, reason
      end
      buffer = buffer .. (data or "")
    until not data
    boot_invoke(address, "close", handle)
    return load(buffer, "=init")
  end
  local reason
  if computer.getBootAddress() then
    init, reason = tryLoadFrom(computer.getBootAddress())
  end
  if not init then
    computer.setBootAddress()
    for address in component.list("filesystem") do
      init, reason = tryLoadFrom(address)
      if init then
        computer.setBootAddress(address)
        break
      end
    end
  end
  if not init then
    error("no bootable medium found" .. (reason and (": " .. tostring(reason)) or ""), 0)
  end
  computer.beep(1000, 0.2)
end
return init()
