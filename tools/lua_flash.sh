
PORT=$1
BAUD=$2
BIN_ADDR=$3
BIN_SIZE=$4
LUA_CHOOSE=$5
FLASH_CHOOSE=$6
MONITOR=$7
ESP_LUA_LIB_PATH="components/esp_lua_lib"
if [ ! -n "$PORT" ] ;then
    PORT="/dev/ttyUSB0"
fi
if [ ! -n "$BAUD" ] ;then
    BAUD="921600"
fi
if [ ! -n "$BIN_ADDR" ] ;then
    BIN_ADDR="0x300000"
fi
if [ ! -n "$BIN_SIZE" ] ;then
    BIN_SIZE="0x100000"
fi
if [ ! -n "$MONITOR" ] ;then
    MONITOR=1
fi
echo "Choose new or old files(0=user, 1=new, 2=old)"
echo "enter (0/1/2, default 0):"
if [ ! -n "$LUA_CHOOSE" ] ;then
    read input
else
    input=$LUA_CHOOSE
fi

if [ "$input" == 1 ]; then
    echo "copy new files from esp_lua_lib"
    if [ -d "lua" ]; then
        rm -r lua
    fi
    cp -r $ESP_LUA_LIB_PATH/lua lua
    cp -r $ESP_LUA_LIB_PATH/html/* lua
elif [ "$input" == 2 ]; then
    echo "read files from flash"
    esptool.py -p $PORT -b $BAUD read_flash $BIN_ADDR $BIN_SIZE lua.bin
    if [ "$?" != 0 ] ;then
        exit 1
    else
        exit 0
    fi
    ./$ESP_LUA_LIB_PATH/tools/mkspiffs -u lua lua.bin
else 
    if [ -d "lua" ]; then
        echo "user lua files"
    else
        echo "Directory ./lua does not exists."
        exit 1
    fi
fi

echo "Choose flash or exit(0=lua, 1=all, 2=app)"
echo "enter (0/1/2, default 0):"
if [ ! -n "$FLASH_CHOOSE" ] ;then
    read input
else
    input=$FLASH_CHOOSE
fi

if [ "$input" == 1 ]; then
    echo "copy main.lua"
    cp main/*.lua lua
    ./$ESP_LUA_LIB_PATH/tools/mkspiffs -c lua -b 4096 -p 256 -s $BIN_SIZE lua.bin
    if [ "$?" != 0 ] ;then
        exit 1
    fi
    echo "flash spiffs bin and app"
    esptool.py -p $PORT -b $BAUD write_flash $BIN_ADDR lua.bin
    if [ "$?" != 0 ] ;then
        exit 1
    fi
    idf.py -p $PORT -b $BAUD build flash
    if [ "$?" != 0 ] ;then
        exit 1
    fi
elif [ "$input" == 2 ]; then
    echo "flash app"
    idf.py -p $PORT -b $BAUD build flash
else
    echo "copy main.lua"
    cp main/*.lua lua
    ./$ESP_LUA_LIB_PATH/tools/mkspiffs -c lua -b 4096 -p 256 -s $BIN_SIZE lua.bin
    if [ "$?" != 0 ] ;then
        exit 1
    fi
    echo "flash spiffs bin and app"
    esptool.py -p $PORT -b $BAUD write_flash $BIN_ADDR lua.bin
    if [ "$?" != 0 ] ;then
        exit 1
    fi
fi

if [ "$MONITOR" == 0 ] ;then
    echo "exit"
else
    idf.py -p $PORT -b $BAUD monitor
    if [ "$?" != 0 ] ;then
        exit 1
    fi
fi