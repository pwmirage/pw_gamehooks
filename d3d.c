/*-
 * The MIT License
 *
 * Copyright 2019 Darek Stojaczyk
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <windows.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <d3d9.h>

#include "common.h"
#include "d3d.h"

HRESULT (__stdcall *endScene_org)(LPDIRECT3DDEVICE9 pDevice);
LPDIRECT3DDEVICE9 g_device = NULL;

static void
draw_rect(int x, int y, int h, int w, D3DCOLOR color)
{
	D3DRECT r = { x, y, x + w, y + h };

	g_device->lpVtbl->Clear(g_device, 1, &r, D3DCLEAR_TARGET, color, 0, 0);
}

static HRESULT APIENTRY
hooked_endScene(LPDIRECT3DDEVICE9 device)
{
	if (!g_device) {
		g_device = device;
	}

	draw_rect(100, 100, 200, 200, D3DCOLOR_ARGB(50, 170, 170, 20));
	return endScene_org(g_device);
}

static int
get_end_scene_fn(HWND hwnd, void **endScene_fn)
{
	IDirect3D9* d3d;
	LPDIRECT3DDEVICE9 dummy_dev = NULL;
	D3DPRESENT_PARAMETERS d3dpp = {};
	HRESULT rc;

	d3d = Direct3DCreate9(D3D_SDK_VERSION);
	if (!d3d) {
		return rc;
	}

	d3dpp.Windowed = false;
	d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3dpp.hDeviceWindow = hwnd;

	rc = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT,
		    D3DDEVTYPE_HAL, d3dpp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
		    &d3dpp, &dummy_dev);
	if (rc != S_OK) {
		// retry in window mode
		d3dpp.Windowed = true;

		rc = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
				d3dpp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
				&d3dpp, &dummy_dev);
		if (rc != S_OK) {
			IDirect3D9_Release(d3d);
			return rc;
		}
    }

    *endScene_fn = (void *)dummy_dev->lpVtbl->EndScene;

    dummy_dev->lpVtbl->Release(dummy_dev);
    IDirect3D9_Release(d3d);
    return S_OK;
}

int
d3d_hook(HWND hwnd)
{
	int rc = get_end_scene_fn(hwnd, (void **)&endScene_org);
	if (rc != S_OK) {
		return rc;
	}

	trampoline_fn((void **)&endScene_org, 7, hooked_endScene);
	return 0;
}
