-- Minimum viable BIOS, effectively a blend between LuaBIOS and AdvancedLoader

local component, computer = component, computer
local gpu = component.list("gpu")()
local screen = component.list("screen")()
local eeprom = component.list("eeprom")()

computer.getBootAddress = function()
	return component.invoke(eeprom, "getData")
end
computer.setBootAddress = function(addr)
	return component.invoke(eeprom, "setData", addr)
end

if gpu and screen then
	component.invoke(gpu, "bind", screen)
	local w, h = component.invoke(gpu, "maxResolution")
	component.invoke(gpu, "setResolution", w, h)
	component.invoke(gpu, "setForeground", 0xFFFFFF)
	component.invoke(gpu, "setBackground", 0x000000)
	component.invoke(gpu, "fill", 1, 1, w, h, " ")
end

local function romRead(addr, path)
	local fs = component.proxy(addr)
	local fd = fs.open(path)
	if not fd then return end
	local data = ""
	while true do
		local chunk, err = fs.read(fd, math.huge)
		if err then
			fs.close(fd)
			return nil
		end
		if not chunk then break end
		data = data .. chunk
	end
	fs.close(fd)
	return data
end

local function getBootCode(addr)
	local drive = component.proxy(addr)
	local sectorSize = drive.getSectorSize()
	local firstSector = drive.readSector(1)

	-- Generic MBR bootcode
	if firstSector:sub(-2, -1) == "\x55\xAA" then
		local codeEnd = sectorSize - 66
		local codeSec = string.sub(firstSector, 1, codeEnd)
		local term = string.find(codeSec, "\0", nil, true)
		return load(string.sub(codeSec, 1, term and (term - 1) or -1))
	end
	-- TODO: whatever else NC might be testing

	-- Read first 32K, which is a standard convention
	local sectorsIn32K = math.ceil(32768 / sectorSize)
	local bootCode = {firstSector}
	-- since its null terminated, this is an optimization
	if not firstSector:find("\0") then
		for i=2,sectorsIn32K do
			local sec = drive.readSector(i)
			table.insert(bootCode, sec)
			if sec:find("\0") then break end
		end
	end
	local rawCode = table.concat(bootCode)
	local term = string.find(rawCode, "\0")
	rawCode = string.sub(rawCode, 1, term and (term - 1) or -1)
	if rawCode == "" then return end
	return load(rawCode)
end

local paths = {
	"init.lua",
	"OS.lua",
	"boot/pipes/kernel",
}

local bootables = {}

for addr in component.list("filesystem", true) do
	for _, path in ipairs(paths) do
		local code = romRead(addr, path)
		if code then
			table.insert(bootables, {addr = addr, code = load(code)})
		end
	end
end

for addr in component.list("drive", true) do
	local f = getBootCode(addr)
	if f then
		table.insert(bootables, {code = f, addr = addr})
	end
end

local function boot(bootable)
	local w, h = component.invoke(gpu, "maxResolution")
	component.invoke(gpu, "setResolution", w, h)
	component.invoke(gpu, "setForeground", 0xFFFFFF)
	component.invoke(gpu, "setBackground", 0x000000)
	component.invoke(gpu, "fill", 1, 1, w, h, " ")
	computer.setBootAddress(bootable.addr)
	bootable.code(bootable.addr)
	error("halted")
end

if #bootables == 1 then
	boot(bootables[1])
elseif #bootables > 1 then
	local sel = 1
	local function showBootable(bootable, i)
		local w = component.invoke(gpu, "getResolution")
		component.invoke(gpu, "fill", 1, i, w, 1, " ")
		local text = component.invoke(bootable.addr, "getLabel") or bootable.addr
		component.invoke(gpu, "set", 1, i, text)
	end
	for i=1,#bootables do
		if i == 1 then
			component.invoke(gpu, "setForeground", 0x000000)
			component.invoke(gpu, "setBackground", 0xFFFFFF)
		else
			component.invoke(gpu, "setForeground", 0xFFFFFF)
			component.invoke(gpu, "setBackground", 0x000000)
		end
		showBootable(bootables[i], i)
	end
	while true do
		local e = {computer.pullSignal()}
		if e[1] == "key_down" then
			local keycode = e[4]
			component.invoke(gpu, "setForeground", 0xFFFFFF)
			component.invoke(gpu, "setBackground", 0x000000)
			showBootable(bootables[sel], sel)
			if keycode == 0x1C then
				break
			elseif keycode == 0xC8 then
				sel = math.max(sel - 1, 1)
			elseif keycode == 0xD0 then
				sel = math.min(sel + 1, #bootables)
			end
			component.invoke(gpu, "setForeground", 0x000000)
			component.invoke(gpu, "setBackground", 0xFFFFFF)
			showBootable(bootables[sel], sel)
		end
	end

	boot(bootables[sel])
end

error("no bootable medium found")
