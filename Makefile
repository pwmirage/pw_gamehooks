OBJECTS = main.o pw_api.o gamehook.o
CFLAGS := -m32 -O3 -MD -MP $(CFLAGS)
CFLAGS += -DHOOK_BUILD_DATE=\"$(shell date +'%b\ %e')\"

$(shell mkdir -p build &>/dev/null)

all: build/gamehook.dll

clean:
	rm -f $(OBJECTS:%.o=build/%.o) $(OBJECTS:%.o=build/%.d)

build/gamehook.dll: $(OBJECTS:%.o=build/%.o)
	gcc $(CFLAGS) -o $@ -s -shared $^ -Wl,--subsystem,windows

build/%.o: %.c
	gcc $(CFLAGS) -c -o $@ $<

build/gamehook.o: gamehook.rc
	windres -i $< -o $@

-include $(OBJECTS:%.o=build/%.d)
