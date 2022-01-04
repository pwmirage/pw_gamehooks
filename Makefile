OBJECTS = main.o pw_api.o gamehook_rc.o common.o d3d.o game_config.o avl.o pw_item_desc.o idmap.o
LIB_OBJECTS = crash_handler.o extlib.o
CFLAGS := -m32 -O2 -g -MMD -MP -masm=intel $(CFLAGS)
CFLAGS += -DHOOK_BUILD_DATE="\"$(shell date +'%b %d')\""

$(shell mkdir -p build &>/dev/null)

all: build/gamehook.dll

clean:
	rm -f $(OBJECTS:%.o=build/%.o) $(OBJECTS:%.o=build/%.d)

build/gamehook.dll: $(OBJECTS:%.o=build/%.o) build/libgamehook.dll
	gcc $(CFLAGS) -o $@ -shared -fPIC $(filter %.o,$^) -Wl,--subsystem,windows -Wl,-Bstatic -lgdi32 -ld3d9 -ld3d8 -Wl,-Bdynamic -lkeystone build/libgamehook.dll -static-libgcc

build/libgamehook.dll: $(LIB_OBJECTS:%.o=build/%.o)
	gcc $(CFLAGS) -o $@ -shared -fPIC -Wl,--subsystem,windows  -Wl,-Bstatic  -Wl,--whole-archive -lcimgui -Wl,--no-whole-archive $(filter-out %/extlib.o,$^) -limm32 -limagehlp -lbfd -liberty -lz build/extlib.o -Wl,-Bdynamic -lgdi32 -static-libgcc

build/extlib.o: CFLAGS := -DDLLEXPORT=1 $(CFLAGS)
build/crash_handler.o: CFLAGS := -DDLLEXPORT=1 $(CFLAGS)

build/%.o: %.c
	gcc $(CFLAGS) -c -o $@ $<

build/gamehook_rc.o: gamehook.rc
	windres -i $< -o $@

# extra daemon for rebuilding the hook remotely
daemon: daemon.c
	gcc $(CFLAGS) -o build/gamedaemon.exe $^ -lws2_32

-include $(OBJECTS:%.o=build/%.d)
-include $(LIB_OBJECTS:%.o=build/%.d)
