# Goals #

Create Tcl/Tk extension to provide some level of control over Windows power management support for Tk applications.

That is: to provide a way for Tcl/Tk applications to handle
  * WM\_POWERBROADCAST and its various classes of reported info (suspend/resume, etc);
  * WM\_QUERYENDSESSION and WM\_ENDSESSION.

# Status #

Production/ready.

## Implemented ##

  * Ability to bind to WM\_QUERYENDSESSION and WM\_ENDSESSION ("Query end session" request can be cancelled by the callback script if desired).
  * Ability to bind to WM\_POWERBROADCAST and the basic classes of its info (PBT\_APMPOWERSTATUSCHANGE, PBT\_APMRESUMEAUTOMATIC, PBT\_APMRESUMESUSPEND and PBT\_APMSUSPEND).
  * It's possible to inspect the system's power status and the messages being processed.