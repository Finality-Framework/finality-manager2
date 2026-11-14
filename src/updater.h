#pragma once

#include "app_types.h"

/* Checks the latest GitHub release in the background and shows an update
   reminder when a newer version is available. */
void updater_check(Manager *m);
