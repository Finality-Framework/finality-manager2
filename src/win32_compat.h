#pragma once

/* Prepares the process so that child processes spawned through GSubprocess
   keep their standard output connected to the pipes the application reads.

   On Windows, a GUI-subsystem process (such as this GTK application) has no
   console.  GLib then launches the console-less gspawn helper, and a console
   application started by that helper (notably java.exe) loses the inherited
   stdout/stderr pipes, so the log window stays empty.  Giving the process a
   console makes GLib use the console helper, which preserves the pipes. */
void win32_prepare_child_stdio(void);
