#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
static double qpc(void) { LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return (double)c.QuadPart * 1000.0 / (double)f.QuadPart; }
static double ft(void) { FILETIME t; GetSystemTimeAsFileTime(&t); return (double)(((unsigned long long)t.dwHighDateTime << 32) | t.dwLowDateTime) / 10000.0; }
static void show(const char *what, DWORD t0, DWORD g0, double q0, double f0)
{
	printf("%s: timeGetTime %lu  GetTickCount %lu  QPC %.0f  system %.0f ms\n", what, timeGetTime() - t0, GetTickCount() - g0, qpc() - q0, ft() - f0);
	fflush(stdout);
}
int main(void)
{
	DWORD t0, g0; double q0, f0; int i;
	LARGE_INTEGER f; QueryPerformanceFrequency(&f);
	printf("QPC frequency %lld\n", f.QuadPart);
	t0 = timeGetTime(); g0 = GetTickCount(); q0 = qpc(); f0 = ft();
	Sleep(2000); show("Sleep(2000)", t0, g0, q0, f0);
	t0 = timeGetTime(); g0 = GetTickCount(); q0 = qpc(); f0 = ft();
	for (i = 0; i != 100; i++) Sleep(8);
	show("100 x Sleep(8)", t0, g0, q0, f0);
	t0 = timeGetTime(); g0 = GetTickCount(); q0 = qpc(); f0 = ft();
	for (i = 0; i != 200; i++) Sleep(1);
	show("200 x Sleep(1)", t0, g0, q0, f0);
	t0 = timeGetTime(); g0 = GetTickCount(); q0 = qpc(); f0 = ft();
	for (i = 0; i != 2000; i++) { MSG m; PeekMessageA(&m, NULL, 0, 0, PM_REMOVE); }
	show("2000 x PeekMessage", t0, g0, q0, f0);
	t0 = timeGetTime(); g0 = GetTickCount(); q0 = qpc(); f0 = ft();
	for (i = 0; i != 200000; i++) timeGetTime();
	show("200000 x timeGetTime", t0, g0, q0, f0);
	return 0;
}
