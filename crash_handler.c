#define PACKAGE "gamehook.dll"
#define PACKAGE_VERSION "1.0"

#include <windows.h>
#include <excpt.h>
#include <imagehlp.h>
#include <bfd.h>
#include <psapi.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>

#include "extlib.h"

#define BUFFER_MAX (16*1024)

#define BFD_ERR_OK          (0)
#define BFD_ERR_OPEN_FAIL   (1)
#define BFD_ERR_BAD_FORMAT  (2)
#define BFD_ERR_NO_SYMBOLS  (3)
#define BFD_ERR_READ_SYMBOL (4)

struct bfd_ctx {
	bfd * handle;
	asymbol ** symbol;
};

struct bfd_set {
	char * name;
	struct bfd_ctx * bc;
	struct bfd_set *next;
};

struct find_info {
	asymbol **symbol;
	bfd_vma counter;
	const char *file;
	const char *func;
	unsigned line;
	bool found;
};

static void
lookup_section(bfd *abfd, asection *sec, void *opaque_data)
{
	struct find_info *data = opaque_data;

	if (data->found)
		return;

	if (!(bfd_get_section_flags(abfd, sec) & SEC_ALLOC))
		return;

	bfd_vma vma = bfd_get_section_vma(abfd, sec);
	if (data->counter < vma || vma + bfd_get_section_size(sec) <= data->counter)
		return;

	data->found = true;
	bfd_find_nearest_line(abfd, sec, data->symbol, data->counter - vma,
			&(data->file), &(data->func), &(data->line));
}

static struct find_info
find(struct bfd_ctx * b, DWORD offset)
{
	struct find_info data;

	data.symbol = b ? b->symbol : NULL;
	data.counter = offset;
	data.file = NULL;
	data.func = NULL;
	data.line = 0;
	data.found = false;

	if (b) {
		bfd_map_over_sections(b->handle, &lookup_section, &data);
	}

	return data;
}

static int
init_bfd_ctx(struct bfd_ctx *bc, const char * procname, int *err)
{
	bc->handle = NULL;
	bc->symbol = NULL;

	bfd *b = bfd_openr(procname, 0);
	if (!b) {
		if(err) { *err = BFD_ERR_OPEN_FAIL; }
		return 1;
	}

	if(!bfd_check_format(b, bfd_object)) {
		bfd_close(b);
		if(err) { *err = BFD_ERR_BAD_FORMAT; }
		return 1;
	}

	if(!(bfd_get_file_flags(b) & HAS_SYMS)) {
		bfd_close(b);
		if(err) { *err = BFD_ERR_NO_SYMBOLS; }
		return 1;
	}

	void *symbol_table;

	unsigned dummy = 0;
	if (bfd_read_minisymbols(b, FALSE, &symbol_table, &dummy) == 0) {
		if (bfd_read_minisymbols(b, TRUE, &symbol_table, &dummy) < 0) {
			free(symbol_table);
			bfd_close(b);
			if(err) { *err = BFD_ERR_READ_SYMBOL; }
			return 1;
		}
	}

	bc->handle = b;
	bc->symbol = symbol_table;

	if(err) { *err = BFD_ERR_OK; }
	return 0;
}

static void
close_bfd_ctx(struct bfd_ctx *bc)
{
	if (bc) {
		if (bc->symbol) {
			free(bc->symbol);
		}
		if (bc->handle) {
			bfd_close(bc->handle);
		}
	}
}

static struct bfd_ctx *
get_bc(struct bfd_set *set , const char *procname, int *err)
{
	while(set->name) {
		if (strcmp(set->name , procname) == 0) {
			return set->bc;
		}
		set = set->next;
	}
	struct bfd_ctx bc;
	if (init_bfd_ctx(&bc, procname, err)) {
		return NULL;
	}
	set->next = calloc(1, sizeof(*set));
	set->bc = malloc(sizeof(struct bfd_ctx));
	memcpy(set->bc, &bc, sizeof(bc));
	set->name = strdup(procname);

	return set->bc;
}

static void
release_set(struct bfd_set *set)
{
	while(set) {
		struct bfd_set * temp = set->next;
		free(set->name);
		close_bfd_ctx(set->bc);
		free(set);
		set = temp;
	}
}

const char *
except_code_to_str(int code)
{
	switch(code)
	{
		case EXCEPTION_ACCESS_VIOLATION:
			return "EXCEPTION_ACCESS_VIOLATION";
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
			return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
		case EXCEPTION_BREAKPOINT:
			return "EXCEPTION_BREAKPOINT";
		case EXCEPTION_DATATYPE_MISALIGNMENT:
			return "EXCEPTION_DATATYPE_MISALIGNMENT";
		case EXCEPTION_FLT_DENORMAL_OPERAND:
			return "EXCEPTION_FLT_DENORMAL_OPERAND";
		case EXCEPTION_FLT_DIVIDE_BY_ZERO:
			return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
		case EXCEPTION_FLT_INEXACT_RESULT:
			return "EXCEPTION_FLT_INEXACT_RESULT";
		case EXCEPTION_FLT_INVALID_OPERATION:
			return "EXCEPTION_FLT_INVALID_OPERATION";
		case EXCEPTION_FLT_OVERFLOW:
			return "EXCEPTION_FLT_OVERFLOW";
		case EXCEPTION_FLT_STACK_CHECK:
			return "EXCEPTION_FLT_STACK_CHECK";
		case EXCEPTION_FLT_UNDERFLOW:
			return "EXCEPTION_FLT_UNDERFLOW";
		case EXCEPTION_ILLEGAL_INSTRUCTION:
			return "EXCEPTION_ILLEGAL_INSTRUCTION";
		case EXCEPTION_IN_PAGE_ERROR:
			return "EXCEPTION_IN_PAGE_ERROR";
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
			return "EXCEPTION_INT_DIVIDE_BY_ZERO";
		case EXCEPTION_INT_OVERFLOW:
			return "EXCEPTION_INT_OVERFLOW";
		case EXCEPTION_INVALID_DISPOSITION:
			return "EXCEPTION_INVALID_DISPOSITION";
		case EXCEPTION_NONCONTINUABLE_EXCEPTION:
			return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
		case EXCEPTION_PRIV_INSTRUCTION:
			return "EXCEPTION_PRIV_INSTRUCTION";
		case EXCEPTION_SINGLE_STEP:
			return "EXCEPTION_SINGLE_STEP";
		case EXCEPTION_STACK_OVERFLOW:
			return "EXCEPTION_STACK_OVERFLOW";
		default:
			return "Unrecognized Exception";
	}
}

const char *
get_basename(const char *in)
{
	const char *ret = in;
	char c;

	while ((c = *in++)) {
		if (c == '\\' || c == '/') {
			ret = in;
		}
	}

	return ret;
}

static size_t
snprint_stacktrace(char *buf, size_t bufsize, CONTEXT *context)
{
	HANDLE process = GetCurrentProcess();
	HANDLE thread = GetCurrentThread();
	char symbolBuffer[sizeof(IMAGEHLP_SYMBOL) + 255] = {};
	IMAGEHLP_SYMBOL *symbol = (IMAGEHLP_SYMBOL*) symbolBuffer;
	char module_name_raw[MAX_PATH];
	STACKFRAME frame = {0};
	size_t buf_off = 0;
	unsigned displacement = 0;
	int depth = 0;
	int err = BFD_ERR_OK;

	symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL) + 255;
	symbol->MaxNameLength = 254;

	SymInitialize(process, 0, true);

	bfd_init();
	struct bfd_set *set = calloc(1,sizeof(*set));

	/* setup initial stack frame */
	frame.AddrPC.Offset         = context->Eip;
	frame.AddrPC.Mode           = AddrModeFlat;
	frame.AddrStack.Offset      = context->Esp;
	frame.AddrStack.Mode        = AddrModeFlat;
	frame.AddrFrame.Offset      = context->Ebp;
	frame.AddrFrame.Mode        = AddrModeFlat;

	while (StackWalk(IMAGE_FILE_MACHINE_I386, process,
				thread, &frame,
				context, 0, SymFunctionTableAccess,
				SymGetModuleBase, 0)) {
		uintptr_t addr = (uintptr_t)frame.AddrPC.Offset;
		DWORD module_base = SymGetModuleBase(process, addr);
		struct bfd_ctx *bc = NULL;
		const char *module_name = "[unknown module]";
		const char *symbol_name = "?";

		if (module_base && GetModuleFileNameA((HINSTANCE)module_base,
					module_name_raw, MAX_PATH)) {
			module_name = get_basename(module_name_raw);
			if (strncmp(module_name, "gamehook", 8) == 0 ||
					strncmp(module_name, "game.exe", 8) == 0) {
				bc = get_bc(set, module_name_raw, &err);
			}
		}

		if (SymGetSymFromAddr(process, addr,
					(DWORD*)&displacement, symbol)) {
			symbol_name = symbol->Name;
		}

		struct find_info data = find(bc, addr);
		if (data.func) {
			buf_off += snprintf(buf + buf_off, bufsize - buf_off,
					"    [%d] 0x%08x => %s %s:%u %s\r\n", depth++,
					addr, module_name,
					get_basename(data.file), data.line, data.func);
		} else {
			buf_off += snprintf(buf + buf_off, bufsize - buf_off,
					"    [%d] 0x%08x => %s %s\r\n", depth++,
					addr, module_name, symbol_name);
		}
	}

	SymCleanup(process);
	release_set(set);

	return buf_off;
}

static HWND g_parent_window;
static HINSTANCE g_instance;
static const char *g_crash_log = NULL;

static BOOL CALLBACK
hwnd_set_font(HWND child, LPARAM font)
{
	SendMessage(child, WM_SETFONT, font, TRUE);
	return TRUE;
}

static void
init_gui(HWND hwnd, HINSTANCE hInst)
{
	HICON hIcon= (HICON)GetClassLong(g_parent_window, GCL_HICON);
	SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

	HWND log = CreateWindow("Edit", g_crash_log,
			WS_VISIBLE | WS_CHILD | WS_GROUP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
			10, 10, 560, 267, hwnd, (HMENU)0, hInst, 0);

	HWND quit_b = CreateWindow("Button", "OK",
			WS_VISIBLE | WS_CHILD | WS_TABSTOP, 487, 287, 83, 23,
			hwnd, (HMENU)1, hInst, 0);

	EnumChildWindows(hwnd, (WNDENUMPROC)hwnd_set_font,
			(LPARAM)GetStockObject(DEFAULT_GUI_FONT));
}

static LRESULT CALLBACK
WndProc(HWND hwnd, UINT msg, WPARAM arg1, LPARAM arg2)
{
	switch(msg) {
		case WM_CREATE:
		{
			init_gui(hwnd, g_instance);
			SetWindowPos(hwnd, HWND_TOPMOST, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
			break;
		}
		case WM_COMMAND:
			if ((int)LOWORD(arg1) == 1) {
				PostMessage(hwnd, WM_CLOSE, 0, 0);
			}
			break;
		case WM_CLOSE:
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
	}

	return DefWindowProcW(hwnd, msg, arg1, arg2);
}

static void
init_win(const char *log)
{
	WNDCLASSW wc = {0};
	HINSTANCE hInst = g_instance;
	MSG msg;

	g_crash_log = log;

	wc.lpszClassName = L"MGCrash";
	wc.hInstance = hInst;
	wc.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
	wc.lpfnWndProc = WndProc;
	wc.hCursor = LoadCursor(0, IDC_ARROW);

	RegisterClassW(&wc);

	RECT rect = { 0, 0, 580, 320 };
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, FALSE);

	size_t x, y, w, h;


	w = rect.right - rect.left;
	h = rect.bottom - rect.top;
	x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
	y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

	CreateWindowW(wc.lpszClassName, L"PW has crashed!",
			(WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX)) | WS_VISIBLE,
			x, y, w, h, NULL, 0, hInst, 0);

	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

static crash_handler_cb g_crash_cb;
static void *g_crash_ctx;
static bool g_enabled = true;

APICALL void
handle_crash(void *winapi_exception_info)
{
	EXCEPTION_POINTERS *ExceptionInfo = winapi_exception_info;

	static int crashed = 0;
	char buf[8192];
	size_t buf_off = 0;

	if (!g_enabled) {
		return;
	}

	if (crashed) {
		/* another crash handler is still running, don't interrupt it */
		return;
	}
	crashed = 1;

	buf_off = snprintf(buf, sizeof(buf), "PW has crashed! Please send the following information to an admin.\r\n\r\n"
			"%s (0x%x) at 0x%08x\r\n",
			except_code_to_str(ExceptionInfo->ExceptionRecord->ExceptionCode),
			ExceptionInfo->ExceptionRecord->ExceptionCode,
			ExceptionInfo->ContextRecord->Eip);
	if (ExceptionInfo->ExceptionRecord->ExceptionCode != EXCEPTION_STACK_OVERFLOW) {
		buf_off += snprint_stacktrace(buf + buf_off, sizeof(buf) - buf_off, ExceptionInfo->ContextRecord);
	}

	if (g_crash_cb) {
		buf_off += g_crash_cb(buf + buf_off, sizeof(buf) - buf_off, &g_parent_window, g_crash_ctx);
	}

	init_win(buf);

	/* just exit */
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	*(int *)0x0 = 1;
}

static LONG WINAPI
winapi_crash_handler(EXCEPTION_POINTERS *ExceptionInfo)
{
	handle_crash(ExceptionInfo);
	return EXCEPTION_EXECUTE_HANDLER;
}

APICALL void
setup_crash_handler(crash_handler_cb cb, void *ctx)
{
	g_instance = (HINSTANCE)GetModuleHandle(NULL);
	g_crash_cb = cb;
	g_crash_ctx = ctx;
	g_enabled = true;

	SetUnhandledExceptionFilter(winapi_crash_handler);
}

APICALL void
remove_crash_handler(void)
{
	g_enabled = false;
}
