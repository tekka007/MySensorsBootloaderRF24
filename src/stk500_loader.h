/*
 * Serial STK500v1 loader (avrdude -c arduino), derived from optiboot.
 */

#ifndef STK500_LOADER_H
#define STK500_LOADER_H

// Serves one programming session. After STK_LEAVE_PROGMODE it carries on with mys_loader_run(), so it never returns.
void stk500_loader_run(void) __attribute__((noreturn));

#endif // STK500_LOADER_H
