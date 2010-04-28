/****************************************************************************
* Utils for check_tcp
*
* License: GPL
* Copyright (c) 1999-2007 nagios-plugins team
*
* Last Modified: $Date: 2007-09-22 18:48:33 +0100 (Sat, 22 Sep 2007) $
*
* Description:
*
* This file contains utilities for check_tcp. These are tested by libtap
*
* License Information:
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
* $Id: utils_tcp.c 1787 2007-09-22 17:48:33Z psychotrahe $
*
*****************************************************************************/

#include "common.h"
#include "utils_tcp.h"

int
np_expect_match(char* status, char** server_expect, int expect_count, int all, int exact_match, int verbose)
{
	int match = 0;
	int i;
	for (i = 0; i < expect_count; i++) {
		if (verbose)
			printf ("looking for [%s] %s [%s]\n", server_expect[i],
					(exact_match) ? "in beginning of" : "anywhere in",
					status);

		if ((exact_match && !strncmp(status, server_expect[i], strlen(server_expect[i]))) ||
			(! exact_match && strstr(status, server_expect[i])))
		{
			if(verbose) puts("found it");
			match += 1;
		} else
			if(verbose) puts("couldn't find it");
	}
	if ((all == TRUE && match == expect_count) ||
		(! all && match >= 1)) {
		return TRUE;
	} else
		return FALSE;
}
