#ifndef WINDOWCAPTURE_H
#define WINDOWCAPTURE_H

#include <output_debug.h>

HBITMAP CaptureDesktop();
HBITMAP CaptureForegroundWindow(BOOL bClientAreaOnly = FALSE);
HBITMAP CaptureWindow(HWND hwnd, BOOL bClientAreaOnly = FALSE);



#endif  WINDOWCAPTURE_H