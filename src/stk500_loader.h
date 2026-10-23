/*
 * Serial STK500v1 loader (avrdude -c arduino), derived from optiboot.
 */

#ifndef STK500_LOADER_H
#define STK500_LOADER_H

// Serves one programming session, returns after STK_LEAVE_PROGMODE
void stk500_loader_run(void);

#endif // STK500_LOADER_H
