OBJECTS = main.o input.o pw_api.o gamehook_rc.o common.o d3d.o csh.o csh_config.o avl.o pw_item_desc.o idmap.o window.o win_settings.o win_console.o win_misc.o
LIB_OBJECTS = crash_handler.o extlib.o avl.o
CFLAGS := -m32 -O2 -ggdb -MMD -MP -fno-strict-aliasing -masm=intel $(CFLAGS)
CFLAGS += -DHOOK_BUILD_DATE="\"$(shell TZ=UTC date +'%b %d %Y %I:%M %p UTC')\""

$(shell mkdir -p build &>/dev/null)

all: build/gamehook.dll

.PHONY: daemon

clean:
	rm -f $(OBJECTS:%.o=build/%.o) $(OBJECTS:%.o=build/%.d) $(LIB_OBJECTS:%.o=build/%.o) $(LIB_OBJECTS:%.o=build/%.d) build/libgamehook.dll build/gamehook.dll

build/gamehook.dll: $(OBJECTS:%.o=build/%.o) build/libgamehook.dll
	gcc $(CFLAGS) -o $@ -shared -fPIC $(filter %.o,$^) -Wl,--subsystem,windows -Wl,-Bstatic -lgdi32 -ld3d9 -ld3d8 -Wl,-Bdynamic -lkeystone build/libgamehook.dll -static-libgcc

build/libgamehook.dll: $(LIB_OBJECTS:%.o=build/%.o)
	gcc $(CFLAGS) -o $@ -shared -fPIC -Wl,--subsystem,windows  -Wl,-Bstatic  -Wl,--whole-archive -lcimgui -Wl,--no-whole-archive $(filter-out %/extlib.o,$^) -limm32 -limagehlp -lbfd -liberty -lz build/extlib.o -Wl,-Bdynamic -lgdi32 -static-libgcc

build/extlib.o: CFLAGS := -DDLLEXPORT=1 $(CFLAGS)
build/crash_handler.o: CFLAGS := -DDLLEXPORT=1 $(CFLAGS)

build/%.o: %.c
	gcc $(CFLAGS) -c -o $@ $<

build/%.o: %.cpp
	g++ -fno-exceptions $(CFLAGS) -c -o $@ $<

build/gamehook_rc.o: gamehook.rc
	windres -i $< -o $@

daemon:
	$(MAKE) -C daemon

-include $(OBJECTS:%.o=build/%.d)
-include $(LIB_OBJECTS:%.o=build/%.d)
