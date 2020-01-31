local json = require('json')
local dumpTable = require('dumpTable')

assert(wifi.init('CMCC-GPcG', 'etb3rzz7')) 
assert(sys.sntp('ntp1.aliyun.com'))

local base_url  = 'https://api.seniverse.com/v3/weather/now.json?key=lsawo7f7smtdljg9&language=zh-Hans&unit=c'

local mqtt_connected = false
web.mqtt('START', 'mqtt://mqtt.emake.run', 100)
local last_clock = os.time()
while (1) do
    event, data = web.mqtt('HANDLE', 10)
    if (event) then
        if (event == 'MQTT_EVENT_DATA') then
            printTable(json.decode(data))
        elseif (event == 'MQTT_EVENT_CONNECTED') then
            mqtt_connected = true
            web.mqtt('SUB', 'nc_temp', 0)
        elseif (event == 'MQTT_EVENT_DISCONNECTED') then
            mqtt_connected = false
        end
    end
    if (mqtt_connected and os.difftime (os.time(), last_clock) >= 30) then
        nc_temp = web.rest('GET', base_url..'&location=nanchang')
        web.mqtt('PUB', 'nc_temp', nc_temp, 0)
        last_clock = os.time()
        print('clock: '..os.clock()..', heap: '..sys.heap())
    end
end
