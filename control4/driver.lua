--[[
    CEC-Dongle Control4 Driver
    TV Proxy that masks HDMI-CEC commands via the CEC-Dongle REST API

    This driver presents as a standard TV to Control4, intercepting all
    power, volume, input, and transport commands and forwarding them
    as CEC commands through the ESP8266-based CEC-Dongle.
]]

-- ═══════════════════════════════════════════════════════════════════════════
-- CONSTANTS
-- ═══════════════════════════════════════════════════════════════════════════

DRIVER_VERSION = "1.0.0"
HTTP_TIMEOUT   = 5000    -- ms

-- CEC logical address names for logging
CEC_NAMES = {
    [0]  = "TV",
    [1]  = "Recording 1",
    [2]  = "Recording 2",
    [3]  = "Tuner 1",
    [4]  = "Playback 1",
    [5]  = "Audio System",
    [6]  = "Tuner 2",
    [7]  = "Tuner 3",
    [8]  = "Playback 2",
    [9]  = "Recording 3",
    [10] = "Tuner 4",
    [14] = "Free Use",
    [15] = "Broadcast",
}

-- HDMI input → CEC physical address mapping
-- Physical addresses follow the x.0.0.0 convention (HDMI port on root TV)
INPUT_PHYSICAL_ADDRESSES = {
    [1] = 0x1000,
    [2] = 0x2000,
    [3] = 0x3000,
    [4] = 0x4000,
    [5] = 0x5000,
    [6] = 0x6000,
    [7] = 0x7000,
    [8] = 0x8000,
}

-- Control4 input connection IDs
INPUT_BINDING_BASE = 2001 -- HDMI 1 = 2001, HDMI 2 = 2002, etc.

-- ═══════════════════════════════════════════════════════════════════════════
-- STATE
-- ═══════════════════════════════════════════════════════════════════════════

g_dongleIP      = ""
g_pollInterval  = 30
g_tvAddress     = 0
g_audioAddress  = 5
g_volumeTarget  = "audio"
g_powerOnCmd    = "image_view_on"
g_debugMode     = false
g_powerState    = "OFF"
g_currentInput  = 1
g_pollTimer     = nil
g_isOnline      = false

-- ═══════════════════════════════════════════════════════════════════════════
-- LIFECYCLE
-- ═══════════════════════════════════════════════════════════════════════════

function OnDriverInit()
    dbg("Driver initialising v" .. DRIVER_VERSION)
    C4:UpdateProperty("Driver Status", "Initialising...")

    -- Read properties
    ReadProperties()

    -- Set up poll timer
    SetupPollTimer()
end

function OnDriverLateInit()
    dbg("Late init — starting first poll")
    PollStatus()
end

function OnDriverDestroyed()
    if g_pollTimer then
        C4:KillTimer(g_pollTimer)
        g_pollTimer = nil
    end
    dbg("Driver destroyed")
end

-- ═══════════════════════════════════════════════════════════════════════════
-- PROPERTIES
-- ═══════════════════════════════════════════════════════════════════════════

function OnPropertyChanged(strProperty)
    dbg("Property changed: " .. strProperty)

    if strProperty == "Dongle IP Address" then
        g_dongleIP = Properties["Dongle IP Address"] or ""
        PollStatus()

    elseif strProperty == "Poll Interval (seconds)" then
        g_pollInterval = tonumber(Properties["Poll Interval (seconds)"]) or 30
        SetupPollTimer()

    elseif strProperty == "CEC TV Logical Address" then
        g_tvAddress = tonumber(Properties["CEC TV Logical Address"]) or 0

    elseif strProperty == "CEC Audio Logical Address" then
        g_audioAddress = tonumber(Properties["CEC Audio Logical Address"]) or 5

    elseif strProperty == "Volume Control Target" then
        local val = Properties["Volume Control Target"] or ""
        if val:find("TV") then
            g_volumeTarget = "tv"
        elseif val:find("Broadcast") then
            g_volumeTarget = "broadcast"
        else
            g_volumeTarget = "audio"
        end

    elseif strProperty == "Power On Command" then
        local val = Properties["Power On Command"] or ""
        if val:find("Text View") then
            g_powerOnCmd = "text_view_on"
        elseif val:find("User Control") then
            g_powerOnCmd = "user_control_power"
        else
            g_powerOnCmd = "image_view_on"
        end

    elseif strProperty == "Debug Mode" then
        g_debugMode = (Properties["Debug Mode"] == "On")
    end
end

function ReadProperties()
    g_dongleIP     = Properties["Dongle IP Address"] or ""
    g_pollInterval = tonumber(Properties["Poll Interval (seconds)"]) or 30
    g_tvAddress    = tonumber(Properties["CEC TV Logical Address"]) or 0
    g_audioAddress = tonumber(Properties["CEC Audio Logical Address"]) or 5
    g_debugMode    = (Properties["Debug Mode"] == "On")

    local volProp = Properties["Volume Control Target"] or ""
    if volProp:find("TV") then g_volumeTarget = "tv"
    elseif volProp:find("Broadcast") then g_volumeTarget = "broadcast"
    else g_volumeTarget = "audio"
    end

    local pwrProp = Properties["Power On Command"] or ""
    if pwrProp:find("Text View") then g_powerOnCmd = "text_view_on"
    elseif pwrProp:find("User Control") then g_powerOnCmd = "user_control_power"
    else g_powerOnCmd = "image_view_on"
    end
end

-- ═══════════════════════════════════════════════════════════════════════════
-- TIMER
-- ═══════════════════════════════════════════════════════════════════════════

function SetupPollTimer()
    if g_pollTimer then
        C4:KillTimer(g_pollTimer)
        g_pollTimer = nil
    end
    if g_pollInterval > 0 then
        g_pollTimer = C4:AddTimer(g_pollInterval, "SECONDS", true)
        dbg("Poll timer set: " .. g_pollInterval .. "s")
    end
end

function OnTimerExpired(idTimer)
    if idTimer == g_pollTimer then
        PollStatus()
    end
end

-- ═══════════════════════════════════════════════════════════════════════════
-- REST API HELPERS
-- ═══════════════════════════════════════════════════════════════════════════

function BuildURL(path)
    if g_dongleIP == "" then return nil end
    return "http://" .. g_dongleIP .. path
end

--- Send a GET request to the dongle
function DoGet(path, callback)
    local url = BuildURL(path)
    if not url then
        dbg("No dongle IP configured")
        return
    end

    dbg("GET " .. url)
    C4:urlGet(url, {}, false,
        function(ticketId, strData, responseCode, tHeaders, strError)
            if responseCode == 200 and strData then
                if callback then callback(strData) end
            else
                dbg("GET failed: " .. (strError or "HTTP " .. tostring(responseCode)))
                SetOffline()
            end
        end
    )
end

--- Send a POST request with JSON body
function DoPost(path, body, callback)
    local url = BuildURL(path)
    if not url then
        dbg("No dongle IP configured")
        return
    end

    local jsonBody = body and C4:JsonEncode(body) or "{}"
    dbg("POST " .. url .. " body=" .. jsonBody)

    C4:urlPost(url, jsonBody,
        { ["Content-Type"] = "application/json" },
        false,
        function(ticketId, strData, responseCode, tHeaders, strError)
            if responseCode == 200 and strData then
                if callback then callback(strData) end
            else
                dbg("POST failed: " .. (strError or "HTTP " .. tostring(responseCode)))
            end
        end
    )
end

--- Send a CEC command via the dongle REST API
function SendCEC(destination, dataBytes, callback)
    local body = {
        destination = destination,
        data = dataBytes,
    }
    DoPost("/api/cec/send", body, callback)
end

-- ═══════════════════════════════════════════════════════════════════════════
-- STATUS POLLING
-- ═══════════════════════════════════════════════════════════════════════════

function PollStatus()
    DoGet("/api/status", function(strData)
        local ok, result = pcall(C4.JsonDecode, C4, strData)
        if ok and result then
            SetOnline()
            C4:UpdateProperty("Driver Status",
                "Connected | " .. (result.wifi_ip or "?") ..
                " | Heap: " .. tostring(result.heap_free or 0) .. "B" ..
                " | Up: " .. string.format("%.1f", (result.uptime_ms or 0) / 60000) .. "m")
        else
            SetOffline()
        end
    end)
end

function SetOnline()
    if not g_isOnline then
        g_isOnline = true
        C4:SendToProxy(1, "ONLINE_CHANGED", { STATE = "True" })
        dbg("Dongle is ONLINE")
    end
end

function SetOffline()
    if g_isOnline then
        g_isOnline = false
        C4:SendToProxy(1, "ONLINE_CHANGED", { STATE = "False" })
        C4:UpdateProperty("Driver Status", "Offline")
        dbg("Dongle is OFFLINE")
    end
end

-- ═══════════════════════════════════════════════════════════════════════════
-- VOLUME TARGET RESOLUTION
-- ═══════════════════════════════════════════════════════════════════════════

function GetVolumeDestination()
    if g_volumeTarget == "tv" then return g_tvAddress
    elseif g_volumeTarget == "broadcast" then return 15
    else return g_audioAddress
    end
end

-- ═══════════════════════════════════════════════════════════════════════════
-- TV PROXY COMMAND HANDLING
-- ═══════════════════════════════════════════════════════════════════════════

function ReceivedFromProxy(idBinding, strCommand, tParams)
    dbg("Proxy[" .. idBinding .. "] CMD: " .. strCommand .. " params=" .. TableToString(tParams or {}))

    -- ── Power ────────────────────────────────────────────────────────────
    if strCommand == "ON" then
        PowerOn()

    elseif strCommand == "OFF" then
        PowerOff()

    -- ── Input Selection ──────────────────────────────────────────────────
    elseif strCommand == "SET_INPUT" then
        local inputId = tonumber(tParams.INPUT) or 0
        -- Connection IDs: 2001=HDMI1, 2002=HDMI2, etc.
        local inputNum = inputId - INPUT_BINDING_BASE + 1
        if inputNum >= 1 and inputNum <= 8 then
            SetInput(inputNum)
        end

    -- ── Volume ───────────────────────────────────────────────────────────
    elseif strCommand == "MUTE_ON" or strCommand == "MUTE_OFF" or strCommand == "MUTE_TOGGLE" then
        MuteToggle()

    elseif strCommand == "VOLUME_UP" or strCommand == "UP" then
        VolumeUp()

    elseif strCommand == "VOLUME_DOWN" or strCommand == "DOWN" then
        VolumeDown()

    elseif strCommand == "SET_VOLUME_LEVEL" then
        -- CEC doesn't natively support absolute volume; send repeated commands
        -- For now, just log it
        dbg("SET_VOLUME_LEVEL requested: " .. tostring(tParams.LEVEL) .. " (not directly supported via CEC)")

    -- ── Menu/Navigation (passthrough as User Control Pressed) ────────────
    elseif strCommand == "NUMBER_0" then SendCEC(g_tvAddress, {0x44, 0x20})
    elseif strCommand == "NUMBER_1" then SendCEC(g_tvAddress, {0x44, 0x21})
    elseif strCommand == "NUMBER_2" then SendCEC(g_tvAddress, {0x44, 0x22})
    elseif strCommand == "NUMBER_3" then SendCEC(g_tvAddress, {0x44, 0x23})
    elseif strCommand == "NUMBER_4" then SendCEC(g_tvAddress, {0x44, 0x24})
    elseif strCommand == "NUMBER_5" then SendCEC(g_tvAddress, {0x44, 0x25})
    elseif strCommand == "NUMBER_6" then SendCEC(g_tvAddress, {0x44, 0x26})
    elseif strCommand == "NUMBER_7" then SendCEC(g_tvAddress, {0x44, 0x27})
    elseif strCommand == "NUMBER_8" then SendCEC(g_tvAddress, {0x44, 0x28})
    elseif strCommand == "NUMBER_9" then SendCEC(g_tvAddress, {0x44, 0x29})

    elseif strCommand == "UP" then    SendCEC(g_tvAddress, {0x44, 0x01})
    elseif strCommand == "DOWN" then  SendCEC(g_tvAddress, {0x44, 0x02})
    elseif strCommand == "LEFT" then  SendCEC(g_tvAddress, {0x44, 0x03})
    elseif strCommand == "RIGHT" then SendCEC(g_tvAddress, {0x44, 0x04})
    elseif strCommand == "ENTER" or strCommand == "SELECT" then
        SendCEC(g_tvAddress, {0x44, 0x00})
    elseif strCommand == "EXIT" then
        SendCEC(g_tvAddress, {0x44, 0x0D})
    elseif strCommand == "MENU" then
        SendCEC(g_tvAddress, {0x44, 0x09})
    elseif strCommand == "GUIDE" then
        SendCEC(g_tvAddress, {0x44, 0x0E})
    elseif strCommand == "INFO" then
        SendCEC(g_tvAddress, {0x44, 0x35})

    -- ── Transport Controls ───────────────────────────────────────────────
    elseif strCommand == "PLAY" then    SendCEC(g_tvAddress, {0x44, 0x44})
    elseif strCommand == "PAUSE" then   SendCEC(g_tvAddress, {0x44, 0x46})
    elseif strCommand == "STOP" then    SendCEC(g_tvAddress, {0x44, 0x45})
    elseif strCommand == "SKIP_FWD" then  SendCEC(g_tvAddress, {0x44, 0x4B})
    elseif strCommand == "SKIP_REV" then  SendCEC(g_tvAddress, {0x44, 0x4C})
    elseif strCommand == "SCAN_FWD" then  SendCEC(g_tvAddress, {0x44, 0x49})
    elseif strCommand == "SCAN_REV" then  SendCEC(g_tvAddress, {0x44, 0x48})
    elseif strCommand == "RECORD" then    SendCEC(g_tvAddress, {0x44, 0x47})

    -- ── Unhandled ────────────────────────────────────────────────────────
    else
        dbg("Unhandled proxy command: " .. strCommand)
    end
end

-- ═══════════════════════════════════════════════════════════════════════════
-- CEC COMMAND IMPLEMENTATIONS
-- ═══════════════════════════════════════════════════════════════════════════

function PowerOn()
    dbg("PowerOn -> CEC TV address " .. g_tvAddress)

    local data
    if g_powerOnCmd == "text_view_on" then
        data = {0x0D}
    elseif g_powerOnCmd == "user_control_power" then
        data = {0x44, 0x6D}
    else
        data = {0x04} -- Image View On
    end

    SendCEC(g_tvAddress, data, function(strData)
        g_powerState = "ON"
        C4:SendToProxy(1, "POWER_STATE_CHANGED", { POWER_STATE = "ON" })
        dbg("Power ON sent successfully")
    end)

    -- Also notify C4 right away (optimistic)
    g_powerState = "ON"
    C4:SendToProxy(1, "POWER_STATE_CHANGED", { POWER_STATE = "ON" })
end

function PowerOff()
    dbg("PowerOff -> CEC Standby, TV address " .. g_tvAddress)

    -- Send Standby to TV
    SendCEC(g_tvAddress, {0x36}, function(strData)
        g_powerState = "OFF"
        C4:SendToProxy(1, "POWER_STATE_CHANGED", { POWER_STATE = "OFF" })
        dbg("Power OFF sent successfully")
    end)

    -- Optimistic
    g_powerState = "OFF"
    C4:SendToProxy(1, "POWER_STATE_CHANGED", { POWER_STATE = "OFF" })
end

function SetInput(inputNum)
    dbg("SetInput -> HDMI " .. inputNum)

    local physAddr = INPUT_PHYSICAL_ADDRESSES[inputNum]
    if not physAddr then
        dbg("Invalid input number: " .. inputNum)
        return
    end

    -- Set Stream Path (broadcast)
    local hiB = math.floor(physAddr / 256)
    local loB = physAddr % 256
    SendCEC(15, {0x86, hiB, loB}, function(strData)
        g_currentInput = inputNum
        local bindingId = INPUT_BINDING_BASE + inputNum - 1
        C4:SendToProxy(1, "INPUT_CHANGED", { INPUT = tostring(bindingId) })
        dbg("Input switched to HDMI " .. inputNum)
    end)

    -- Optimistic
    g_currentInput = inputNum
    local bindingId = INPUT_BINDING_BASE + inputNum - 1
    C4:SendToProxy(1, "INPUT_CHANGED", { INPUT = tostring(bindingId) })
end

function VolumeUp()
    local dest = GetVolumeDestination()
    dbg("VolumeUp -> dest " .. dest)
    -- User Control Pressed: Volume Up (0x41)
    SendCEC(dest, {0x44, 0x41})
end

function VolumeDown()
    local dest = GetVolumeDestination()
    dbg("VolumeDown -> dest " .. dest)
    -- User Control Pressed: Volume Down (0x42)
    SendCEC(dest, {0x44, 0x42})
end

function MuteToggle()
    local dest = GetVolumeDestination()
    dbg("MuteToggle -> dest " .. dest)
    -- User Control Pressed: Mute (0x43)
    SendCEC(dest, {0x44, 0x43})
end

-- ═══════════════════════════════════════════════════════════════════════════
-- COMMANDS (from Composer Pro / Programming)
-- ═══════════════════════════════════════════════════════════════════════════

function ExecuteCommand(strCommand, tParams)
    dbg("ExecuteCommand: " .. strCommand)

    if strCommand == "Power On" then
        PowerOn()
    elseif strCommand == "Power Off" then
        PowerOff()
    elseif strCommand == "Volume Up" then
        VolumeUp()
    elseif strCommand == "Volume Down" then
        VolumeDown()
    elseif strCommand == "Mute Toggle" then
        MuteToggle()
    elseif strCommand == "Input HDMI 1" then SetInput(1)
    elseif strCommand == "Input HDMI 2" then SetInput(2)
    elseif strCommand == "Input HDMI 3" then SetInput(3)
    elseif strCommand == "Input HDMI 4" then SetInput(4)

    elseif strCommand == "Send Raw CEC" then
        local dest = tonumber(tParams.Destination) or 15
        local dataStr = tParams.Data or ""
        local dataBytes = ParseHexString(dataStr)
        if #dataBytes > 0 then
            SendCEC(dest, dataBytes)
        else
            dbg("Send Raw CEC: no valid data bytes")
        end

    -- Actions
    elseif strCommand == "PollStatus" then
        PollStatus()
    elseif strCommand == "RefreshLog" then
        DoGet("/api/cec/log", function(strData)
            dbg("CEC Log: " .. (strData or "empty"))
        end)
    end
end

-- ═══════════════════════════════════════════════════════════════════════════
-- ONLINE/OFFLINE
-- ═══════════════════════════════════════════════════════════════════════════

function OnlineChanged(strStatus)
    dbg("OnlineChanged: " .. tostring(strStatus))
end

-- ═══════════════════════════════════════════════════════════════════════════
-- UTILITY
-- ═══════════════════════════════════════════════════════════════════════════

function dbg(msg)
    if g_debugMode then
        print("[CEC-Dongle] " .. tostring(msg))
    end
end

function TableToString(t)
    if type(t) ~= "table" then return tostring(t) end
    local s = "{"
    for k, v in pairs(t) do
        s = s .. tostring(k) .. "=" .. tostring(v) .. ", "
    end
    return s .. "}"
end

function ParseHexString(str)
    -- Accept formats: "36", "44,41", "44 41", "0x44 0x41"
    local bytes = {}
    for hex in str:gmatch("[0-9A-Fa-f]+") do
        table.insert(bytes, tonumber(hex, 16))
    end
    return bytes
end
