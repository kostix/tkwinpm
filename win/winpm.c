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

typedef struct {
	Tcl_Interp *interp; /* Interpreter to which this state belongs */
	HWND hwndMonitor; /* Handle to the monitoring window */
	Tk_BindingTable bindings;
} Winpm_InterpData;

static const ClientData BindObject = (ClientData) 0xBAADF00D;

static const char *HandledEvents[] = {
	"WM_QUERYENDSESSION",
	"WM_ENDSESSION",
	"WM_POWERBROADCAST",
	NULL
};

typedef struct {
	char token;
	char value[];
} Winpm_PercentMap;

static void
Winpm_ExpandPercents (
	Tcl_Interp *interp,
	Winpm_PercentMap *mapPtr,
	CONST char *before,
	Tcl_DString *dsPtr
	)
{
//	CONST char *string;

	while (1) {
		break;
	}
}

static int
Winpm_CheckEventName (
	Tcl_Interp *interp,
	Tcl_Obj *objPtr
	)
{
	int i;

	return Tcl_GetIndexFromObj(interp, objPtr,
			HandledEvents, "event", 0, &i);
}

/*
 * winpm bind
 * winpm bind WM_QUERYENDSESSION
 * winpm bind WM_QUERYENDSESSION SCRIPT
 */
static int
Winpm_CmdBind (
	Tcl_Interp *interp,
	Tk_BindingTable bindings,
	int objc,
	Tcl_Obj *const objv[]
	)
{
	switch (objc) {
		case 2: /* List all currently bound events */
			Tk_GetAllBindings(interp, bindings, BindObject);
			return TCL_OK;
		break;

		case 3: { /* Show binding for an event */
			CONST char *scriptPtr;

			if (Winpm_CheckEventName(interp,
						objv[2]) != TCL_OK) return TCL_ERROR;

			scriptPtr = Tk_GetBinding(interp, bindings,
					BindObject, Tcl_GetString(objv[2]));
			if (scriptPtr == NULL) {
				Tcl_ResetResult(interp);
			} else {
				Tcl_SetObjResult(interp,
						Tcl_NewStringObj(scriptPtr, -1));
			}
			return TCL_OK;
		}
		break;

		case 4: { /* Create or delete binding */
			char *s;
			long len;

			s = Tcl_GetStringFromObj(objv[3], &len);
			if (len == 0) {
				return Tk_DeleteBinding(interp, bindings,
						BindObject, Tcl_GetString(objv[2]));
			} else {
				unsigned long mask;
				int append;

				if (Winpm_CheckEventName(interp,
							objv[2]) != TCL_OK) return TCL_ERROR;

				append = s[0] == '+';
				if (append) ++s;

				mask = Tk_CreateBinding(interp, bindings,
						BindObject, Tcl_GetString(objv[2]), s, append);
				if (mask == 0) {
					return TCL_ERROR;
				} else {
					return TCL_OK;
				}
			}
		}
		break;

		default:
			Tcl_WrongNumArgs(interp, 2, objv, "?event? ?command?");
			return TCL_ERROR;
		break;
	}
}

/* winpm generate EVENT */
static int
Winpm_CmdGenerate (
	Tcl_Interp *interp,
	Tk_BindingTable bindings,
	int objc,
	Tcl_Obj *const objv[]
	)
{
	CONST char *scriptPtr;

	if (objc != 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "event");
		return TCL_ERROR;
	}

	if (Winpm_CheckEventName(interp,
				objv[2]) != TCL_OK) return TCL_ERROR;

	scriptPtr = Tk_GetBinding(interp, bindings,
			BindObject, Tcl_GetString(objv[2]));
	if (scriptPtr == NULL) {
		return TCL_OK;
	} else {
		int code = Tcl_GlobalEval(interp, scriptPtr);
		if (code == TCL_ERROR) {
			/* TODO format error message */
			return TCL_ERROR;
		} else {
			return TCL_OK;
		}
	}
}

/* winpm info arg ?arg ...? */
static int
Winpm_CmdInfo (
	Tcl_Interp *interp,
	Winpm_InterpData *statePtr,
	int objc,
	Tcl_Obj *const objv[]
	)
{
	static const char *topics[] = { "events", NULL };
	typedef enum { INF_EVENTS } INF_Option;
	int opt;

	if (objc < 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "topic ?arg ...?");
		return TCL_ERROR;
	}

	if (Tcl_GetIndexFromObj(interp, objv[2], topics, "topic",
			0, &opt) != TCL_OK) { return TCL_ERROR; }

	switch (opt) {
		case INF_EVENTS: {
			int i;
			CONST char *p;
			Tcl_Obj *listObj;

			listObj = Tcl_NewListObj(0, NULL);
			i = 0;
			p = HandledEvents[i];
			while (p != NULL) {
				Tcl_ListObjAppendElement(interp,
						listObj, Tcl_NewStringObj(p, -1));
				p = HandledEvents[++i];
			}
			Tcl_SetObjResult(interp, listObj);
			return TCL_OK;
		}
		break;
	}

	return TCL_OK;
}

static int
Winpm_Cmd (
	ClientData clientData, /* Not used. */
	Tcl_Interp *interp,
	int objc,
	Tcl_Obj *const objv[]
	)
{
	static const char *options[] = { "bind", "generate", "info", NULL };
	typedef enum { WPM_BIND, WPM_GENERATE, WPM_INFO } WPM_Option;
	int opt;
	Winpm_InterpData *statePtr;

	if (objc == 1) {
		Tcl_WrongNumArgs(interp, 1, objv,
				"option ?arg ...?");
		return TCL_ERROR;
	}

	if (Tcl_GetIndexFromObj(interp, objv[1], options, "option",
			0, &opt) != TCL_OK) { return TCL_ERROR; }

	statePtr = (Winpm_InterpData *) clientData;

	switch (opt) {
		case WPM_BIND:
			return Winpm_CmdBind(interp, statePtr->bindings, objc, objv);
		break;

		case WPM_GENERATE:
			return Winpm_CmdGenerate(interp, statePtr->bindings, objc, objv);
		break;

		case WPM_INFO:
			return Winpm_CmdInfo(interp, statePtr, objc, objv);
		break;

		default:
			Tcl_SetResult(interp, "Not suported", TCL_VOLATILE);
			return TCL_ERROR;
		break;
	}

	return TCL_OK;
}

static void
Winpm_Cleanup(ClientData clientData)
{
	Winpm_InterpData *pdata = (Winpm_InterpData *) clientData;

	Tk_DeleteBindingTable(pdata->bindings);
	DestroyWindow(pdata->hwndMonitor);

	ckfree((char *) pdata);
}

static LRESULT
Winpm_ProcessQueryEndSession (
	)
{
}

static void
Winpm_ProcessPowerBcast (
	Winpm_InterpData *statePtr,
	WPARAM wParam,
	LPARAM lParam
	)
{
	lParam = 0; // compiler shut up

	OutputDebugString("Winpm_ProcessPowerBcast\n");
	Tcl_GlobalEval(statePtr->interp,
			"winpm generate WM_POWERBROADCAST");

	switch (wParam) {
		case PBT_APMPOWERSTATUSCHANGE: {
			SYSTEM_POWER_STATUS ps;
			if (!GetSystemPowerStatus(&ps)) {
				/* TODO get error message */
				/* TODO process async error */
				Tcl_ResetResult(statePtr->interp);
			}
		}
		break;

		case PBT_APMRESUMEAUTOMATIC:
		break;

		case PBT_APMRESUMESUSPEND:
		break;

		case PBT_APMSUSPEND:
		break;

//		case PBT_POWERSETTINGCHANGE:
//		break;

		default:
		break;
	}
}

static LRESULT CALLBACK
WndProc(
	HWND hwnd,
	UINT uMsg,
	WPARAM wParam,
	LPARAM lParam
)
{
	OutputDebugString("WndProc\n");

	switch (uMsg) {
		case WM_QUERYENDSESSION:
			return TRUE;
		break;

		case WM_ENDSESSION:
			return 0;
		break;

		case WM_POWERBROADCAST:
			Winpm_ProcessPowerBcast(
					(Winpm_InterpData *)GetWindowLongPtr(hwnd, GWLP_USERDATA),
					wParam, lParam);
			return TRUE;
		break;

		default:
			return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
}

static int
CreateMonitorWindow (
	Tcl_Interp *interp,
	Winpm_InterpData *pdata
	)
{
	HINSTANCE  hinst;
	WNDCLASSEX wc;
	ATOM       rc;
	CHAR       title[] = "TkWinPMMonitorWindow";
	CHAR       name[]  = "TkWinPMMonitorWindowClass";
	HWND       hwnd;

	hinst = Tk_GetHINSTANCE();

	memset(&wc, 0, sizeof(wc));
    
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

	hwnd = CreateWindow( name, title, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, NULL, hinst, NULL );
	if (hwnd == NULL) {
		return TCL_ERROR;
	}

	SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG)pdata);

	ShowWindow(hwnd, SW_HIDE);
	UpdateWindow(hwnd);

	pdata->hwndMonitor = hwnd;

	return TCL_OK;
}

#ifdef BUILD_winpm
#undef TCL_STORAGE_CLASS
#define TCL_STORAGE_CLASS DLLEXPORT
#endif /* BUILD_winpm */

EXTERN int
Winpm_Init(Tcl_Interp * interp)
{
	Winpm_InterpData *pdata;

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

	pdata = (Winpm_InterpData *) ckalloc(sizeof(Winpm_InterpData));
	pdata->interp = interp;

	if (CreateMonitorWindow(interp, pdata) != TCL_OK) {
		return TCL_ERROR;
	}

	pdata->bindings = Tk_CreateBindingTable(interp);

	Tcl_CreateObjCommand(interp, "winpm", (Tcl_ObjCmdProc *) Winpm_Cmd,
		(ClientData) pdata, (Tcl_CmdDeleteProc *) Winpm_Cleanup);

	if (Tcl_PkgProvide(interp, PACKAGE_NAME, PACKAGE_VERSION) != TCL_OK) {
		return TCL_ERROR;
	}

	return TCL_OK;
}
