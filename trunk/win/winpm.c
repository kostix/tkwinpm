/*
 * winpm.c --
 *   Power Management support for Windows Tk applications.
 *
 * Copyright (c) 2007 Konstantin Khomoutov.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * $Id$
 */

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <tcl.h>
#include <tk.h>
#include <tkPlatDecls.h>

static int
Winpm_Cmd (
	ClientData clientData, /* Not used. */
	Tcl_Interp *interp,
	int objc,
	Tcl_Obj *const objv[]
	)
{
	return TCL_OK;
}

static void
Winpm_Cleanup(ClientData clientData)
{
	HWND hwndMonitor = (HWND) clientData;
	DestroyWindow(hwndMonitor);
}

static LRESULT CALLBACK
WndProc(
	HWND hwnd,
	UINT uMsg,
	WPARAM wParam,
	LPARAM lParam
)
{
	switch (uMsg) {
		case WM_QUERYENDSESSION:

		case WM_ENDSESSION:

		case WM_POWERBROADCAST:

		default:
			return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
}

static int
CreateMonitorWindow (
	Tcl_Interp *interp,
	HWND *phwnd
	)
{
	HINSTANCE  hinst;
	WNDCLASSEX wc;
	ATOM       rc;
	CHAR       title[] = "TkWinPMMonitorWindow";
	CHAR       name[]  = "TkWinPMMonitorWindowClass";

	hinst = Tk_GetHINSTANCE();

	ZeroMemory(&wc, sizeof(wc));
    
	wc.cbSize        = sizeof(WNDCLASSEX);
	wc.style         = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc   = (WNDPROC) WndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = hinst;
	wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
	wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH) COLOR_WINDOW;
	wc.lpszMenuName  = name;
	wc.lpszClassName = name;

	/* TODO error reporting */
    
	rc = RegisterClassEx(&wc);
	if (rc == 0) {
		return TCL_ERROR;
	}

	*phwnd = CreateWindow( name, title, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, NULL, hinst, NULL );
	if (*phwnd == NULL) {
		return TCL_ERROR;
	}

	SetWindowLongPtr(*phwnd, GWLP_USERDATA, (LONG)interp);

	ShowWindow(*phwnd, SW_HIDE);
	UpdateWindow(*phwnd);

	return TCL_OK;
}

#ifdef BUILD_winpm
#undef TCL_STORAGE_CLASS
#define TCL_STORAGE_CLASS DLLEXPORT
#endif /* BUILD_winpm */

EXTERN int
Winpm_Init(Tcl_Interp * interp)
{
	HWND hwndMonitor;

#ifdef USE_TCL_STUBS
	if (Tcl_InitStubs(interp, "8.1", 0) == NULL) {
		return TCL_ERROR;
	}
#endif
	if (Tcl_PkgRequire(interp, "Tcl", "8.1", 0) == NULL) {
		return TCL_ERROR;
	}

#ifdef USE_TK_STUBS
	if (Tk_InitStubs(interp, "8.1", 0) == NULL) {
		return TCL_ERROR;
	}
#endif
	if (Tcl_PkgRequire(interp, "Tk", "8.1", 0) == NULL) {
		return TCL_ERROR;
	}

	if (CreateMonitorWindow(interp, &hwndMonitor) != TCL_OK) {
		return TCL_ERROR;
	}

	Tcl_CreateObjCommand(interp, "winpm", (Tcl_ObjCmdProc *) Winpm_Cmd,
		(ClientData) hwndMonitor, (Tcl_CmdDeleteProc *) Winpm_Cleanup);

	if (Tcl_PkgProvide(interp, PACKAGE_NAME, PACKAGE_VERSION) != TCL_OK) {
		return TCL_ERROR;
	}

	return TCL_OK;
}

