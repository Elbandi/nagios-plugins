/******************************************************************************
*
* Nagios check_users plugin
*
* License: GPL
* Copyright (c) 2000-2006 nagios-plugins team
*
* Last Modified: $Date: 2007-01-28 21:46:41 +0000 (Sun, 28 Jan 2007) $
*
* Description:
*
* This file contains the check_users plugin
*
*  This plugin checks the number of users currently logged in on the local
*  system and generates an error if the number exceeds the thresholds specified.
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
*  $Id: check_users.c 1590 2007-01-28 21:46:41Z hweiss $
*
*****************************************************************************/

const char *progname = "check_users";
const char *revision = "$Revision: 1590 $";
const char *copyright = "2000-2006";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "popen.h"
#include "utils.h"

#define possibly_set(a,b) ((a) == 0 ? (b) : 0)

int process_arguments (int, char **);
void print_help (void);
void print_usage (void);

int wusers = -1;
int cusers = -1;

int
main (int argc, char **argv)
{
	int users = -1;
	int result = STATE_UNKNOWN;
	char input_buffer[MAX_INPUT_BUFFER];
	char *perf;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	perf = strdup("");

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* run the command */
	child_process = spopen (WHO_COMMAND);
	if (child_process == NULL) {
		printf (_("Could not open pipe: %s\n"), WHO_COMMAND);
		return STATE_UNKNOWN;
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL)
		printf (_("Could not open stderr for %s\n"), WHO_COMMAND);

	users = 0;

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {

		/* increment 'users' on all lines except total user count */
		if (input_buffer[0] != '#') {
			users++;
			continue;
		}

		/* get total logged in users */
		if (sscanf (input_buffer, _("# users=%d"), &users) == 1)
			break;

	}

	/* check STDERR */
	if (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr))
		result = possibly_set (result, STATE_UNKNOWN);
	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process))
		result = possibly_set (result, STATE_UNKNOWN);

	/* else check the user count against warning and critical thresholds */
	if (users >= cusers)
		result = STATE_CRITICAL;
	else if (users >= wusers)
		result = STATE_WARNING;
	else if (users >= 0)
		result = STATE_OK;

	if (result == STATE_UNKNOWN)
		printf ("%s\n", _("Unable to read output"));
	else {
		asprintf(&perf, "%s", perfdata ("users", users, "",
		  TRUE, wusers,
		  TRUE, cusers,
		  TRUE, 0,
		  FALSE, 0));
		printf (_("USERS %s - %d users currently logged in |%s\n"), state_text (result),
		  users, perf);
	}

	return result;
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int option = 0;
	static struct option longopts[] = {
		{"critical", required_argument, 0, 'c'},
		{"warning", required_argument, 0, 'w'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		usage ("\n");

	while (1) {
		c = getopt_long (argc, argv, "+hVvc:w:", longopts, &option);

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case '?':									/* print short usage statement if args not parsable */
			usage5 ();
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 'c':									/* critical */
			if (!is_intnonneg (optarg))
				usage4 (_("Critical threshold must be a positive integer"));
			else
				cusers = atoi (optarg);
			break;
		case 'w':									/* warning */
			if (!is_intnonneg (optarg))
				usage4 (_("Warning threshold must be a positive integer"));
			else
				wusers = atoi (optarg);
			break;
		}
	}

	c = optind;
	if (wusers == -1 && argc > c) {
		if (is_intnonneg (argv[c]) == FALSE)
			usage4 (_("Warning threshold must be a positive integer"));
		else
			wusers = atoi (argv[c++]);
	}

	if (cusers == -1 && argc > c) {
		if (is_intnonneg (argv[c]) == FALSE)
			usage4 (_("Warning threshold must be a positive integer"));
		else
			cusers = atoi (argv[c]);
	}

	return OK;
}



void
print_help (void)
{
	print_revision (progname, revision);

	printf ("Copyright (c) 1999 Ethan Galstad\n");
	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("This plugin checks the number of users currently logged in on the local"));
  printf ("%s\n", _("system and generates an error if the number exceeds the thresholds specified."));

  printf ("\n\n");

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (" %s\n", "-w, --warning=INTEGER");
  printf ("    %s\n", _("Set WARNING status if more than INTEGER users are logged in"));
  printf (" %s\n", "-c, --critical=INTEGER");
  printf ("    %s\n", _("Set CRITICAL status if more than INTEGER users are logged in"));

	printf (_(UT_SUPPORT));
}


void
print_usage (void)
{
  printf (_("Usage:"));
	printf ("%s -w <users> -c <users>\n", progname);
}
