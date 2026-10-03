#include "win32_compat.h"

#include <glib.h>

#ifdef G_OS_WIN32
#include <windows.h>

void win32_prepare_child_stdio(void) {
  /* Only allocate a console when we do not already have one (for example when
     the application was started from a terminal).  The console is hidden right
     away so no extra window is shown. */
  if (GetConsoleWindow() != NULL)
    return;
  if (!AllocConsole())
    return;
  ShowWindow(GetConsoleWindow(), SW_HIDE);
}
#else
void win32_prepare_child_stdio(void) {}
#endif
