-- sandbox stuff

local function copy(v, p)
    if type(v) == "table" then
        local t = {}
        for key, val in pairs(v) do
            t[key] = copy(val)
        end
        if p then
            for key, val in pairs(p) do
                t[key] = copy(val)
            end
        end
        return t
    else
        return v
    end
end

local function spcall(f, ...)
    local t = {pcall(f, ...)}
    if t[1] then
        return table.unpack(t, 2)
    end
    return nil, t[2]
end

local function nextDeadline()
    return computer.uptime() + 5
end

local bubbleYield = false
local timeout = nextDeadline()

local function tooLong()
    return computer.uptime() >= timeout
end

local tooLongWithoutYielding = "too long without yielding"

local function nextHeatUp()
    return computer.uptime() + math.random() * 2 + 0.1
end

local heatInc = nextHeatUp()

debug.sethook(function()
    if computer.uptime() >= heatInc then
        heatInc = nextHeatUp()
        computer.addHeat(math.random() * 3)
    end

    if tooLong() and not bubbleYield then
        bubbleYield = true
        error(tooLongWithoutYielding) -- here it is an actual string
    end
end, "c", 100000) -- no bogo mips, the check is cheap anyways

local function resume(co, val1, ...)
    while true do
        local t = {coroutine.resume(co, val1, ...)}
        if bubbleYield then -- yield was meaningless
            coroutine.yield() -- carry through
        else
            return table.unpack(t) -- yield the user cares about
        end
    end
end

local function yield()
    bubbleYield = true
    coroutine.yield()
end

local function ensureYields()
    if bubbleYield then
        coroutine.yield()
    end
end

local function checkArg(idx, v, ...)
    local bad = true
    local n = select("#", ...)
    for i=1,n do
        local t = select(i, ...)
        if type(v) == t then bad = false break end
    end
    if not bad then return end
    local msg = string.format("bad argument #%d (%s expected, got %s)", idx, table.concat({...}, " or "), type(v))
    error(msg, 3)
end

local libcomponent

local componentProxy = {
    __pairs = function(self)
        local method
        return function()
            method = next(self, method)
            if method then
                return method, self[method]
            end
        end
    end,
}

local componentCallback = {
    __call = function(self, ...)
        return libcomponent.invoke(self.address, self.name, ...)
    end,
    __tostring = function(self)
        return libcomponent.doc(self.address, self.name) or "undocumented"
    end
}

libcomponent = {
    invoke = function(addr, method, ...)
        checkArg(1, addr, "string")
        checkArg(2, method, "string")

        while true do
            local r = {pcall(component.invoke, addr, method, ...)}
            computer.clearError()

            -- in this situation, either the temperature is above 100 C and we throttle
            -- or the call budget has been filled and we dont care
            if computer.isOverheating() or computer.isOverworked() then
                local ok = pcall(yield)
                assert(ok, "component explicitly requested to be suspended")
            end

            if computer.getState() == states.blackout then
                -- oops, powerout
                local ok = pcall(yield)
                assert(ok, "blackout")
            end

            if computer.getState() == states.busy then
                -- busy gets to try again
                computer.setState(states.running)
            else
                if r[1] then
                    return table.unpack(r, 2)
                end
                return nil, r[2]
            end
        end
    end,
    list = function(filter, exact)
        checkArg(1, filter, "string", "nil")
        local t = component.list()
        local list = t
        if filter then
            list = {}
            for addr, kind in pairs(t) do
                if type(exact) == "boolean" and exact then
                    if kind == filter then
                        list[addr] = kind
                    end
                elseif rawequal(exact, "pattern") then
                    if string.match(kind, filter) then
                        list[addr] = kind
                    end
                else
                    if string.find(kind, filter, nil, true) then
                        list[addr] = kind
                    end
                end
            end
        end

        local key = nil
        return setmetatable(list, {
            __call = function()
                key = next(list, key)
                if key then
                    return key, list[key]
                end
            end,
        })
    end,
    methods = component.methods,
    fields = component.fields,
    doc = component.doc,
    slot = component.slot,
    type = component.type,
    proxy = function(addr)
        checkArg(1, addr, "string")
        if not component.type(addr) then return nil, "no such component" end
        local proxy = setmetatable({
            address = addr,
            type = component.type(addr),
            slot = component.slot(addr),
            fields = {},
        }, componentProxy)
        local methods = component.methods(addr)
        for method in pairs(methods) do
            proxy[method] = setmetatable({address = addr, name = method}, componentCallback)
        end
        return proxy
    end,
}

local libcomputer = {
    isRobot = function()
        return libcomponent.list("robot", true) ~= nil
    end,
    address = computer.address,
    tmpAddress = computer.tmpAddress,
    usedMemory = computer.usedMemory,
    freeMemory = computer.freeMemory,
    totalMemory = computer.totalMemory,
    uptime = computer.uptime,
    energy = computer.energy,
    maxEnergy = computer.maxEnergy,
    users = computer.users,
    -- these 2 are not actually implemented
    -- TODO: implement them
    addUser = computer.addUser,
    removeUser = computer.removeUser,

    shutdown = function(reboot)
        computer.setState(reboot and states.REPEAT or states.closing)
        yield()
    end,
    pushSignal = computer.pushSignal,
    pullSignal = function(timeout)
        local deadline = computer.uptime() + (type(timeout) == "number" and timeout or math.huge)

        repeat
            yield() -- give executor a chance to give us stuff
            local s = table.pack(computer.popSignal())
            if s.n > 0 then
                return table.unpack(s)
            end
        until computer.uptime() >= deadline
    end,
    beep = computer.beep,
    getDeviceInfo = function()
        return {} -- yup
    end,
    getProgramLocations = function()
        return {} -- yup
    end,

    getArchitectures = computer.getArchitectures,
    getArchitecture = computer.getArchitecture,
    setArchitecture = function(...)
        computer.setArchitecture(...) -- also sets state to SWITCH
        yield()
    end,
    getTemperature = computer.getTemperature,
}

local sandbox
sandbox = {
    assert = assert,
    error = error,
    getmetatable = function(t)
        if type(t) == "string" then -- HUGE security problem
            return nil -- fixed
        end
        return getmetatable(t)
    end,
    ipairs = ipairs,
    load = function(ld, source, _, env) -- mode is ignored as bytecode is just fully illegal for now
        return load(ld, source, "t", env or sandbox)
    end,
    next = next,
    pairs = pairs,
    pcall = function(...)
        if tooLong() then
            yield()
            return false, tooLongWithoutYielding
        end
        local t = {pcall(...)}
        ensureYields() -- if it took too long, this will make it yield
        return table.unpack(t)
    end,
    rawequal = rawequal,
    rawget = rawget,
    rawlen = rawlen,
    rawset = rawset,
    select = select,
    setmetatable = function(t, mt)
        if type(mt) ~= "table" then
            return setmetatable(t, mt)
        end
        -- we do mutate the metatable but this field shouldn't exist anyways
        mt.__gc = nil
        return setmetatable(t, mt)
    end,
    tonumber = tonumber,
    tostring = tostring,
    type = type,
    _VERSION = _VERSION,
    xpcall = function(f, msgh, ...)
        checkArg(1, f, "function")
        checkArg(2, msgh, "function")

        -- to prevent infinite loops we simply terminate the error handler if it took too long.
        local function errorCapture(ff, ...)
            --ensureYields() -- you can't yield in xpcall...
            -- Immediately dont care
            if tooLong() then
                return nil, tooLongWithoutYielding
            end
            -- This would mean you shutdown in the errorCapture.
            -- In vanilla OC, that does nothing.
            -- In here, it returns a suspended error and then yields eventually.
            if bubbleYield then
                return nil, "suspended"
            end

            return xpcall(ff, function(...)
                if tooLong() then
                    return tooLongWithoutYielding
                else
                    return select(2, errorCapture(msgh, ...))
                end
            end, ...)
        end

        local t = {errorCapture(f, ...)}
        pcall(ensureYields) -- it can fail if we are doing xpcall in xpcall.
        return table.unpack(t)
    end,

    coroutine = {
        create = coroutine.create,
        resume = resume,
        running = coroutine.running,
        status = coroutine.status,
        yield = coroutine.yield,
        wrap = function(f)
            -- uses the correct resume
            local co = coroutine.create(f)
            return function(...)
                local result = {resume(co, ...)}
                if result[1] then
                    return table.unpack(result, 2)
                else
                    error(result[2], 0)
                end
            end
        end,
        isyieldable = coroutine.isyieldable,
    },

    string = copy(string),
    table = copy(table),
    math = copy(math, {
        -- patch table
        atan2 = math.atan,
        ldexp = function(a, e) return a * (2.0 ^ e) end,
        pow = function(a, b) return a ^ b end,
    }),

    os = {
        clock = os.clock,
        date = os.date,
        difftime = function(t2, t1) return t2 - t1 end, -- thanks UNIX
        time = function(t)
            checkArg(1, t, "table", "nil")
            return os.time(t)
        end,
    },

    debug = {
        getinfo = function(...)
          local result = debug.getinfo(...)
          if result then
            return {
              source = result.source,
              short_src = result.short_src,
              linedefined = result.linedefined,
              lastlinedefined = result.lastlinedefined,
              what = result.what,
              currentline = result.currentline,
              nups = result.nups,
              nparams = result.nparams,
              isvararg = result.isvararg,
              name = result.name,
              namewhat = result.namewhat,
              istailcall = result.istailcall,
              -- believe it or not, this IS NOT safe.
              -- They may use this to re-call machine.lua which would reset the hook and timeout.
              -- TODO: make this safe.
              --func = result.func,
            }
          end
        end,
        traceback = debug.traceback,
        -- we only allow the first return, aka not the value.
        -- Otherwise, some dumb shmuck could do nasty stuff.
        -- TODO: make them not need this.
        getlocal = function(...) return (debug.getlocal(...)) end,
        getupvalue = function(...) return (debug.getupvalue(...)) end,
    },

    utf8 = copy(utf8),
    unicode = copy(unicode, {
        isWide = function(s)
            local c = unicode.sub(s, 1, 1)
            return unicode.wlen(c) > unicode.len(c)
        end,
        upper = string.upper,
        lower = string.lower,
		wtrunc = function (str,space)
			space = space - 1
            return unicode.sub(str, 1, space)
		end,
    }),
    checkArg = checkArg,
    component = libcomponent,
    computer = libcomputer,
    debugprint = print,
}
sandbox._G = sandbox

local function bootstrap()
    local eeprom = libcomponent.list("eeprom")()
    assert(eeprom, "no eeprom")

    local code = libcomponent.invoke(eeprom, "get")
    assert(code and #code > 0, "empty eeprom")

    return assert(load(code, "=bios", "t", sandbox))
end

coroutine.yield() -- startup delay

local f = bootstrap()
local co = coroutine.create(f)

local gcInterval = 0.25
local lastGC = computer.uptime()

while true do
    timeout = nextDeadline()
    bubbleYield = false
    collectgarbage()

    if computer.uptime() - lastGC >= gcInterval then
        collectgarbage("collect")
        lastGC = computer.uptime()
    end

    local ok, err = coroutine.resume(co)

    if not ok then
        error(debug.traceback(co, err), 0)
    elseif coroutine.status(co) == "dead" then
        error("computer halted", 0)
    else
        coroutine.yield()
    end
end
