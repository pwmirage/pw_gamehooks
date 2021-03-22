OBJECTS = main.o pw_api.o gamehook.o common.o d3d.o
CFLAGS := -m32 -O3 -MD -MP -masm=intel $(CFLAGS)
CFLAGS += -DHOOK_BUILD_DATE=\"$(shell date +'%b\ %d')\"

$(shell mkdir -p build &>/dev/null)

all: build/gamehook.dll

clean:
	rm -f $(OBJECTS:%.o=build/%.o) $(OBJECTS:%.o=build/%.d)

build/gamehook.dll: $(OBJECTS:%.o=build/%.o)
	gcc $(CFLAGS) -o $@ -s -shared $^ -Wl,--subsystem,windows -lcimgui -lgdi32 -ld3d9 -limm32 -lstdc++

build/%.o: %.c
	gcc $(CFLAGS) -c -o $@ $<

build/gamehook.o: gamehook.rc
	windres -i $< -o $@

# extra daemon for rebuilding the hook remotely
daemon: daemon.c
	gcc $(CFLAGS) -o build/gamedaemon.exe $^ -lws2_32

-include $(OBJECTS:%.o=build/%.d)
