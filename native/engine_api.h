#pragma once
#include <windows.h>
extern "C" {
void* AF_Start(const unsigned*, unsigned, unsigned, unsigned, unsigned, HWND, ULONG_PTR);
int AF_Stop(void*);
int AF_Physical(void*, unsigned, unsigned);
unsigned AF_Error(void*);
int AF_PauseKey(void*, unsigned, unsigned, int);
int AF_Stats(void*, unsigned long long*);
}
