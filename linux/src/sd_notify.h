/* Minimal sd_notify(3): READY=1 / WATCHDOG=1 / STOPPING=1 over
 * $NOTIFY_SOCKET, without linking libsystemd. A no-op when not run under
 * systemd. Pair with WatchdogSec= in exhaustd.service so a hung main loop
 * gets restarted. */
#pragma once

void sd_notify_send(const char *state);
