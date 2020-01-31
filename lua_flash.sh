
PORT=$1
BAUD=$2
BIN_ADDR=$3
BIN_SIZE=$4
if [ ! -n "$PORT" ] ;then
    PORT="/dev/ttyUSB0"
fi
if [ ! -n "$BAUD" ] ;then
    BAUD="921600"
fi
if [ ! -n "$BIN_ADDR" ] ;then
    BIN_ADDR="0x210000"
fi
if [ ! -n "$BIN_SIZE" ] ;then
    BIN_SIZE="0x1f0000"
fi
echo "Choose old or new files(0=old, 1=new, 2=user)"
echo "enter (0/1/2, default 0):"
read input

if [ "$input" == 1 ]; then
    echo "copy new files from esp_lua_lib"
    if [ -d "lua" ]; then
        rm -r lua
    fi
    cp -r components/esp_lua_lib/lua_lib lua
elif [ "$input" == 2 ]; then
    if [ -d "lua" ]; then
        echo "user lua files"
    else
        echo "Directory ./lua does not exists."
        exit 1
    fi
else 
    echo "read files from flash"
    esptool.py -p $PORT -b $BAUD read_flash $BIN_ADDR $BIN_SIZE lua.bin
    ./components/esp_lua_lib/mkspiffs -u lua lua.bin
fi

echo "Choose flash or exit(0=all, 1=app, 2=exit)"
echo "enter (0/1, default 0):"
read input

if [ "$input" == 1 ]; then
    echo "flash app"
    idf.py -p $PORT -b $BAUD build flash monitor
elif [ "$input" == 2 ]; then
    exit 0
else
    echo "copy main.lua"
    cp main/main.lua lua
    ./components/esp_lua_lib/mkspiffs -c lua -b 4096 -p 256 -s $BIN_SIZE lua.bin
    echo "flash spiffs bin and app"
    esptool.py -p $PORT -b $BAUD write_flash $BIN_ADDR lua.bin
    idf.py -p $PORT -b $BAUD build flash monitor
fi
exit 0
