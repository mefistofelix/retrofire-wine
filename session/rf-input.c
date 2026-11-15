/* rf-input: reads input events from standard input and sends them to the X display with
   XTest. One event per line:  m X Y  (pointer to X,Y)   b BUTTON DOWN   k KEYSYM DOWN  */
#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

int main(void)
{
	char line[128];
	Display *d = XOpenDisplay(NULL);
	if (!d) { fprintf(stderr, "rf-input: no display\n"); return 1; }
	while (fgets(line, sizeof(line), stdin))
	{
		int a = 0, b = 0;
		if (sscanf(line + 1, "%d %d", &a, &b) < 2) continue;
		switch (line[0])
		{
			case 'm': XTestFakeMotionEvent(d, -1, a, b, CurrentTime); break;
			case 'b': XTestFakeButtonEvent(d, (unsigned)a, b ? True : False, CurrentTime); break;
			case 'k':
			{
				KeyCode code = XKeysymToKeycode(d, (KeySym)a);
				if (code) XTestFakeKeyEvent(d, code, b ? True : False, CurrentTime);
				break;
			}
		}
		XFlush(d);
	}
	return 0;
}
