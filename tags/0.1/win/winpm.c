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
#include <tchar.h>
#include <pbt.h> /* MinGW's windows.h doesn't include it for some reason */

#ifndef ENDSESSION_CLOSEAPP
#define ENDSESSION_CLOSEAPP 0x00000001L
#endif

#ifndef PBT_APMRESUMEAUTOMATIC
#define PBT_APMRESUMEAUTOMATIC 0x0012
#endif

#include <tcl.h>
#include <tk.h>
#include <tkPlatDecls.h>

/* Mutex to serialize the process threads through the code
 * which tests the existence of/creates the monitoring window class */
TCL_DECLARE_MUTEX(global);

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
	"PBT_APMBATTERYLOW",
	"PBT_APMOEMEVENT",
	"PBT_APMQUERYSUSPEND",
	"PBT_APMQUERYSUSPENDFAILED",
	"PBT_APMRESUMECRITICAL",
	NULL
};

typedef struct {
	char token;
	char value[];
} Winpm_PercentMap;

#if 0
static void
Winpm_ExpandPercents (
	Tcl_Interp *interp,
	Winpm_PercentMap *mapPtr,
	CONST char *before,
	Tcl_DString *dsPtr
	)
{
	CONST char *string;

	while (1) {
		break;
	}
}
#endif

/* Code taken from win/tkWinTest.c of Tk
 *----------------------------------------------------------------------
 *
 * AppendSystemError --
 *
 *	This routine formats a Windows system error message and places
 *	it into the interpreter result.  Originally from tclWinReg.c.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
AppendSystemError(
	Tcl_Interp *interp, /* Current interpreter. */
	DWORD error)        /* Result code from error. */
{
	int length;
	WCHAR *wMsgPtr;
	char *msg;
	char id[TCL_INTEGER_SPACE], msgBuf[24 + TCL_INTEGER_SPACE];
	Tcl_DString ds;
	Tcl_Obj *resultPtr = Tcl_GetObjResult(interp);

	length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM
		| FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, error,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (WCHAR *) &wMsgPtr,
		0, NULL);
	if (length == 0) {
		char *msgPtr;

		length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM
			| FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (char *) &msgPtr,
			0, NULL);
		if (length > 0) {
			wMsgPtr = (WCHAR *) LocalAlloc(LPTR, (length + 1) * sizeof(WCHAR));
			MultiByteToWideChar(CP_ACP, 0, msgPtr, length + 1, wMsgPtr,
				length + 1);
			LocalFree(msgPtr);
		}
	}
	if (length == 0) {
		if (error == ERROR_CALL_NOT_IMPLEMENTED) {
			msg = "function not supported under Win32s";
		} else {
			sprintf(msgBuf, "unknown error: %ld", error);
			msg = msgBuf;
		}
	} else {
		Tcl_Encoding encoding;

		encoding = Tcl_GetEncoding(NULL, "unicode");
		msg = Tcl_ExternalToUtfDString(encoding, (char *) wMsgPtr, -1, &ds);
		Tcl_FreeEncoding(encoding);
		LocalFree(wMsgPtr);

		length = Tcl_DStringLength(&ds);

		/*
		 * Trim the trailing CR/LF from the system message.
		 */
		if (msg[length-1] == '\n') {
			msg[--length] = 0;
		}
		if (msg[length-1] == '\r') {
			msg[--length] = 0;
		}
	}

	sprintf(id, "%ld", error);
	Tcl_SetErrorCode(interp, "WINDOWS", id, msg, (char *) NULL);
	Tcl_AppendToObj(resultPtr, msg, length);

	if (length != 0) {
		Tcl_DStringFree(&ds);
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
		int code;
		Tcl_AllowExceptions(statePtr->interp);
		code =  Tcl_EvalEx(statePtr->interp, script, -1, TCL_EVAL_GLOBAL);
		if (code == TCL_ERROR) {
			Tcl_AddErrorInfo(statePtr->interp, "\n    (command bound to ");
			Tcl_AddErrorInfo(statePtr->interp, event);
			Tcl_AddErrorInfo(statePtr->interp, " " PACKAGE_NAME " event)");
			Tcl_BackgroundError(statePtr->interp);
		}
		return code;
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
			int len;

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

/* winpm info arg ?arg ...? */
static int
Winpm_CmdInfo (
	Tcl_Interp *interp,
	Winpm_InterpData *statePtr,
	int objc,
	Tcl_Obj *const objv[]
	)
{
	static const char *topics[] = { "events", "lastmessage",
		"session", "power", "id", NULL };
	typedef enum { INF_EVENTS, INF_LASTMESSAGE,
		INF_SESSION, INF_POWER, INF_ID } INF_Option;
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

		case INF_SESSION: {
			typedef struct {
				LPARAM flag;
				CONST char *name;
			} SessFlagMap;
			static CONST SessFlagMap fmap[] = {
				{ ENDSESSION_CLOSEAPP, "ENDSESSION_CLOSEAPP" },
				{ ENDSESSION_LOGOFF,   "ENDSESSION_LOGOFF" }
			};
			int i, final;
			Tcl_Obj *elems[2];
			Tcl_Obj *flagsObj;

			if (statePtr->last.uMsg != WM_QUERYENDSESSION
					&& statePtr->last.uMsg != WM_ENDSESSION) {
				Tcl_SetResult(interp, "Information unavailable", TCL_STATIC);
				return TCL_ERROR;
			}

			if (statePtr->last.uMsg == WM_ENDSESSION) {
				final = statePtr->last.wParam;
			} else {
				final = 0;
			}
			elems[0] = Tcl_NewBooleanObj(final);

			flagsObj = Tcl_NewListObj(0, NULL);
			for (i = 0; i < sizeof(fmap)/sizeof(fmap[0]); ++i) {
				if (statePtr->last.lParam & fmap[i].flag) {
					if (Tcl_ListObjAppendElement(interp, flagsObj,
							Tcl_NewStringObj(fmap[i].name,
								-1)) != TCL_OK) {
						return TCL_ERROR;
					}
				}
			}
			elems[1] = flagsObj;

			Tcl_SetObjResult(interp, Tcl_NewListObj(2, elems));
			return TCL_OK;
		}
		break;

		case INF_POWER: {
			SYSTEM_POWER_STATUS power;
			CONST char *string;
			int val;
			Tcl_Obj *elems[5];

			if (!GetSystemPowerStatus(&power)) {
				Tcl_ResetResult(statePtr->interp);
				AppendSystemError(statePtr->interp, GetLastError());
				return TCL_ERROR;
			}

			switch (power.ACLineStatus) {
				case 0:
					string = "OFFLINE";
					break;
				case 1:
					string = "ONLINE";
					break;
				default:
					string = "UNKNOWN";
					break;
			}
			elems[0] = Tcl_NewStringObj(string, -1);

			switch (power.BatteryFlag) {
				case 1:
					string = "HIGH";
					break;
				case 2:
					string = "LOW";
					break;
				case 4:
					string = "CRITICAL";
					break;
				case 8:
					string = "CHARGING";
					break;
				case 128:
					string = "NONE";
					break;
				default:
					string = "UNKNOWN";
					break;
			}
			elems[1] = Tcl_NewStringObj(string, -1);

			if (power.BatteryLifePercent == 255) {
				val = -1;
			} else {
				val = power.BatteryLifePercent;
			}
			elems[2] = Tcl_NewIntObj(val);

			elems[3] = Tcl_NewIntObj(power.BatteryLifeTime);
			elems[4] = Tcl_NewIntObj(power.BatteryFullLifeTime);

			Tcl_SetObjResult(interp, Tcl_NewListObj(5, elems));
			return TCL_OK;
		}
		break;

		case INF_ID: {
			char buf[sizeof("0xFFFFFFFF")];
			sprintf(buf, "0x%08X", (unsigned) statePtr->hwndMonitor);
			Tcl_SetObjResult(interp, Tcl_NewStringObj(buf, -1));
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

	if (Tcl_GetIntFromObj(interp, objv[2], &uMsg) != TCL_OK
			|| Tcl_GetIntFromObj(interp, objv[3], &wParam) != TCL_OK
			|| Tcl_GetLongFromObj(interp, objv[4], &lParam) != TCL_OK) {
		return TCL_ERROR;
	}

	/* FIXME is it possible for SendMessage to return error? */
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
	static const char *options[] = { "bind", "info", "_injectwm", NULL };
	typedef enum { WPM_BIND, WPM_INFO, WPM_INJECTWM } WPM_Option;
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

		case WPM_INFO:
			return Winpm_CmdInfo(interp, statePtr, objc, objv);
		break;

		case WPM_INJECTWM:
			return Winpm_CmdInjectWM(interp, statePtr, objc, objv);
		break;
	}

	return TCL_OK;
}

static LRESULT
Winpm_ProcessPowerBcast (
	Winpm_InterpData *statePtr,
	WPARAM wParam,
	LPARAM lParam
	)
{
	CONST char *class;

	lParam = 0; // compiler shut up

	Winpm_DispatchEvent(statePtr, NULL, "WM_POWERBROADCAST");

	switch (wParam) {
		case PBT_APMPOWERSTATUSCHANGE: {
			/* TODO when %-expanding will be implemented
			 * here we should call GetSystemPowerStatus()
			 * and parse what it returns */
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

		/* Events listed below were removed from Vista */

		/* PBT_APMPOWERSTATUSCHANGE should be used instead */
		case PBT_APMBATTERYLOW:
			class = "PBT_APMBATTERYLOW";
		break;

		case PBT_APMOEMEVENT:
			/* lParam holds OEM event code */
			class = "PBT_APMOEMEVENT";
		break;

		case PBT_APMQUERYSUSPEND:
			/* Special handling: callback script can prevent suspending */
			if (Winpm_DispatchEvent(statePtr, NULL,
						"PBT_APMQUERYSUSPEND") == TCL_CONTINUE) {
				return BROADCAST_QUERY_DENY;
			} else {
				return TRUE;
			}
		break;

		case PBT_APMQUERYSUSPENDFAILED:
			class = "PBT_APMQUERYSUSPENDFAILED";
		break;

		/* PBT_APMRESUMEAUTOMATIC should be used in Vista */
		case PBT_APMRESUMECRITICAL:
			class = "PBT_APMRESUMECRITICAL";
		break;

		default:
			class = NULL;
		break;
	}

	if (class != NULL) {
		Winpm_DispatchEvent(statePtr, NULL, class);
	}

	return TRUE;
}

static Winpm_InterpData*
GetWindowInterpData (
	HWND hwnd)
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
					NULL, "WM_QUERYENDSESSION") != TCL_CONTINUE;
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
			return Winpm_ProcessPowerBcast(statePtr, wParam, lParam);
		break;

		default:
			return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
}

static int
CreateMonitorWindow (
	Tcl_Interp *interp,
	Winpm_InterpData *statePtr)
{
	CONST TCHAR name[]  = _T("TkWinPMMonitorWindow");
	CONST TCHAR title[] = _T("TkWinPMMonitorWindow");

	HINSTANCE   hinst;
	WNDCLASSEX  wc;
	ATOM        rc;
	HWND        hwnd;

	hinst = Tk_GetHINSTANCE();

	memset(&wc, 0, sizeof(wc));
	wc.cbSize = sizeof(wc);

	Tcl_MutexLock(&global);
	if (!GetClassInfoEx(hinst, name, &wc)) {
		if (GetLastError() != ERROR_CLASS_DOES_NOT_EXIST) {
			Tcl_MutexUnlock(&global);
			Tcl_ResetResult(interp);
			AppendSystemError(interp, GetLastError());
			return TCL_ERROR;
		}
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

		rc = RegisterClassEx(&wc);
		if (rc == 0) {
			Tcl_MutexUnlock(&global);
			Tcl_ResetResult(interp);
			AppendSystemError(interp, GetLastError());
			return TCL_ERROR;
		}
	}
	Tcl_MutexUnlock(&global);

	if (wc.lpfnWndProc != (WNDPROC) WndProc) {
		Tcl_SetResult(interp,
				"Monitoring window class already registered\
				by a foreign code", TCL_STATIC);
		return TCL_ERROR;
	}

	hwnd = CreateWindow(name, title, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, NULL, hinst, NULL);
	if (hwnd == NULL) {
		Tcl_ResetResult(interp);
		AppendSystemError(interp, GetLastError());
		return TCL_ERROR;
	}

	SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG)statePtr);

	ShowWindow(hwnd, SW_HIDE);
	UpdateWindow(hwnd);

	statePtr->hwndMonitor = hwnd;

	return TCL_OK;
}

static void
Winpm_Cleanup(ClientData clientData)
{
	Winpm_InterpData *statePtr = (Winpm_InterpData *) clientData;

	Tk_DeleteBindingTable(statePtr->bindings);
	DestroyWindow(statePtr->hwndMonitor);

	ckfree((char *) statePtr);
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

