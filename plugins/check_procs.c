/******************************************************************************
*
* Nagios check_procs plugin
*
* License: GPL
* Copyright (c) 1999-2006 nagios-plugins team
*
* Last Modified: $Date: 2007-07-15 16:21:51 +0100 (Sun, 15 Jul 2007) $
*
* Description:
*
* This file contains the check_procs plugin
*
*  Checks all processes and generates WARNING or CRITICAL states if the specified
*  metric is outside the required threshold ranges. The metric defaults to number
*  of processes.  Search filters can be applied to limit the processes to check.
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
* $Id: check_procs.c 1758 2007-07-15 15:21:51Z psychotrahe $
* 
******************************************************************************/

const char *progname = "check_procs";
const char *program_name = "check_procs";  /* Required for coreutils libs */
const char *revision = "$Revision: 1758 $";
const char *copyright = "2000-2006";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "popen.h"
#include "utils.h"

#include <pwd.h>

int process_arguments (int, char **);
int validate_arguments (void);
int check_thresholds (int);
int convert_to_seconds (char *); 
void print_help (void);
void print_usage (void);

int wmax = -1;
int cmax = -1;
int wmin = -1;
int cmin = -1;

int options = 0; /* bitmask of filter criteria to test against */
#define ALL 1
#define STAT 2
#define PPID 4
#define USER 8
#define PROG 16
#define ARGS 32
#define VSZ  64
#define RSS  128
#define PCPU 256
#define ELAPSED 512
/* Different metrics */
char *metric_name;
enum metric {
	METRIC_PROCS,
	METRIC_VSZ,
	METRIC_RSS,
	METRIC_CPU,
	METRIC_ELAPSED
};
enum metric metric = METRIC_PROCS;

int verbose = 0;
int uid;
pid_t ppid;
int vsz;
int rss;
float pcpu;
char *statopts;
char *prog;
char *args;
char *fmt;
char *fails;
char tmp[MAX_INPUT_BUFFER];



int
main (int argc, char **argv)
{
	char *input_buffer;
	char *input_line;
	char *procprog;

	pid_t mypid = 0;
	int procuid = 0;
	pid_t procpid = 0;
	pid_t procppid = 0;
	int procvsz = 0;
	int procrss = 0;
	int procseconds = 0;
	float procpcpu = 0;
	char procstat[8];
	char procetime[MAX_INPUT_BUFFER] = { '\0' };
	char *procargs;

	const char *zombie = "Z";

	int resultsum = 0; /* bitmask of the filter criteria met by a process */
	int found = 0; /* counter for number of lines returned in `ps` output */
	int procs = 0; /* counter for number of processes meeting filter criteria */
	int pos; /* number of spaces before 'args' in `ps` output */
	int cols; /* number of columns in ps output */
	int expected_cols = PS_COLS - 1;
	int warn = 0; /* number of processes in warn state */
	int crit = 0; /* number of processes in crit state */
	int i = 0;
	int result = STATE_UNKNOWN;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);
	setlocale(LC_NUMERIC, "POSIX");

	input_buffer = malloc (MAX_INPUT_BUFFER);
	procprog = malloc (MAX_INPUT_BUFFER);

	asprintf (&metric_name, "PROCS");
	metric = METRIC_PROCS;

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* get our pid */
	mypid = getpid();

	/* Set signal handling and alarm timeout */
	if (signal (SIGALRM, popen_timeout_alarm_handler) == SIG_ERR) {
		usage4 (_("Cannot catch SIGALRM"));
	}
	alarm (timeout_interval);

	if (verbose >= 2)
		printf (_("CMD: %s\n"), PS_COMMAND);

	child_process = spopen (PS_COMMAND);
	if (child_process == NULL) {
		printf (_("Could not open pipe: %s\n"), PS_COMMAND);
		return STATE_UNKNOWN;
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL)
		printf (_("Could not open stderr for %s\n"), PS_COMMAND);

	/* flush first line */
	fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process);
	while ( input_buffer[strlen(input_buffer)-1] != '\n' )
		fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process);

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {
		asprintf (&input_line, "%s", input_buffer);
		while ( input_buffer[strlen(input_buffer)-1] != '\n' ) {
			fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process);
			asprintf (&input_line, "%s%s", input_line, input_buffer);
		}

		if (verbose >= 3)
			printf ("%s", input_line);

		strcpy (procprog, "");
		asprintf (&procargs, "%s", "");

		cols = sscanf (input_line, PS_FORMAT, PS_VARLIST);

		/* Zombie processes do not give a procprog command */
		if ( cols < expected_cols && strstr(procstat, zombie) ) {
			cols = expected_cols;
		}
		if ( cols >= expected_cols ) {
			resultsum = 0;
			asprintf (&procargs, "%s", input_line + pos);
			strip (procargs);

			/* Some ps return full pathname for command. This removes path */
			strcpy(procprog, base_name(procprog));

			/* we need to convert the elapsed time to seconds */
			procseconds = convert_to_seconds(procetime);

			if (verbose >= 3)
				printf ("proc#=%d uid=%d vsz=%d rss=%d pid=%d ppid=%d pcpu=%.2f stat=%s etime=%s prog=%s args=%s\n", 
					procs, procuid, procvsz, procrss,
					procpid, procppid, procpcpu, procstat, 
					procetime, procprog, procargs);

			/* Ignore self */
			if (mypid == procpid) continue;

			if ((options & STAT) && (strstr (statopts, procstat)))
				resultsum |= STAT;
			if ((options & ARGS) && procargs && (strstr (procargs, args) != NULL))
				resultsum |= ARGS;
			if ((options & PROG) && procprog && (strcmp (prog, procprog) == 0))
				resultsum |= PROG;
			if ((options & PPID) && (procppid == ppid))
				resultsum |= PPID;
			if ((options & USER) && (procuid == uid))
				resultsum |= USER;
			if ((options & VSZ)  && (procvsz >= vsz))
				resultsum |= VSZ;
			if ((options & RSS)  && (procrss >= rss))
				resultsum |= RSS;
			if ((options & PCPU)  && (procpcpu >= pcpu))
				resultsum |= PCPU;

			found++;

			/* Next line if filters not matched */
			if (!(options == resultsum || options == ALL))
				continue;

			procs++;

			if (metric == METRIC_VSZ)
				i = check_thresholds (procvsz);
			else if (metric == METRIC_RSS)
				i = check_thresholds (procrss);
			/* TODO? float thresholds for --metric=CPU */
			else if (metric == METRIC_CPU)
				i = check_thresholds ((int)procpcpu); 
			else if (metric == METRIC_ELAPSED)
				i = check_thresholds (procseconds);

			if (metric != METRIC_PROCS) {
				if (i == STATE_WARNING) {
					warn++;
					asprintf (&fails, "%s%s%s", fails, (strcmp(fails,"") ? ", " : ""), procprog);
					result = max_state (result, i);
				}
				if (i == STATE_CRITICAL) {
					crit++;
					asprintf (&fails, "%s%s%s", fails, (strcmp(fails,"") ? ", " : ""), procprog);
					result = max_state (result, i);
				}
			}
		} 
		/* This should not happen */
		else if (verbose) {
			printf(_("Not parseable: %s"), input_buffer);
		}
	}

	/* If we get anything on STDERR, at least set warning */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr)) {
		if (verbose)
			printf ("STDERR: %s", input_buffer);
		result = max_state (result, STATE_WARNING);
		printf (_("System call sent warnings to stderr\n"));
	}
	
	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process)) {
		printf (_("System call returned nonzero status\n"));
		result = max_state (result, STATE_WARNING);
	}

	if (found == 0) {							/* no process lines parsed so return STATE_UNKNOWN */
		printf (_("Unable to read output\n"));
		return result;
	}

	if ( result == STATE_UNKNOWN ) 
		result = STATE_OK;

	/* Needed if procs found, but none match filter */
	if ( metric == METRIC_PROCS ) {
		result = max_state (result, check_thresholds (procs) );
	}

	if ( result == STATE_OK ) {
		printf ("%s %s: ", metric_name, _("OK"));
	} else if (result == STATE_WARNING) {
		printf ("%s %s: ", metric_name, _("WARNING"));
		if ( metric != METRIC_PROCS ) {
			printf (_("%d warn out of "), warn);
		}
	} else if (result == STATE_CRITICAL) {
		printf ("%s %s: ", metric_name, _("CRITICAL"));
		if (metric != METRIC_PROCS) {
			printf (_("%d crit, %d warn out of "), crit, warn);
		}
	} 
	printf (ngettext ("%d process", "%d processes", (unsigned long) procs), procs);
	
	if (strcmp(fmt,"") != 0) {
		printf (_(" with %s"), fmt);
	}

	if ( verbose >= 1 && strcmp(fails,"") )
		printf (" [%s]", fails);

	printf ("\n");
	return result;
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c = 1;
	char *user;
	struct passwd *pw;
	int option = 0;
	static struct option longopts[] = {
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"metric", required_argument, 0, 'm'},
		{"timeout", required_argument, 0, 't'},
		{"status", required_argument, 0, 's'},
		{"ppid", required_argument, 0, 'p'},
		{"command", required_argument, 0, 'C'},
		{"vsz", required_argument, 0, 'z'},
		{"rss", required_argument, 0, 'r'},
		{"pcpu", required_argument, 0, 'P'},
		{"elapsed", required_argument, 0, 'e'},
		{"argument-array", required_argument, 0, 'a'},
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"verbose", no_argument, 0, 'v'},
		{0, 0, 0, 0}
	};

	for (c = 1; c < argc; c++)
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");

	while (1) {
		c = getopt_long (argc, argv, "Vvht:c:w:p:s:u:C:a:z:r:m:P:", 
			longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':									/* help */
			usage5 ();
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 't':									/* timeout period */
			if (!is_integer (optarg))
				usage2 (_("Timeout interval must be a positive integer"), optarg);
			else
				timeout_interval = atoi (optarg);
			break;
		case 'c':									/* critical threshold */
			if (is_integer (optarg))
				cmax = atoi (optarg);
			else if (sscanf (optarg, ":%d", &cmax) == 1)
				break;
			else if (sscanf (optarg, "%d:%d", &cmin, &cmax) == 2)
				break;
			else if (sscanf (optarg, "%d:", &cmin) == 1)
				break;
			else
				usage4 (_("Critical Process Count must be an integer!"));
			break;							 
		case 'w':									/* warning threshold */
			if (is_integer (optarg))
				wmax = atoi (optarg);
			else if (sscanf (optarg, ":%d", &wmax) == 1)
				break;
			else if (sscanf (optarg, "%d:%d", &wmin, &wmax) == 2)
				break;
			else if (sscanf (optarg, "%d:", &wmin) == 1)
				break;
			else
				usage4 (_("Warning Process Count must be an integer!"));
			break;
		case 'p':									/* process id */
			if (sscanf (optarg, "%d%[^0-9]", &ppid, tmp) == 1) {
				asprintf (&fmt, "%s%sPPID = %d", (fmt ? fmt : "") , (options ? ", " : ""), ppid);
				options |= PPID;
				break;
			}
			usage4 (_("Parent Process ID must be an integer!"));
		case 's':									/* status */
			if (statopts)
				break;
			else
				statopts = optarg;
			asprintf (&fmt, _("%s%sSTATE = %s"), (fmt ? fmt : ""), (options ? ", " : ""), statopts);
			options |= STAT;
			break;
		case 'u':									/* user or user id */
			if (is_integer (optarg)) {
				uid = atoi (optarg);
				pw = getpwuid ((uid_t) uid);
				/*  check to be sure user exists */
				if (pw == NULL)
					usage2 (_("UID %s was not found"), optarg);
			}
			else {
				pw = getpwnam (optarg);
				/*  check to be sure user exists */
				if (pw == NULL)
					usage2 (_("User name %s was not found"), optarg);
				/*  then get uid */
				uid = pw->pw_uid;
			}
			user = pw->pw_name;
			asprintf (&fmt, "%s%sUID = %d (%s)", (fmt ? fmt : ""), (options ? ", " : ""),
			          uid, user);
			options |= USER;
			break;
		case 'C':									/* command */
			/* TODO: allow this to be passed in with --metric */
			if (prog)
				break;
			else
				prog = optarg;
			asprintf (&fmt, _("%s%scommand name '%s'"), (fmt ? fmt : ""), (options ? ", " : ""),
			          prog);
			options |= PROG;
			break;
		case 'a':									/* args (full path name with args) */
			/* TODO: allow this to be passed in with --metric */
			if (args)
				break;
			else
				args = optarg;
			asprintf (&fmt, "%s%sargs '%s'", (fmt ? fmt : ""), (options ? ", " : ""), args);
			options |= ARGS;
			break;
		case 'r': 					/* RSS */
			if (sscanf (optarg, "%d%[^0-9]", &rss, tmp) == 1) {
				asprintf (&fmt, "%s%sRSS >= %d", (fmt ? fmt : ""), (options ? ", " : ""), rss);
				options |= RSS;
				break;
			}
			usage4 (_("RSS must be an integer!"));
		case 'z':					/* VSZ */
			if (sscanf (optarg, "%d%[^0-9]", &vsz, tmp) == 1) {
				asprintf (&fmt, "%s%sVSZ >= %d", (fmt ? fmt : ""), (options ? ", " : ""), vsz);
				options |= VSZ;
				break;
			}
			usage4 (_("VSZ must be an integer!"));
		case 'P':					/* PCPU */
			/* TODO: -P 1.5.5 is accepted */
			if (sscanf (optarg, "%f%[^0-9.]", &pcpu, tmp) == 1) {
				asprintf (&fmt, "%s%sPCPU >= %.2f", (fmt ? fmt : ""), (options ? ", " : ""), pcpu);
				options |= PCPU;
				break;
			}
			usage4 (_("PCPU must be a float!"));
		case 'm':
			asprintf (&metric_name, "%s", optarg);
			if ( strcmp(optarg, "PROCS") == 0) {
				metric = METRIC_PROCS;
				break;
			} 
			else if ( strcmp(optarg, "VSZ") == 0) {
				metric = METRIC_VSZ;
				break;
			} 
			else if ( strcmp(optarg, "RSS") == 0 ) {
				metric = METRIC_RSS;
				break;
			}
			else if ( strcmp(optarg, "CPU") == 0 ) {
				metric = METRIC_CPU;
				break;
			}
			else if ( strcmp(optarg, "ELAPSED") == 0) {
				metric = METRIC_ELAPSED;
				break;
			}
				
			usage4 (_("Metric must be one of PROCS, VSZ, RSS, CPU, ELAPSED!"));
		case 'v':									/* command */
			verbose++;
			break;
		}
	}

	c = optind;
	if (wmax == -1 && argv[c])
		wmax = atoi (argv[c++]);
	if (cmax == -1 && argv[c])
		cmax = atoi (argv[c++]);
	if (statopts == NULL && argv[c]) {
		asprintf (&statopts, "%s", argv[c++]);
		asprintf (&fmt, _("%s%sSTATE = %s"), (fmt ? fmt : ""), (options ? ", " : ""), statopts);
		options |= STAT;
	}

	return validate_arguments ();
}



int
validate_arguments ()
{

	if (wmax >= 0 && wmin == -1)
		wmin = 0;
	if (cmax >= 0 && cmin == -1)
		cmin = 0;
	if (wmax >= wmin && cmax >= cmin) {	/* standard ranges */
		if (wmax > cmax && cmax != -1) {
			printf (_("wmax (%d) cannot be greater than cmax (%d)\n"), wmax, cmax);
			return ERROR;
		}
		if (cmin > wmin && wmin != -1) {
			printf (_("wmin (%d) cannot be less than cmin (%d)\n"), wmin, cmin);
			return ERROR;
		}
	}

/* 	if (wmax == -1 && cmax == -1 && wmin == -1 && cmin == -1) { */
/* 		printf ("At least one threshold must be set\n"); */
/* 		return ERROR; */
/* 	} */

	if (options == 0)
		options = ALL;

	if (statopts==NULL)
		statopts = strdup("");

	if (prog==NULL)
		prog = strdup("");

	if (args==NULL)
		args = strdup("");

	if (fmt==NULL)
		fmt = strdup("");

	if (fails==NULL)
		fails = strdup("");

	return options;
}



/* Check thresholds against value */
int
check_thresholds (int value)
{
 	if (wmax == -1 && cmax == -1 && wmin == -1 && cmin == -1) {
		return OK;
 	}
	else if (cmax >= 0 && cmin >= 0 && cmax < cmin) {
		if (value > cmax && value < cmin)
			return STATE_CRITICAL;
	}
	else if (cmax >= 0 && value > cmax) {
		return STATE_CRITICAL;
	}
	else if (cmin >= 0 && value < cmin) {
		return STATE_CRITICAL;
	}

	if (wmax >= 0 && wmin >= 0 && wmax < wmin) {
		if (value > wmax && value < wmin) {
			return STATE_WARNING;
		}
	}
	else if (wmax >= 0 && value > wmax) {
		return STATE_WARNING;
	}
	else if (wmin >= 0 && value < wmin) {
		return STATE_WARNING;
	}
	return STATE_OK;
}


/* convert the elapsed time to seconds */
int
convert_to_seconds(char *etime) {

	char *ptr;
	int total;

	int hyphcnt;
	int coloncnt;
	int days;
	int hours;
	int minutes;
	int seconds;

	hyphcnt = 0;
	coloncnt = 0;
	days = 0;
	hours = 0;
	minutes = 0;
	seconds = 0;

	for (ptr = etime; *ptr != '\0'; ptr++) {
	
		if (*ptr == '-') {
			hyphcnt++;
			continue;
		}
		if (*ptr == ':') {
			coloncnt++;
			continue;
		}
	}

	if (hyphcnt > 0) {
		sscanf(etime, "%d-%d:%d:%d",
				&days, &hours, &minutes, &seconds);
		/* linux 2.6.5/2.6.6 reporting some processes with infinite
		 * elapsed times for some reason */
		if (days == 49710) {
			return 0;
		}
	} else {
		if (coloncnt == 2) {
			sscanf(etime, "%d:%d:%d",
				&hours, &minutes, &seconds);
		} else if (coloncnt == 1) {
			sscanf(etime, "%d:%d",
				&minutes, &seconds);
		}
	}

	total = (days * 86400) +
		(hours * 3600) +
		(minutes * 60) +
		seconds;

	if (verbose >= 3 && metric == METRIC_ELAPSED) {
			printf("seconds: %d\n", total);
	}
	return total;
}


void
print_help (void)
{
	print_revision (progname, revision);

	printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>");
	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("Checks all processes and generates WARNING or CRITICAL states if the specified"));
  printf ("%s\n", _("metric is outside the required threshold ranges. The metric defaults to number"));
  printf ("%s\n", _("of processes.  Search filters can be applied to limit the processes to check."));

  printf ("\n\n");
  
	print_usage ();

	printf ("%s\n", _("Required Arguments:"));
  printf (" %s\n", "-w, --warning=RANGE");
  printf ("   %s\n", _("Generate warning state if metric is outside this range"));
  printf (" %s\n", "-c, --critical=RANGE");
  printf ("   %s\n", _("Generate critical state if metric is outside this range"));

	printf ("%s\n", _("Optional Arguments:"));
  printf (" %s\n", "-m, --metric=TYPE");
  printf ("  %s\n", _("Check thresholds against metric. Valid types:"));
  printf ("  %s\n", _("PROCS   - number of processes (default)"));
  printf ("  %s\n", _("VSZ     - virtual memory size"));
  printf ("  %s\n", _("RSS     - resident set memory size"));
  printf ("  %s\n", _("CPU     - percentage cpu"));
/* only linux etime is support currently */
#if defined( __linux__ )
	printf ("  %s\n", _("ELAPSED - time elapsed in seconds"));
#endif /* defined(__linux__) */
	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);

	printf (" %s\n", "-v, --verbose");
  printf ("    %s\n", _("Extra information. Up to 3 verbosity levels"));

	printf ("%s\n", "Optional Filters:");
  printf (" %s\n", "-s, --state=STATUSFLAGS");
  printf ("   %s\n", _("Only scan for processes that have, in the output of `ps`, one or"));
  printf ("   %s\n", _("more of the status flags you specify (for example R, Z, S, RS,"));
  printf ("   %s\n", _("RSZDT, plus others based on the output of your 'ps' command)."));
  printf (" %s\n", "-p, --ppid=PPID");
  printf ("   %s\n", _("Only scan for children of the parent process ID indicated."));
  printf (" %s\n", "-z, --vsz=VSZ");
  printf ("   %s\n", _("Only scan for processes with vsz higher than indicated."));
  printf (" %s\n", "-r, --rss=RSS");
  printf ("   %s\n", _("Only scan for processes with rss higher than indicated."));
	printf (" %s\n", "-P, --pcpu=PCPU");
  printf ("   %s\n", _("Only scan for processes with pcpu higher than indicated."));
  printf (" %s\n", "-u, --user=USER");
  printf ("   %s\n", _("Only scan for processes with user name or ID indicated."));
  printf (" %s\n", "-a, --argument-array=STRING");
  printf ("   %s\n", _("Only scan for processes with args that contain STRING."));
  printf (" %s\n", "-C, --command=COMMAND");
  printf ("   %s\n", _("Only scan for exact matches of COMMAND (without path)."));

	printf(_("\n\
RANGEs are specified 'min:max' or 'min:' or ':max' (or 'max'). If\n\
specified 'max:min', a warning status will be generated if the\n\
count is inside the specified range\n\n"));

	printf(_("\
This plugin checks the number of currently running processes and\n\
generates WARNING or CRITICAL states if the process count is outside\n\
the specified threshold ranges. The process count can be filtered by\n\
process owner, parent process PID, current state (e.g., 'Z'), or may\n\
be the total number of running processes\n\n"));

	printf ("%s\n", _("Examples:"));
  printf (" %s\n", "check_procs -w 2:2 -c 2:1024 -C portsentry");
  printf ("  %s\n", _("Warning if not two processes with command name portsentry."));
  printf ("  %s\n\n", _("Critical if < 2 or > 1024 processes"));
  printf (" %s\n", "check_procs -w 10 -a '/usr/local/bin/perl' -u root");
  printf ("  %s\n", _("Warning alert if > 10 processes with command arguments containing"));
  printf ("  %s\n\n", _("'/usr/local/bin/perl' and owned by root"));
  printf (" %s\n", "check_procs -w 50000 -c 100000 --metric=VSZ");
  printf ("  %s\n\n", _("Alert if vsz of any processes over 50K or 100K"));
  printf (" %s\n", "check_procs -w 10 -c 20 --metric=CPU");
  printf ("  %s\n\n", _("Alert if cpu of any processes over 10%% or 20%%"));

	printf (_(UT_SUPPORT));
}

void
print_usage (void)
{
  printf (_("Usage:"));
	printf ("%s -w <range> -c <range> [-m metric] [-s state] [-p ppid]\n", progname);
  printf (" [-u user] [-r rss] [-z vsz] [-P %%cpu] [-a argument-array]\n");
  printf (" [-C command] [-t timeout] [-v]\n");
}
