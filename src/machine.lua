-- The boot-up code of the Lua architecture.
-- Extremely bad.
-- Do not use in a serious context, you will be hacked.
-- There is no sandboxing here.
-- NOTE: we implemented method synchronization in here btw

os.exit = nil
os.execute = nil

local sysyieldobj = {}
local coroutine = coroutine

local resume, yield = coroutine.resume, coroutine.yield

local function sysyield()
	yield(sysyieldobj)
end

function coroutine.resume(co, ...)
	while true do
		local t = {resume(co, ...)}
		if t[1] and rawequal(t[2], sysyieldobj) then
			yield(sysyieldobj)
		else
			return table.unpack(t)
		end
	end
end

function coroutine.wrap(f)
	local co = coroutine.create(f)
	return function(...)
		if coroutine.status(co) ~= "suspended" then return end
		local t = {coroutine.resume(co, ...)}
		if t[1] then
			return table.unpack(t, 2)
		end
		error(t[2], 2)
	end
end

local clist, cinvoke, computer, component, print, unicode = component.list, component.invoke, computer, component, print, unicode
debug.print = print
debug.sysyield = sysyield

function component.list(ctype, exact)
	local list = clist()
	local desired = {}

	for addr, type in pairs(list) do
		if ctype then
			if exact then
				if ctype == type then desired[addr] = type end
			else
				if string.find(type, ctype) then desired[addr] = type end
			end
		else
			desired[addr] = type
		end
	end

	local key = nil
	setmetatable(desired, {__call = function()
		local val
		key, val = next(desired, key)
		return key, val
	end})
	return desired
end

local syncedMethodStats

local function realInvoke(address, method, ...)
	local t = {pcall(cinvoke, address, method, ...)}
	if not _SYNCED then
		if computer.energy() <= 0 then sysyield() end -- out of power
		if computer.isOverused() then sysyield() end -- overused
		if computer.isIdle() then sysyield() end -- machine idle
	end

	if os.getenv("NN_INVDBG") and component.type(address) == os.getenv("NN_INVDBG") then
		print("invoked", address, method, ...)
		print("got", table.unpack(t))
	end

	if t[1] then
		return table.unpack(t, 2)
	end
	return nil, t[2]
end

function component.invoke(address, method, ...)
	if type(address) ~= "string" then return nil, "bad argument #1 (string expected)" end
	if type(method) ~= "string" then return nil, "bad argument #2 (string expected)" end
	if component.getMethodFlags(address, method).direct or os.getenv("NN_FAST") then
		return realInvoke(address, method, ...)
	else
		-- must sync
		syncedMethodStats = {address, method, ...}
		sysyield()
		local rets = syncedMethodStats
		syncedMethodStats = nil
		return table.unpack(rets)
	end
end

local componentCallback = {
	__call = function(self, ...)
		return component.invoke(self.address, self.name, ...)
	end,
	__tostring = function(self)
		return component.doc(self.address, self.name) or "function"
	end,
}

local componentProxy = {
	__index = function(self, key)
		if self.fields[key] and self.fields[key].getter then
			return component.invoke(self.address, key)
		end
		return rawget(self, key)
	end,
	__newindex = function(self, key, value)
		if self.fields[key] and self.fields[key].setter then
			return component.invoke(self.address, key, value)
		end
		rawset(self, key, value)
	end,
	__pairs = function(self)
		local reachedFields = false
		local key, val
		return function()
			if not reachedFields then
				key, val = next(self, key)
				if key == nil then reachedFields = true end
			end
			if reachedFields then
				key = next(self.fields, key)
				val = self[key]
			end
			return key, val
		end
	end,
}

local proxyCache = setmetatable({}, {__mode = "v"})

function component.proxy(address)
	if proxyCache[address] then return proxyCache[address] end

	local t, err = component.type(address)
	if not t then return nil, err end
	local slot, err = component.slot(address)
	if not slot then return nil, err end

	local proxy = {address = address, type = t, slot = slot}
	proxy.fields, err = component.fields(address)
	if not proxy.fields then return nil, err end

	local methods = component.methods(address)
	for name in pairs(methods) do
		proxy[name] = setmetatable({address=address,name=name}, componentCallback)
	end

	setmetatable(proxy, componentProxy)
	proxyCache[address] = proxy
	return proxy
end

function computer.getProgramLocations()
	return {}
end

local shutdown, setArch = computer.shutdown, computer.setArchitecture

function computer.shutdown(...)
	shutdown(...)
	sysyield()
end

function computer.setArchitecture(arch)
	if arch == computer.getArchitecture() then return end
	local ok, err = setArch(arch)
	sysyield()
	return ok, err
end

function computer.pullSignal(timeout)
	timeout = timeout or math.huge
	local deadline = computer.uptime() + timeout
	while true do
		if computer.uptime() >= deadline then return end
		local t = {computer.popSignal()}
		if #t == 0 then
			sysyield()
		else
			return table.unpack(t)
		end
	end
end

unicode.upper, unicode.lower = string.upper, string.lower

unicode.isWide = function(s)
	local c = unicode.sub(s, 1, 1)
	return unicode.wlen(c) > unicode.len(c)
end

unicode.wtrunc = function(str,space)
	space = space - 1
	return unicode.sub(str, 1, space)
end

unicode.reverse = string.reverse

unicode.charWidth = function(s)
	return 1
end

unicode.sub = function(str, a, b)
	if not b then b = utf8.len(str) end
	if not a then a = 1 end
	-- a = math.max(a,1)
	if a < 0 then
		-- negative
		a = utf8.len(str) + a + 1
	end
	if b < 0 then
		b = utf8.len(str) + b + 1
	end
	if a > b then return "" end
	if b >= utf8.len(str) then b = #str else b = utf8.offset(str,b+1)-1 end
	if a > utf8.len(str) then return "" end
	a = utf8.offset(str,a)
	return str:sub(a,b)
	-- return str:sub(a, b)
end

function checkArg(arg, val, ...)
	local t = {...}
	for i=1,#t do
		if type(val) == t[i] then return end
	end
	error("bad argument #" .. arg .. " (" .. table.concat(t, ", ") .. ") expected", 2)
end

if os.getenv("NN_REPL") == "1" then
	while true do
		io.write("\x1b[34mlua>\x1b[0m ")
		io.flush()
		local l = io.read("l")
		if not l then break end
		local f, err = load("return " .. l, "=repl")
		if f then
			print(f())
		else
			f, err = load(l, "=repl")
			if f then f() else print(err) end
		end
		yield()
	end
	io.write("\n")
	print("exiting repl")
end

-- Save on just a tiny smudgeon of RAM
io = nil
package = nil

local sandbox
sandbox = {
	_VERSION = _VERSION,
	assert = assert,
	error = error,
	getmetatable = getmetatable,
	ipairs = ipairs,
	load = function(chunk, name, ty, env)
		return load(chunk, name, ty, env or sandbox)
	end,
	next = next,
	pairs = pairs,
	pcall = pcall,
	rawequal = rawequal,
	rawget = rawget,
	rawlen = rawlen,
	rawset = rawset,
	select = select,
	setmetatable = setmetatable,
	tonumber = tonumber,
	tostring = tostring,
	type = type,
	xpcall = xpcall,
	collectgarbage = function() collectgarbage("collect") end,

	bit32 = bit32,

	coroutine = {
		create = coroutine.create,
		resume = coroutine.resume,
		running = coroutine.running,
		status = coroutine.status,
		wrap = coroutine.wrap,
		yield = coroutine.yield,
		isyieldable = coroutine.isyieldable,
	},

	debug = {
		getinfo = debug.getinfo,
		traceback = debug.traceback,
		getlocal = debug.getlocal,
		getupvalue = debug.getupvalue,
		print = debug.print,
	},

	math = {
		abs = math.abs,
		acos = math.acos,
		asin = math.asin,
		atan = math.atan,
		atan2 = math.atan2,
		ceil = math.ceil,
		cos = math.cos,
		cosh = math.cosh,
		deg = math.deg,
		exp = math.exp,
		floor = math.floor,
		fmod = math.fmod,
		frexp = math.frexp,
		huge = math.huge,
		ldexp = math.ldexp,
		log = math.log,
		max = math.max,
		min = math.min,
		modf = math.modf,
		pi = math.pi,
		pow = math.pow,
		rad = math.rad,
		random = math.random,
		randomseed = math.randomseed,
		sin = math.sin,
		sinh = math.sinh,
		sqrt = math.sqrt,
		tan = math.tan,
		tanh = math.tanh,
		maxinteger = math.maxinteger,
		mininteger = math.mininteger,
		type = math.type,
		ult = math.ult,
	},

	os = {
		clock = os.clock,
		date = os.date,
		difftime = os.difftime,
		time = os.time,
	},

	string = {
		byte = string.byte,
		char = string.char,
		dump = string.dump,
		find = string.find,
		format = string.format,
		gmatch = string.gmatch,
		gsub = string.gsub,
		len = string.len,
		lower = string.lower,
		match = string.match,
		rep = string.rep,
		reverse = string.reverse,
		sub = string.sub,
		upper = string.upper,
		pack = string.pack,
		unpack = string.unpack,
		packsize = string.packsize,
	},

	table = {
		concat = table.concat,
		insert = table.insert,
		pack = table.pack,
		remove = table.remove,
		sort = table.sort,
		unpack = table.unpack,
		move = table.move,
	},

	checkArg = checkArg,

	component = {
		doc = component.doc,
		fields = component.fields,
		invoke = component.invoke,
		list = component.list,
		methods = component.methods,
		proxy = component.proxy,
		slot = component.slot,
		type = component.type,
	},

	computer = {
		address = computer.address,
		addUser = computer.addUser,
		beep = computer.beep,
		energy = computer.energy,
		freeMemory = computer.freeMemory,
		getArchitectures = computer.getArchitectures,
		getArchitecture = computer.getArchitecture,
		getDeviceInfo = computer.getDeviceInfo,
		getProgramLocations = computer.getProgramLocations,
		isRobot = computer.isRobot,
		maxEnergy = computer.maxEnergy,
		pullSignal = computer.pullSignal,
		pushSignal = computer.pushSignal,
		removeUser = computer.removeUser,
		setArchitecture = computer.setArchitecture,
		shutdown = computer.shutdown,
		tmpAddress = computer.tmpAddress,
		totalMemory = computer.totalMemory,
		uptime = computer.uptime,
		users = computer.users,
	},

	unicode = {
		len = utf8.len,
		wlen = utf8.len, -- this can be very wrong.
		sub = function (str,a,b)
			if not b then b = utf8.len(str) end
			if not a then a = 1 end
			-- a = math.max(a,1)
			
			if a < 0 then
				-- negative
				
				a = utf8.len(str) + a + 1
			end
			
			if b < 0 then
				b = utf8.len(str) + b + 1
			end
			
			if a > b then return "" end
			
			if b >= utf8.len(str) then b = #str else b = utf8.offset(str,b+1)-1 end
			
			if a > utf8.len(str) then return "" end
			a = utf8.offset(str,a)
			
			return str:sub(a,b)
			-- return str:sub(a, b)
		end,
		char = utf8.char,
		wtrunc = function (str,space)
			space = space - 1
			return str:sub(1,(space >= utf8.len(str)) and (#str) or (utf8.offset(str,space+1)-1))
		end,
		upper = string.upper, -- these are accurate... sometimes
		lower = string.lower,
		isWide = function ()
			return false
		end
	},

	utf8 = utf8,
}
sandbox._G = sandbox

local eeprom = component.list("eeprom", true)()
assert(eeprom, "missing firmware")

-- this would automatically reboot us if it needs to be a different architecture
local arch = component.invoke(eeprom, "getArchitecture")
if arch then computer.setArchitecture(arch) end

local code = assert(component.invoke(eeprom, "get"))
local f = assert(load(code, "=bios", nil, sandbox))
local thread = coroutine.create(f)

while true do
	collectgarbage("collect")
	if _SYNCED then
		if syncedMethodStats then
			--debug.print("calling synced method")
			syncedMethodStats = {realInvoke(table.unpack(syncedMethodStats))}
		end
	else
		local ok, err = resume(thread)
		if not ok then
			print(debug.traceback(thread, err))
		end
		if coroutine.status(thread) == "dead" then break end
	end
	yield()
end
