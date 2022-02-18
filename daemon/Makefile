OBJECTS = daemon.o daemon_gdb.o
CFLAGS := -m32 -O0 -ggdb -MMD -MP -masm=intel $(CFLAGS)
CFLAGS += -D_WIN32_WINNT=0x0601 -DHOOK_BUILD_DATE="\"$(shell date +'%b %d')\""

$(shell mkdir -p build &>/dev/null)

all: build/gamedaemon.exe

clean:
	rm -f $(OBJECTS:%.o=build/%.o) $(OBJECTS:%.o=build/%.d) build/gamedaemon.exe

build/gamedaemon.exe: $(OBJECTS:%.o=build/%.o)
	gcc $(CFLAGS) -o build/gamedaemon.exe $^ -lws2_32
	cp build/gamedaemon.exe ../build/gamedaemon.exe

build/%.o: %.c
	gcc $(CFLAGS) -c -o $@ $<

-include $(OBJECTS:%.o=build/%.d)