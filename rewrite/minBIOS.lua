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
	component.invoke(gpu, "maxResolution")
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
		local term = string.find(codeSec, "\0", 5, true)
		return load(string.sub(codeSec, 1, term and (term - 1) or -1))
	end
	-- TODO: whatever else NC might be testing
	local sectorsIn32K = math.ceil(32768 / sectorSize)
	local bootCode = {firstSector}
	for i=2,sectorsIn32K do
		table.insert(bootCode, drive.readSector(i))
	end
	local rawCode = table.concat(bootCode)
	local term = string.find(rawCode, "\0")
	rawCode = string.sub(rawCode, 1, term and (term - 1) or -1)
	return load(rawCode)
end

local paths = {
	"init.lua",
	"OS.lua",
	"boot/pipes/kernel",
}

local prevboot = computer.getBootAddress()
if prevboot then
	if component.type(prevboot) == "filesystem" then
		for _, path in ipairs(paths) do
			local code = romRead(prevboot, path)
			if code then
				assert(load(code))(prevboot)
				error("halted")
			end
		end
	end
	if component.type(prevboot) == "drive" then
		local f = getBootCode(prevboot)
		if f then
			f(prevboot)
			error("halted")
		end
	end
end

for addr in component.list("filesystem", true) do
	for _, path in ipairs(paths) do
		local code = romRead(addr, path)
		if code then
			computer.setBootAddress(addr)
			assert(load(code))(addr)
			error("halted")
		end
	end
end

for addr in component.list("drive", true) do
	local f = getBootCode(addr)
	if f then
		computer.setBootAddress(addr)
		f(addr)
		error("halted")
	end
end

error("no bootable medium found")
