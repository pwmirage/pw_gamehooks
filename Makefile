all: dll exe

.PHONY: dll exe

dll:
	gcc -m32 -O3 -c -o main.o main.c
	gcc -m32 -O3 -c -o pw_api.o pw_api.c
	gcc -m32 -o pw_tab.dll -s -shared main.c pw_api.o -Wl,--subsystem,windows

exe:
	gcc -m32 -O3 -o elementclient.exe pw_wrapper.c
