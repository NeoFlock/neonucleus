-- The boot-up code of the Lua architecture.
-- Extremely bad.
-- Do not use in a serious context, you will be hacked.
-- There is no sandboxing here.


local sysyieldobj = {}

local function sysyield()
	coroutine.yield(sysyieldobj)
end

local resume = coroutine.resume

function coroutine.resume(co, ...)
	local t = {resume(co, ...)}
	if t[1] and rawequal(t[2], sysyieldobj) then
		coroutine.yield(sysyieldobj)
	else
		return table.unpack(t)
	end
end

local clist, cinvoke, computer, component, print = component.list, component.invoke, computer, component, print
debug.print = print

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

function component.invoke(address, method, ...)
	local t = {pcall(cinvoke, address, method, ...)}
	if computer.energy() <= 0 then sysyield() end -- out of power
	if computer.isOverused() then sysyield() end -- overused

	if t[1] then
		return table.unpack(t, 2)
	end
	return nil, t[2]
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

function checkArg(arg, val, ...)
	local t = {...}
	for i=1,#t do
		if type(val) == t[i] then return end
	end
	error("bad argument #" .. arg .. " (" .. table.concat(t, ", ") .. ") expected", 2)
end

-- HORRENDOUS approximation
unicode = string

if os.getenv("NN_REPL") == "1" then
	while true do
		io.write("lua> ")
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
	end
	io.write("\n")
	print("exiting repl")
end

local eeprom = component.list("eeprom", true)()
assert(eeprom, "missing firmware")

local code = assert(component.invoke(eeprom, "get"))
local f = assert(load(code, "=bios"))

local ok, err = xpcall(f, debug.traceback)
if not ok then
	print(err)
end
