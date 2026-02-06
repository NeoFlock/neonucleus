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
	-- NOTE: user can trigger false sysyields
	-- by simply yielding an object who's
	-- __eql always returns true.
	if t[1] and t[2] == sysyieldobj then
		coroutine.yield(sysyieldobj)
	else
		return table.unpack(t)
	end
end

local clist = component.list

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

	setmetatable(desired, {__call = next})
	return desired
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

local eeprom = component.list("eeprom", true)()
assert(eeprom, "missing firmware")

local code = assert(component.invoke(eeprom, "get"))
local f = assert(load(code, "=bios"))

local ok, err = xpcall(f, debug.traceback)
if not ok then
	print(err)
end
