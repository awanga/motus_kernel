/* console.c: Routines that deal with sending and receiving IO
 *            to/from the current console device using the PROM.
 *
 * Copyright (C) 1995 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <linux/string.h>

extern int prom_stdin, prom_stdout;

static int __prom_console_write_buf(const char *buf, int len)
{
	unsigned long args[7];
	char inc;

	args[0] = (unsigned long) "read";
	args[1] = 3;
	args[2] = 1;
	args[3] = (unsigned int) prom_stdin;
	args[4] = (unsigned long) &inc;
	args[5] = 1;
	args[6] = (unsigned long) -1;

	p1275_cmd_direct(args);

	if (args[6] == 1)
		return inc;
	return -1;
}

/* Non blocking put character to console device, returns -1 if
 * unsuccessful.
 */
inline int
prom_nbputchar(char c)
{
	unsigned long args[7];
	char outc;
	
	outc = c;

	args[0] = (unsigned long) "write";
	args[1] = 3;
	args[2] = 1;
	args[3] = (unsigned int) prom_stdout;
	args[4] = (unsigned long) &outc;
	args[5] = 1;
	args[6] = (unsigned long) -1;

	p1275_cmd_direct(args);

	if (args[6] == 1)
		return 0;
	else
		return -1;
}

	p1275_cmd_direct(args);

	ret = (int) args[6];
	if (ret < 0)
		return -1;
	return ret;
}

void prom_console_write_buf(const char *buf, int len)
{
	unsigned long args[7];

	args[0] = (unsigned long) "write";
	args[1] = 3;
	args[2] = 1;
	args[3] = (unsigned int) prom_stdout;
	args[4] = (unsigned long) s;
	args[5] = len;
	args[6] = (unsigned long) -1;

	p1275_cmd_direct(args);
}
