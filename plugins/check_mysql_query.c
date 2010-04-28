/******************************************************************************
*
* Nagios check_mysql_query plugin
*
* License: GPL
* Copyright (c) 2006 nagios-plugins team, after Didi Rieder (check_mysql)
*
* Last Modified: $Date: 2007-01-28 21:46:41 +0000 (Sun, 28 Jan 2007) $
*
* Description:
*
* This file contains the check_mysql_query plugin
*
*  This plugin is for running arbitrary SQL and checking the results
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
* CHECK_MYSQL_QUERY.C
*
* $Id: check_mysql_query.c 1590 2007-01-28 21:46:41Z hweiss $
*
******************************************************************************/

const char *progname = "check_mysql_query";
const char *revision = "$Revision: 1590 $";
const char *copyright = "2006";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "utils.h"
#include "utils_base.h"
#include "netutils.h"

#include <mysql.h>
#include <errmsg.h>

char *db_user = NULL;
char *db_host = NULL;
char *db_pass = NULL;
char *db = NULL;
unsigned int db_port = MYSQL_PORT;

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

char *sql_query = NULL;
int verbose = 0;
thresholds *my_thresholds = NULL;


int
main (int argc, char **argv)
{

	MYSQL mysql;
	MYSQL_RES *res;
	MYSQL_ROW row;
	
	double value;
	char *error = NULL;
	int status;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* initialize mysql  */
	mysql_init (&mysql);

	mysql_options(&mysql,MYSQL_READ_DEFAULT_GROUP,"client");

	/* establish a connection to the server and error checking */
	if (!mysql_real_connect(&mysql,db_host,db_user,db_pass,db,db_port,NULL,0)) {
		if (mysql_errno (&mysql) == CR_UNKNOWN_HOST)
			die (STATE_WARNING, "QUERY %s: %s\n", _("WARNING"), mysql_error (&mysql));
		else if (mysql_errno (&mysql) == CR_VERSION_ERROR)
			die (STATE_WARNING, "QUERY %s: %s\n", _("WARNING"), mysql_error (&mysql));
		else if (mysql_errno (&mysql) == CR_OUT_OF_MEMORY)
			die (STATE_WARNING, "QUERY %s: %s\n", _("WARNING"), mysql_error (&mysql));
		else if (mysql_errno (&mysql) == CR_IPSOCK_ERROR)
			die (STATE_WARNING, "QUERY %s: %s\n", _("WARNING"), mysql_error (&mysql));
		else if (mysql_errno (&mysql) == CR_SOCKET_CREATE_ERROR)
			die (STATE_WARNING, "QUERY %s: %s\n", _("WARNING"), mysql_error (&mysql));
		else
			die (STATE_CRITICAL, "QUERY %s: %s\n", _("CRITICAL"), mysql_error (&mysql));
	}

	if (mysql_query (&mysql, sql_query) != 0) {
		error = strdup(mysql_error(&mysql));
		mysql_close (&mysql);
		die (STATE_CRITICAL, "QUERY %s: %s - %s\n", _("CRITICAL"), _("Error with query"), error);
	}

	/* store the result */
	if ( (res = mysql_store_result (&mysql)) == NULL) {
		error = strdup(mysql_error(&mysql));
		mysql_close (&mysql);
		die (STATE_CRITICAL, "QUERY %s: Error with store_result - %s\n", _("CRITICAL"), error);
	}

	/* Check there is some data */
	if (mysql_num_rows(res) == 0) {
		mysql_close(&mysql);
		die (STATE_WARNING, "QUERY %s: %s\n", _("WARNING"), _("No rows returned"));
	}

	/* fetch the first row */
	if ( (row = mysql_fetch_row (res)) == NULL) {
		error = strdup(mysql_error(&mysql));
		mysql_free_result (res);
		mysql_close (&mysql);
		die (STATE_CRITICAL, "QUERY %s: Fetch row error - %s\n", _("CRITICAL"), error);
	}

	/* free the result */
	mysql_free_result (res);

	/* close the connection */
	mysql_close (&mysql);

	if (! is_numeric(row[0])) {
		die (STATE_CRITICAL, "QUERY %s: %s - '%s'\n", _("CRITICAL"), _("Is not a numeric"), row[0]);
	}

	value = strtod(row[0], NULL);

	if (verbose >= 3)
		printf("mysql result: %f\n", value);

	status = get_status(value, my_thresholds);

	if (status == STATE_OK) {
		printf("QUERY %s: ", _("OK"));
	} else if (status == STATE_WARNING) {
		printf("QUERY %s: ", _("WARNING"));
	} else if (status == STATE_CRITICAL) {
		printf("QUERY %s: ", _("CRITICAL"));
	}
	printf(_("'%s' returned %f"), sql_query, value);
	printf("\n");

	return status;
}


/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;
	char *warning = NULL;
	char *critical = NULL;

	int option = 0;
	static struct option longopts[] = {
		{"hostname", required_argument, 0, 'H'},
		{"database", required_argument, 0, 'd'},
		{"username", required_argument, 0, 'u'},
		{"password", required_argument, 0, 'p'},
		{"port", required_argument, 0, 'P'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{"query", required_argument, 0, 'q'},
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{0, 0, 0, 0}
	};

	if (argc < 1)
		return ERROR;

	while (1) {
		c = getopt_long (argc, argv, "hvVSP:p:u:d:H:q:w:c:", longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'H':									/* hostname */
			if (is_host (optarg)) {
				db_host = optarg;
			}
			else {
				usage2 (_("Invalid hostname/address"), optarg);
			}
			break;
		case 'd':									/* hostname */
			db = optarg;
			break;
		case 'u':									/* username */
			db_user = optarg;
			break;
		case 'p':									/* authentication information: password */
			asprintf(&db_pass, "%s", optarg);

			/* Delete the password from process list */
			while (*optarg != '\0') {
				*optarg = 'X';
				optarg++;
			}
			break;
		case 'P':									/* critical time threshold */
			db_port = atoi (optarg);
			break;
		case 'v':
			verbose++;
			break;
		case 'V':									/* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'q':
			asprintf(&sql_query, "%s", optarg);
			break;
		case 'w':
			warning = optarg;
			break;
		case 'c':
			critical = optarg;
			break;
		case '?':									/* help */
			usage5 ();
		}
	}

	c = optind;

	set_thresholds(&my_thresholds, warning, critical);

	return validate_arguments ();
}


int
validate_arguments (void)
{
	if (sql_query == NULL)
		usage("Must specify a SQL query to run");

	if (db_user == NULL)
		db_user = strdup("");

	if (db_host == NULL)
		db_host = strdup("");

	if (db_pass == NULL)
		db_pass == strdup("");

	if (db == NULL)
		db = strdup("");

	return OK;
}


void
print_help (void)
{
	char *myport;
	asprintf (&myport, "%d", MYSQL_PORT);

	print_revision (progname, revision);

	printf (_(COPYRIGHT), copyright, email);

	printf ("%s\n", _("This program checks a query result against threshold levels"));

  printf ("\n\n");

	print_usage ();

	printf (_(UT_HELP_VRSN));
	printf (" -q, --query=STRING\n");
	printf ("    %s\n", _("SQL query to run. Only first column in first row will be read"));
	printf (_(UT_WARN_CRIT_RANGE));
	printf (_(UT_HOST_PORT), 'P', myport);
	printf (" -d, --database=STRING\n");
	printf ("    %s\n", _("Database to check"));
	printf (" -u, --username=STRING\n");
	printf ("    %s\n", _("Username to login with"));
	printf (" -p, --password=STRING\n");
	printf ("    %s\n", _("Password to login with"));
	printf ("    ==> %s <==\n", _("IMPORTANT: THIS FORM OF AUTHENTICATION IS NOT SECURE!!!"));

	printf ("\n");

	printf ("%s\n", _("A query is required. The result from the query should be numeric."));
	printf ("%s\n", _("For extra security, create a user with minimal access."));

	printf (_(UT_SUPPORT));
}


void
print_usage (void)
{
  printf (_("Usage:"));
	printf ("%s -q SQL_query [-w warn] [-c crit]\n",progname);
  printf ("[-d database] [-H host] [-P port] [-u user] [-p password]\n");
}
