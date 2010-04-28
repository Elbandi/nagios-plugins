/*****************************************************************************
*
* Nagios check_dig plugin
*
* License: GPL
* Copyright (c) 1999-2006 nagios-plugins team
*
* Last Modified: $Date: 2007-01-28 21:46:41 +0000 (Sun, 28 Jan 2007) $
*
* Description:
*
* This file contains the check_dig plugin
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
* $Id: check_dig.c 1590 2007-01-28 21:46:41Z hweiss $
*
*****************************************************************************/

/* Hackers note:
 *  There are typecasts to (char *) from _("foo bar") in this file.
 *  They prevent compiler warnings. Never (ever), permute strings obtained
 *  that are typecast from (const char *) (which happens when --disable-nls)
 *  because on some architectures those strings are in non-writable memory */

const char *progname = "check_dig";
const char *revision = "$Revision: 1590 $";
const char *copyright = "2002-2006";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"
#include "runcmd.h"

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

#define UNDEFINED 0
#define DEFAULT_PORT 53

char *query_address = NULL;
char *record_type = "A";
char *expected_address = NULL;
char *dns_server = NULL;
int verbose = FALSE;
int server_port = DEFAULT_PORT;
double warning_interval = UNDEFINED;
double critical_interval = UNDEFINED;
struct timeval tv;

int
main (int argc, char **argv)
{
  char *command_line;
  output chld_out, chld_err;
  char *msg = NULL;
  size_t i;
  char *t;
  long microsec;
  double elapsed_time;
  int result = STATE_UNKNOWN;

  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  /* Set signal handling and alarm */
  if (signal (SIGALRM, popen_timeout_alarm_handler) == SIG_ERR)
    usage_va(_("Cannot catch SIGALRM"));

  if (process_arguments (argc, argv) == ERROR)
    usage_va(_("Could not parse arguments"));

  /* get the command to run */
  asprintf (&command_line, "%s @%s -p %d %s -t %s",
            PATH_TO_DIG, dns_server, server_port, query_address, record_type);

  alarm (timeout_interval);
  gettimeofday (&tv, NULL);

  if (verbose) {
    printf ("%s\n", command_line);
    if(expected_address != NULL) {
      printf (_("Looking for: '%s'\n"), expected_address);
    } else {
      printf (_("Looking for: '%s'\n"), query_address);
    }
  }

  /* run the command */
  if(np_runcmd(command_line, &chld_out, &chld_err, 0) != 0) {
    result = STATE_WARNING;
    msg = (char *)_("dig returned an error status");
  }

  for(i = 0; i < chld_out.lines; i++) {
    /* the server is responding, we just got the host name... */
    if (strstr (chld_out.line[i], ";; ANSWER SECTION:")) {

      /* loop through the whole 'ANSWER SECTION' */
      for(; i < chld_out.lines; i++) {
        /* get the host address */
        if (verbose)
          printf ("%s\n", chld_out.line[i]);

        if (strstr (chld_out.line[i], (expected_address == NULL ? query_address : expected_address)) != NULL) {
          msg = chld_out.line[i];
          result = STATE_OK;

          /* Translate output TAB -> SPACE */
          t = msg;
          while ((t = strchr(t, '\t')) != NULL) *t = ' ';
          break;
        }
      }

      if (result == STATE_UNKNOWN) {
        msg = (char *)_("Server not found in ANSWER SECTION");
        result = STATE_WARNING;
      }

      /* we found the answer section, so break out of the loop */
      break;
    }
  }

  if (result == STATE_UNKNOWN)
    msg = (char *)_("No ANSWER SECTION found");

  /* If we get anything on STDERR, at least set warning */
  if(chld_err.buflen > 0) {
    result = max_state(result, STATE_WARNING);
    if(!msg) for(i = 0; i < chld_err.lines; i++) {
      msg = strchr(chld_err.line[0], ':');
      if(msg) {
        msg++;
        break;
      }
    }
  }

  microsec = deltime (tv);
  elapsed_time = (double)microsec / 1.0e6;

  if (critical_interval > UNDEFINED && elapsed_time > critical_interval)
    result = STATE_CRITICAL;

  else if (warning_interval > UNDEFINED && elapsed_time > warning_interval)
    result = STATE_WARNING;

  printf ("DNS %s - %.3f seconds response time (%s)|%s\n",
          state_text (result), elapsed_time,
          msg ? msg : _("Probably a non-existent host/domain"),
          fperfdata("time", elapsed_time, "s",
                    (warning_interval>UNDEFINED?TRUE:FALSE),
                    warning_interval,
                    (critical_interval>UNDEFINED?TRUE:FALSE),
            critical_interval,
            TRUE, 0, FALSE, 0));
  return result;
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
  int c;

  int option = 0;
  static struct option longopts[] = {
    {"hostname", required_argument, 0, 'H'},
    {"query_address", required_argument, 0, 'l'},
    {"warning", required_argument, 0, 'w'},
    {"critical", required_argument, 0, 'c'},
    {"timeout", required_argument, 0, 't'},
    {"verbose", no_argument, 0, 'v'},
    {"version", no_argument, 0, 'V'},
    {"help", no_argument, 0, 'h'},
    {"record_type", required_argument, 0, 'T'},
    {"expected_address", required_argument, 0, 'a'},
    {"port", required_argument, 0, 'p'},
    {0, 0, 0, 0}
  };

  if (argc < 2)
    return ERROR;

  while (1) {
    c = getopt_long (argc, argv, "hVvt:l:H:w:c:T:p:a:", longopts, &option);

    if (c == -1 || c == EOF)
      break;

    switch (c) {
    case 'h':                 /* help */
      print_help ();
      exit (STATE_OK);
    case 'V':                 /* version */
      print_revision (progname, revision);
      exit (STATE_OK);
    case 'H':                 /* hostname */
      host_or_die(optarg);
      dns_server = optarg;
      break;
    case 'p':                 /* server port */
      if (is_intpos (optarg)) {
        server_port = atoi (optarg);
      }
      else {
        usage_va(_("Port must be a positive integer - %s"), optarg);
      }
      break;
    case 'l':                 /* address to lookup */
      query_address = optarg;
      break;
    case 'w':                 /* warning */
      if (is_nonnegative (optarg)) {
        warning_interval = strtod (optarg, NULL);
      }
      else {
        usage_va(_("Warning interval must be a positive integer - %s"), optarg);
      }
      break;
    case 'c':                 /* critical */
      if (is_nonnegative (optarg)) {
        critical_interval = strtod (optarg, NULL);
      }
      else {
        usage_va(_("Critical interval must be a positive integer - %s"), optarg);
      }
      break;
    case 't':                 /* timeout */
      if (is_intnonneg (optarg)) {
        timeout_interval = atoi (optarg);
      }
      else {
        usage_va(_("Timeout interval must be a positive integer - %s"), optarg);
      }
      break;
    case 'v':                 /* verbose */
      verbose = TRUE;
      break;
    case 'T':
      record_type = optarg;
      break;
    case 'a':
      expected_address = optarg;
      break;
    default:                  /* usage5 */
      usage5();
    }
  }

  c = optind;
  if (dns_server == NULL) {
    if (c < argc) {
      host_or_die(argv[c]);
      dns_server = argv[c];
    }
    else {
      dns_server = strdup ("127.0.0.1");
    }
  }

  return validate_arguments ();
}



int
validate_arguments (void)
{
  return OK;
}



void
print_help (void)
{
  char *myport;

  asprintf (&myport, "%d", DEFAULT_PORT);

  print_revision (progname, revision);

  printf ("Copyright (c) 2000 Karl DeBisschop <kdebisschop@users.sourceforge.net>\n");
  printf (COPYRIGHT, copyright, email);

  printf (_("This plugin test the DNS service on the specified host using dig"));

  printf ("\n\n");

  print_usage ();

  printf (_(UT_HELP_VRSN));

  printf (_(UT_HOST_PORT), 'p', myport);

  printf (" %s\n","-l, --lookup=STRING");
  printf ("    %s\n",_("machine name to lookup"));
  printf (" %s\n","-T, --record_type=STRING");
  printf ("    %s\n",_("record type to lookup (default: A)"));
  printf (" %s\n","-a, --expected_address=STRING");
  printf ("    %s\n",_("an address expected to be in the answer section.if not set, uses whatever was in -l"));
  printf (_(UT_WARN_CRIT));
  printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);
  printf (_(UT_VERBOSE));
  printf (_(UT_SUPPORT));
}



void
print_usage (void)
{
  printf (_("Usage:"));
  printf ("%s -H host -l lookup [-p <server port>] [-T <query type>]", progname);
  printf (" [-w <warning interval>] [-c <critical interval>] [-t <timeout>]");
  printf (" [-a <expected answer address>] [-v]\n");
}
