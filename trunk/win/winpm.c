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
	struct {
		ULONG uMsg;
		WPARAM wParam;
		LPARAM lParam;
	} last;
} Winpm_InterpData;

static const ClientData BindObject = (ClientData) 0xBAADF00D;

static const char *HandledEvents[] = {
	"WM_QUERYENDSESSION",
	"WM_ENDSESSION",
	"WM_POWERBROADCAST",
	"PBT_APMPOWERSTATUSCHANGE",
	"PBT_APMRESUMEAUTOMATIC",
	"PBT_APMRESUMESUSPEND",
	"PBT_APMSUSPEND",
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
Winpm_DispatchEvent (
	Winpm_InterpData *statePtr,
	Winpm_PercentMap *mapPtr,
	CONST char *event
	)
{
	CONST char *script;

	script = Tk_GetBinding(statePtr->interp, statePtr->bindings,
			BindObject, event);
	if (script == NULL) {
		return TCL_OK;
	} else {
		return Tcl_EvalEx(statePtr->interp, script, -1, TCL_EVAL_GLOBAL);
	}
}

static int
Winpm_NormalizeEventName (
	Tcl_Interp *interp,
	Tcl_Obj *objPtr,
	CONST char **namePtr
	)
{
	int i;

	if (Tcl_GetIndexFromObj(interp, objPtr,
			HandledEvents, "event", 0, &i) != TCL_OK) {
		return TCL_ERROR;
	} else {
		*namePtr = HandledEvents[i];
		return TCL_OK;
	}
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
			CONST char *event, *script;

			if (Winpm_NormalizeEventName(interp, objv[2],
						&event) != TCL_OK) return TCL_ERROR;

			script = Tk_GetBinding(interp, bindings,
					BindObject, event);
			if (script == NULL) {
				Tcl_ResetResult(interp);
			} else {
				Tcl_SetObjResult(interp,
						Tcl_NewStringObj(script, -1));
			}
			return TCL_OK;
		}
		break;

		case 4: { /* Create or delete binding */
			CONST char *event, *script;
			long len;

			if (Winpm_NormalizeEventName(interp, objv[2],
					&event) != TCL_OK) {
				return TCL_ERROR;
			}

			script = Tcl_GetStringFromObj(objv[3], &len);
			if (len == 0) {
				return Tk_DeleteBinding(interp, bindings,
						BindObject, event);
			} else {
				unsigned long mask;
				int append;

				append = script[0] == '+';
				if (append) ++script;

				mask = Tk_CreateBinding(interp, bindings,
						BindObject, event, script, append);
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
	CONST char *event, *script;

	if (objc != 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "event");
		return TCL_ERROR;
	}

	if (Winpm_NormalizeEventName(interp, objv[2],
				&event) != TCL_OK) return TCL_ERROR;

	script = Tk_GetBinding(interp, bindings,
			BindObject, event);
	if (script == NULL) {
		return TCL_OK;
	} else {
		int code = Tcl_EvalEx(interp, script, -1, TCL_EVAL_GLOBAL);
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
	static const char *topics[] = { "events", "lastmessage", NULL };
	typedef enum { INF_EVENTS, INF_LASTMESSAGE } INF_Option;
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

		case INF_LASTMESSAGE: {
			Tcl_Obj *elems[3];

			elems[0] = Tcl_NewLongObj(statePtr->last.uMsg);
			elems[1] = Tcl_NewLongObj(statePtr->last.wParam);
			elems[2] = Tcl_NewLongObj(statePtr->last.lParam);

			Tcl_SetObjResult(interp, Tcl_NewListObj(3, elems));
			return TCL_OK;
		}
		break;
	}

	return TCL_OK;
}

/* winpm _injectwm uMsg wParam lParam */
static int
Winpm_CmdInjectWM (
	Tcl_Interp *interp,
	Winpm_InterpData *statePtr,
	int objc,
	Tcl_Obj *const objv[]
	)
{
	UINT    uMsg;
	WPARAM  wParam;
	LPARAM  lParam;
	LRESULT res;

	if (objc != 5) {
		Tcl_WrongNumArgs(interp, 2, objv, "uMsg wParam lParam");
		return TCL_ERROR;
	}

	if (Tcl_GetLongFromObj(interp, objv[2], &uMsg) != TCL_OK
			|| Tcl_GetLongFromObj(interp, objv[3], &wParam) != TCL_OK
			|| Tcl_GetLongFromObj(interp, objv[4], &lParam) != TCL_OK) {
		return TCL_ERROR;
	}

	res = SendMessage(statePtr->hwndMonitor, uMsg, wParam, lParam);

	Tcl_SetObjResult(interp, Tcl_NewLongObj(res));
	return TCL_OK;
}

static int
Winpm_Cmd (
	ClientData clientData,
	Tcl_Interp *interp,
	int objc,
	Tcl_Obj *const objv[]
	)
{
	static const char *options[] = { "bind", "generate", "info",
		"_injectwm", NULL };
	typedef enum { WPM_BIND, WPM_GENERATE, WPM_INFO, WPM_INJECTWM } WPM_Option;
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

		case WPM_INJECTWM:
			return Winpm_CmdInjectWM(interp, statePtr, objc, objv);
		break;
	}

	return TCL_OK;
}

static void
Winpm_ProcessPowerBcast (
	Winpm_InterpData *statePtr,
	WPARAM wParam,
	LPARAM lParam
	)
{
	CONST char *class;

	lParam = 0; // compiler shut up

	switch (wParam) {
		case PBT_APMPOWERSTATUSCHANGE: {
			SYSTEM_POWER_STATUS ps;
			if (!GetSystemPowerStatus(&ps)) {
				/* TODO get error message */
				/* TODO process async error */
				Tcl_ResetResult(statePtr->interp);
			}
			class = "PBT_APMPOWERSTATUSCHANGE";
		}
		break;

		case PBT_APMRESUMEAUTOMATIC:
			class = "PBT_APMRESUMEAUTOMATIC";
		break;

		case PBT_APMRESUMESUSPEND:
			class = "PBT_APMRESUMESUSPEND";
		break;

		case PBT_APMSUSPEND:
			class = "PBT_APMSUSPEND";
		break;

//		case PBT_POWERSETTINGCHANGE:
//		break;

		default:
			class = NULL;
		break;
	}

	Winpm_DispatchEvent(statePtr, NULL, "WM_POWERBROADCAST");
	if (class != NULL) {
		Winpm_DispatchEvent(statePtr, NULL, class);
	}
}

static Winpm_InterpData*
GetWindowInterpData (
	HWND hwnd
	)
{
	return (Winpm_InterpData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
}

static void
SaveLastMessage (
	Winpm_InterpData *statePtr,
	UINT uMsg,
	WPARAM wParam,
	LPARAM lParam
	)
{
	statePtr->last.uMsg   = uMsg;
	statePtr->last.wParam = wParam;
	statePtr->last.lParam = lParam;
}

static LRESULT CALLBACK
WndProc(
	HWND hwnd,
	UINT uMsg,
	WPARAM wParam,
	LPARAM lParam
)
{
	Winpm_InterpData *statePtr;

	switch (uMsg) {
		case WM_QUERYENDSESSION:
			statePtr = GetWindowInterpData(hwnd);
			SaveLastMessage(statePtr, uMsg, wParam, lParam);
			return Winpm_DispatchEvent(statePtr,
					NULL, "WM_QUERYENDSESSION") !=  TCL_CONTINUE;
		break;

		case WM_ENDSESSION:
			statePtr = GetWindowInterpData(hwnd);
			SaveLastMessage(statePtr, uMsg, wParam, lParam);
			Winpm_DispatchEvent(statePtr, NULL, "WM_ENDSESSION");
			return 0;
		break;

		case WM_POWERBROADCAST:
			statePtr = GetWindowInterpData(hwnd);
			SaveLastMessage(statePtr, uMsg, wParam, lParam);
			Winpm_ProcessPowerBcast(statePtr, wParam, lParam);
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

static void
Winpm_Cleanup(ClientData clientData)
{
	Winpm_InterpData *pdata = (Winpm_InterpData *) clientData;

	Tk_DeleteBindingTable(pdata->bindings);
	DestroyWindow(pdata->hwndMonitor);

	ckfree((char *) pdata);
}

#ifdef BUILD_winpm
#undef TCL_STORAGE_CLASS
#define TCL_STORAGE_CLASS DLLEXPORT
#endif /* BUILD_winpm */

EXTERN int
Winpm_Init(Tcl_Interp * interp)
{
	Winpm_InterpData *statePtr;

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

	statePtr = (Winpm_InterpData *) ckalloc(sizeof(Winpm_InterpData));
	memset(statePtr, 0, sizeof(*statePtr));

	statePtr->interp = interp;

	if (CreateMonitorWindow(interp, statePtr) != TCL_OK) {
		return TCL_ERROR;
	}

	statePtr->bindings = Tk_CreateBindingTable(interp);

	Tcl_CreateObjCommand(interp, "winpm", (Tcl_ObjCmdProc *) Winpm_Cmd,
		(ClientData) statePtr, (Tcl_CmdDeleteProc *) Winpm_Cleanup);

	if (Tcl_PkgProvide(interp, PACKAGE_NAME, PACKAGE_VERSION) != TCL_OK) {
		return TCL_ERROR;
	}

	return TCL_OK;
}

