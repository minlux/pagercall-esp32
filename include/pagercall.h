#pragma once
#include <WebServer.h>
#include <stdint.h>

void pagercall_begin();
void pagercall_notify(WebServer &server);

// FIFO for pending transmissions; packed = (keyboard10 << 10) | pager10
bool pagercall_push(uint32_t packed);   // returns false if full
bool pagercall_pop(uint32_t *packed);   // returns false if empty

// Encode a call into a 13-byte Serial1 sequence; returns byte count
uint32_t pagercall_encode_6n1(uint8_t out[13], uint32_t keyboard10, uint32_t pager10, uint32_t action5);

void pagercall_cw_start(void);  // enable CW carrier
void pagercall_cw_stop(void);   // return to standby (carrier off)
