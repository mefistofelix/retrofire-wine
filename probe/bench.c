/* Small CPU benchmark for the probe: a Win32 console program (x86), run under Wine + Box86.
   Prints the time of three loops whose work is known, so the speed can be compared with
   the cycles DOSBox Pure reaches on the same device. */
#include <windows.h>
#include <stdio.h>

static unsigned int lcg(unsigned int n)
{
	unsigned int x = 12345, sum = 0, i;
	for (i = 0; i != n; i++) { x = x * 1664525u + 1013904223u; sum += x >> 16; }
	return sum;
}

static unsigned int sieve(int rounds)
{
	static unsigned char flags[65536];
	unsigned int count = 0;
	int r, i, k;
	for (r = 0; r != rounds; r++)
	{
		count = 0;
		for (i = 0; i != 65536; i++) flags[i] = 1;
		for (i = 2; i != 65536; i++)
			if (flags[i]) { count++; for (k = i + i; k < 65536; k += i) flags[k] = 0; }
	}
	return count;
}

static double mandel(int size)
{
	double total = 0;
	int px, py, n;
	for (py = 0; py != size; py++)
		for (px = 0; px != size; px++)
		{
			double cr = -2.0 + 3.0 * px / size, ci = -1.5 + 3.0 * py / size, zr = 0, zi = 0;
			for (n = 0; n != 200 && zr * zr + zi * zi < 4.0; n++) { double t = zr * zr - zi * zi + cr; zi = 2 * zr * zi + ci; zr = t; }
			total += n;
		}
	return total;
}

static double now_ms(void)
{
	LARGE_INTEGER f, c;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&c);
	return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

int main(void)
{
	double t0 = now_ms(), t1, t2, t3;
	unsigned int a = 0, b, r;
	double c;
	for (r = 0; r != 10; r++) a += lcg(200000000u);   /* 2000M iterations, a few instructions each */
	t1 = now_ms();
	b = sieve(3000);
	t2 = now_ms();
	c = mandel(1600);
	t3 = now_ms();
	printf("lcg 2000M iterations: %.0f ms (check %u)
", t1 - t0, a);
	printf("sieve 64K x3000:      %.0f ms (primes %u)
", t2 - t1, b);
	printf("mandelbrot 1600x1600: %.0f ms (sum %.0f)
", t3 - t2, c);
	return 0;
}
