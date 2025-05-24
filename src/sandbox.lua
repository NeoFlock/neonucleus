-- sandbox stuff

local clist = component.list
function component.list(type, exact)
    local m = clist()
    if not type then return m end
    local t = {}
    for addr, kind in pairs(m) do
        if exact then
            if type == kind then
                t[addr] = kind
            end
        else
            if string.match(kind, type) then
                t[addr] = kind
            end
        end
    end
    setmetatable(t, {
        __call = function()
            return next(t)
        end,
    })
    return t
end

for addr, kind in pairs(component.list()) do
    if kind == "eeprom" then
        local data = component.invoke(addr, "get")
        assert(load(data, "=eeprom"))()
        return
    end
end

error("no bios")
