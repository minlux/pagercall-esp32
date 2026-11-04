#pragma once
#include <WebServer.h>
#include <stdint.h>

void pagercall_begin();
void pagercall_notify(WebServer &server);
void pagercall_update(void);    // drive TX state machine; call from loop()
bool pagercall_idle(void);      // no call pending (FIFO empty, no transmission running)
