/******************************************************************************
*
* Nagios check_snmp plugin
*
* License: GPL
* Copyright (c) 1999-2007 nagios-plugins team
*
* Last Modified: $Date: 2007-12-10 07:52:00 +0000 (Mon, 10 Dec 2007) $
*
* Description:
*
* This file contains the check_snmp plugin
*
*  Check status of remote machines and obtain system information via SNMP
*
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
* $Id: check_snmp.c 1859 2007-12-10 07:52:00Z dermoth $
* 
******************************************************************************/

const char *progname = "check_snmp";
const char *revision = "$Revision: 1859 $";
const char *copyright = "1999-2007";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "utils.h"
#include "popen.h"

#define DEFAULT_COMMUNITY "public"
#define DEFAULT_PORT "161"
#define DEFAULT_MIBLIST "ALL"
#define DEFAULT_PROTOCOL "1"
#define DEFAULT_TIMEOUT 1
#define DEFAULT_RETRIES 5
#define DEFAULT_AUTH_PROTOCOL "MD5"
#define DEFAULT_DELIMITER "="
#define DEFAULT_OUTPUT_DELIMITER " "

#define mark(a) ((a)!=0?"*":"")

#define CHECK_UNDEF 0
#define CRIT_PRESENT 1
#define CRIT_STRING 2
#define CRIT_REGEX 4
#define CRIT_GT 8
#define CRIT_LT 16
#define CRIT_GE 32
#define CRIT_LE 64
#define CRIT_EQ 128
#define CRIT_NE 256
#define CRIT_RANGE 512
#define WARN_PRESENT 1024
#define WARN_STRING 2048
#define WARN_REGEX 4096
#define WARN_GT 8192
#define WARN_LT 16384
#define WARN_GE 32768
#define WARN_LE 65536
#define WARN_EQ 131072
#define WARN_NE 262144
#define WARN_RANGE 524288

#define MAX_OIDS 8
#define MAX_DELIM_LENGTH 8

int process_arguments (int, char **);
int validate_arguments (void);
char *clarify_message (char *);
int check_num (int);
int llu_getll (unsigned long long *, char *);
int llu_getul (unsigned long long *, char *);
char *thisarg (char *str);
char *nextarg (char *str);
void print_usage (void);
void print_help (void);

#include "regex.h"
char regex_expect[MAX_INPUT_BUFFER] = "";
regex_t preg;
regmatch_t pmatch[10];
char timestamp[10] = "";
char errbuf[MAX_INPUT_BUFFER] = "";
char perfstr[MAX_INPUT_BUFFER] = "";
int cflags = REG_EXTENDED | REG_NOSUB | REG_NEWLINE;
int eflags = 0;
int errcode, excode;

char *server_address = NULL;
char *community = NULL;
char *authpriv = NULL;
char *proto = NULL;
char *seclevel = NULL;
char *secname = NULL;
char *authproto = NULL;
char *authpasswd = NULL;
char *privpasswd = NULL;
char *oid;
char *label;
char *units;
char *port;
char string_value[MAX_INPUT_BUFFER] = "";
char **labels = NULL;
char **unitv = NULL;
size_t nlabels = 0;
size_t labels_size = 8;
size_t nunits = 0;
size_t unitv_size = 8;
int verbose = FALSE;
int usesnmpgetnext = FALSE;
unsigned long long lower_warn_lim[MAX_OIDS];
unsigned long long upper_warn_lim[MAX_OIDS];
unsigned long long lower_crit_lim[MAX_OIDS];
unsigned long long upper_crit_lim[MAX_OIDS];
unsigned long long response_value[MAX_OIDS];
int check_warning_value = FALSE;
int check_critical_value = FALSE;
int retries = 0;
unsigned long long eval_method[MAX_OIDS];
char *delimiter;
char *output_delim;
char *miblist = NULL;
int needmibs = FALSE;


int
main (int argc, char **argv)
{
	int i = 0;
	int iresult = STATE_UNKNOWN;
	int found = 0;
	int result = STATE_DEPENDENT;
	char input_buffer[MAX_INPUT_BUFFER];
	char *command_line = NULL;
	char *cl_hidden_auth = NULL;
	char *response = NULL;
	char *outbuff;
	char *output;
	char *ptr = NULL;
	char *p2 = NULL;
	char *show = NULL;
	char type[8] = "";

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	labels = malloc (labels_size);
	unitv = malloc (unitv_size);
	for (i = 0; i < MAX_OIDS; i++)
		eval_method[i] = CHECK_UNDEF;
	i = 0;

	oid = strdup ("");
	label = strdup ("SNMP");
	units = strdup ("");
	port = strdup (DEFAULT_PORT);
	outbuff = strdup ("");
	output = strdup ("");
	delimiter = strdup (" = ");
	output_delim = strdup (DEFAULT_OUTPUT_DELIMITER);
	/* miblist = strdup (DEFAULT_MIBLIST); */
	timeout_interval = DEFAULT_TIMEOUT;
	retries = DEFAULT_RETRIES;

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* create the command line to execute */
		if(usesnmpgetnext == TRUE) {
		asprintf(&command_line, "%s -t %d -r %d -m %s -v %s %s %s:%s %s",
			PATH_TO_SNMPGETNEXT, timeout_interval, retries, miblist, proto,
			authpriv, server_address, port, oid);
		asprintf(&cl_hidden_auth, "%s -t %d -r %d -m %s -v %s %s %s:%s %s",
			PATH_TO_SNMPGETNEXT, timeout_interval, retries, miblist, proto,
			"[authpriv]", server_address, port, oid);
	}else{

		asprintf (&command_line, "%s -t %d -r %d -m %s -v %s %s %s:%s %s",
			PATH_TO_SNMPGET, timeout_interval, retries, miblist, proto,
			authpriv, server_address, port, oid);
		asprintf(&cl_hidden_auth, "%s -t %d -r %d -m %s -v %s %s %s:%s %s",
			PATH_TO_SNMPGET, timeout_interval, retries, miblist, proto,
			"[authpriv]", server_address, port, oid);
	}
	
	if (verbose)
		printf ("%s\n", command_line);
	

	/* run the command */
	child_process = spopen (command_line);
	if (child_process == NULL) {
		printf (_("Could not open pipe: %s\n"), cl_hidden_auth);
		exit (STATE_UNKNOWN);
	}

#if 0		/* Removed May 29, 2007 */
	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL) {
		printf (_("Could not open stderr for %s\n"), cl_hidden_auth);
	}
#endif

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process))
		asprintf (&output, "%s%s", output, input_buffer);

	if (verbose)
		printf ("%s\n", output);

	ptr = output;

	strncat(perfstr, "| ", sizeof(perfstr)-strlen(perfstr)-1);
	while (ptr) {
		char *foo;
		unsigned int copylen;

		foo = strstr (ptr, delimiter);
		copylen = foo-ptr;
		if (copylen > sizeof(perfstr)-strlen(perfstr)-1)
			copylen = sizeof(perfstr)-strlen(perfstr)-1;
		strncat(perfstr, ptr, copylen);
		ptr = foo; 

		if (ptr == NULL)
			break;

		ptr += strlen (delimiter);
		ptr += strspn (ptr, " ");

		found++;

		if (ptr[0] == '"') {
			ptr++;
			response = strpcpy (response, ptr, "\"");
			ptr = strpbrk (ptr, "\"");
			ptr += strspn (ptr, "\"\n");
		}
		else {
			response = strpcpy (response, ptr, "\n");
			ptr = strpbrk (ptr, "\n");
			ptr += strspn (ptr, "\n");
			while
				(strstr (ptr, delimiter) &&
				 strstr (ptr, "\n") && strstr (ptr, "\n") < strstr (ptr, delimiter)) {
				response = strpcat (response, ptr, "\n");
				ptr = strpbrk (ptr, "\n");
			}
			if (ptr && strstr (ptr, delimiter) == NULL) {
				asprintf (&response, "%s%s", response, ptr);
				ptr = NULL;
			}
		}

		/* We strip out the datatype indicator for PHBs */

		/* Clean up type array - Sol10 does not necessarily zero it out */
		bzero(type, sizeof(type));

		if (strstr (response, "Gauge: "))
			show = strstr (response, "Gauge: ") + 7;
		else if (strstr (response, "Gauge32: "))
			show = strstr (response, "Gauge32: ") + 9;
		else if (strstr (response, "Counter32: ")) {
			show = strstr (response, "Counter32: ") + 11;
			strcpy(type, "c");
		}
		else if (strstr (response, "Counter64: ")) {
			show = strstr (response, "Counter64: ") + 11;
			strcpy(type, "c");
		}
		else if (strstr (response, "INTEGER: "))
			show = strstr (response, "INTEGER: ") + 9;
		else if (strstr (response, "STRING: "))
			show = strstr (response, "STRING: ") + 8;
		else
			show = response;
		p2 = show;

		iresult = STATE_DEPENDENT;

		/* Process this block for integer comparisons */
		if (eval_method[i] & CRIT_GT ||
		    eval_method[i] & CRIT_LT ||
		    eval_method[i] & CRIT_GE ||
		    eval_method[i] & CRIT_LE ||
		    eval_method[i] & CRIT_EQ ||
		    eval_method[i] & CRIT_NE ||
		    eval_method[i] & WARN_GT ||
		    eval_method[i] & WARN_LT ||
		    eval_method[i] & WARN_GE ||
		    eval_method[i] & WARN_LE ||
		    eval_method[i] & WARN_EQ ||
		    eval_method[i] & WARN_NE) {
			p2 = strpbrk (p2, "0123456789");
			if (p2 == NULL) 
				die (STATE_UNKNOWN,_("No valid data returned"));
			response_value[i] = strtoul (p2, NULL, 10);
			iresult = check_num (i);
			asprintf (&show, "%llu", response_value[i]);
		}

		/* Process this block for string matching */
		else if (eval_method[i] & CRIT_STRING) {
			if (strcmp (show, string_value))
				iresult = STATE_CRITICAL;
			else
				iresult = STATE_OK;
		}

		/* Process this block for regex matching */
		else if (eval_method[i] & CRIT_REGEX) {
			excode = regexec (&preg, response, 10, pmatch, eflags);
			if (excode == 0) {
				iresult = STATE_OK;
			}
			else if (excode != REG_NOMATCH) {
				regerror (excode, &preg, errbuf, MAX_INPUT_BUFFER);
				printf (_("Execute Error: %s\n"), errbuf);
				exit (STATE_CRITICAL);
			}
			else {
				iresult = STATE_CRITICAL;
			}
		}

		/* Process this block for existence-nonexistence checks */
		else {
			if (eval_method[i] & CRIT_PRESENT)
				iresult = STATE_CRITICAL;
			else if (eval_method[i] & WARN_PRESENT)
				iresult = STATE_WARNING;
			else if (response && iresult == STATE_DEPENDENT) 
				iresult = STATE_OK;
		}

		/* Result is the worst outcome of all the OIDs tested */
		result = max_state (result, iresult);

		/* Prepend a label for this OID if there is one */
		if (nlabels > (size_t)1 && (size_t)i < nlabels && labels[i] != NULL)
			asprintf (&outbuff, "%s%s%s %s%s%s", outbuff,
				(i == 0) ? " " : output_delim,
				labels[i], mark (iresult), show, mark (iresult));
		else
			asprintf (&outbuff, "%s%s%s%s%s", outbuff, (i == 0) ? " " : output_delim,
				mark (iresult), show, mark (iresult));

		/* Append a unit string for this OID if there is one */
		if (nunits > (size_t)0 && (size_t)i < nunits && unitv[i] != NULL)
			asprintf (&outbuff, "%s %s", outbuff, unitv[i]);

		i++;

		strncat(perfstr, "=", sizeof(perfstr)-strlen(perfstr)-1);
		strncat(perfstr, show, sizeof(perfstr)-strlen(perfstr)-1);
		if (type)
			strncat(perfstr, type, sizeof(perfstr)-strlen(perfstr)-1);
		strncat(perfstr, " ", sizeof(perfstr)-strlen(perfstr)-1);

	}	/* end while (ptr) */

	if (found == 0)
		die (STATE_UNKNOWN,
			_("%s problem - No data received from host\nCMD: %s\n"),
			label,
			cl_hidden_auth);

#if 0		/* Removed May 29, 2007 */
	/* WARNING if output found on stderr */
	if (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr))
		result = max_state (result, STATE_WARNING);

	/* close stderr */
	(void) fclose (child_stderr);
#endif

	/* close the pipe */
	if (spclose (child_process)) {
		if (result == STATE_OK)
			result = STATE_UNKNOWN;
		asprintf (&outbuff, "%s (%s)", outbuff, _("snmpget returned an error status"));
	}

/* 	if (nunits == 1 || i == 1) */
/* 		printf ("%s %s -%s %s\n", label, state_text (result), outbuff, units); */
/* 	else */
	printf ("%s %s -%s %s \n", label, state_text (result), outbuff, perfstr);

	return result;
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	char *ptr;
	int c = 1;
	int j = 0, jj = 0, ii = 0;

	int option = 0;
	static struct option longopts[] = {
		STD_LONG_OPTS,
		{"community", required_argument, 0, 'C'},
		{"oid", required_argument, 0, 'o'},
		{"object", required_argument, 0, 'o'},
		{"delimiter", required_argument, 0, 'd'},
		{"output-delimiter", required_argument, 0, 'D'},
		{"string", required_argument, 0, 's'},
		{"timeout", required_argument, 0, 't'},
		{"regex", required_argument, 0, 'r'},
		{"ereg", required_argument, 0, 'r'},
		{"eregi", required_argument, 0, 'R'},
		{"label", required_argument, 0, 'l'},
		{"units", required_argument, 0, 'u'},
		{"port", required_argument, 0, 'p'},
		{"retries", required_argument, 0, 'e'},
		{"miblist", required_argument, 0, 'm'},
		{"protocol", required_argument, 0, 'P'},
		{"seclevel", required_argument, 0, 'L'},
		{"secname", required_argument, 0, 'U'},
		{"authproto", required_argument, 0, 'a'},
		{"authpasswd", required_argument, 0, 'A'},
		{"privpasswd", required_argument, 0, 'X'},
		{"next", no_argument, 0, 'n'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	/* reverse compatibility for very old non-POSIX usage forms */
	for (c = 1; c < argc; c++) {
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");
		if (strcmp ("-wv", argv[c]) == 0)
			strcpy (argv[c], "-w");
		if (strcmp ("-cv", argv[c]) == 0)
			strcpy (argv[c], "-c");
	}

	while (1) {
		c = getopt_long (argc, argv, "nhvVt:c:w:H:C:o:e:E:d:D:s:t:R:r:l:u:p:m:P:L:U:a:A:X:",
									 longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':	/* usage */
			usage5 ();
		case 'h':	/* help */
			print_help ();
			exit (STATE_OK); 
		case 'V':	/* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 'v': /* verbose */
			verbose = TRUE;
			break;

	/* Connection info */
		case 'C':									/* group or community */
			community = optarg;
			break;
		case 'H':									/* Host or server */
			server_address = optarg;
			break;
		case 'p':	/* TCP port number */
			port = optarg;
			break;
		case 'm':	/* List of MIBS  */
			miblist = optarg;
			break;
		case 'n':	/* usesnmpgetnext */
			usesnmpgetnext = TRUE;
			break;
		case 'P':	/* SNMP protocol version */
			proto = optarg;
			break;
		case 'L':	/* security level */
			seclevel = optarg;
			break;
		case 'U':	/* security username */
			secname = optarg;
			break;
		case 'a':	/* auth protocol */
			authproto = optarg;
			break;
		case 'A':	/* auth passwd */
			authpasswd = optarg;
			break;
		case 'X':	/* priv passwd */
			privpasswd = optarg;
			break;
		case 't':	/* timeout period */
			if (!is_integer (optarg))
				usage2 (_("Timeout interval must be a positive integer"), optarg);
			else
				timeout_interval = atoi (optarg);
			break;

	/* Test parameters */
		case 'c':									/* critical time threshold */
			if (strspn (optarg, "0123456789:,") < strlen (optarg))
				usage2 (_("Invalid critical threshold"), optarg);
			for (ptr = optarg; ptr && jj < MAX_OIDS; jj++) {
				if (llu_getll (&lower_crit_lim[jj], ptr) == 1)
					eval_method[jj] |= CRIT_LT;
				if (llu_getul (&upper_crit_lim[jj], ptr) == 1)
					eval_method[jj] |= CRIT_GT;
				(ptr = index (ptr, ',')) ? ptr++ : ptr;
			}
			break;
		case 'w':									/* warning time threshold */
			if (strspn (optarg, "0123456789:,") < strlen (optarg))
				usage2 (_("Invalid warning threshold"), optarg);
			for (ptr = optarg; ptr && ii < MAX_OIDS; ii++) {
				if (llu_getll (&lower_warn_lim[ii], ptr) == 1)
					eval_method[ii] |= WARN_LT;
				if (llu_getul (&upper_warn_lim[ii], ptr) == 1)
					eval_method[ii] |= WARN_GT;
				(ptr = index (ptr, ',')) ? ptr++ : ptr;
			}
			break;
		case 'e': /* PRELIMINARY - may change */
		case 'E': /* PRELIMINARY - may change */
			if (!is_integer (optarg))
				usage2 (_("Retries interval must be a positive integer"), optarg);
			else
				retries = atoi(optarg);
			break;
		case 'o':									/* object identifier */
			if ( strspn( optarg, "0123456789.," ) != strlen( optarg ) ) {
					/*
					 * we have something other than digits, periods and comas,
					 * so we have a mib variable, rather than just an SNMP OID,
					 * so we have to actually read the mib files
					 */
					needmibs = TRUE;
			}

			for (ptr = optarg; (ptr = index (ptr, ',')); ptr++)
				ptr[0] = ' '; /* relpace comma with space */
			for (ptr = optarg; (ptr = index (ptr, ' ')); ptr++)
				j++; /* count OIDs */
			asprintf (&oid, "%s %s", (oid?oid:""), optarg);
			if (c == 'E' || c == 'e') {
				jj++;
				ii++;
			}
			if (c == 'E') 
				eval_method[j+1] |= WARN_PRESENT;
			else if (c == 'e')
				eval_method[j+1] |= CRIT_PRESENT;
			break;
		case 's':									/* string or substring */
			strncpy (string_value, optarg, sizeof (string_value) - 1);
			string_value[sizeof (string_value) - 1] = 0;
			eval_method[jj++] = CRIT_STRING;
			ii++;
			break;
		case 'R':									/* regex */
			cflags = REG_ICASE;
		case 'r':									/* regex */
			cflags |= REG_EXTENDED | REG_NOSUB | REG_NEWLINE;
			strncpy (regex_expect, optarg, sizeof (regex_expect) - 1);
			regex_expect[sizeof (regex_expect) - 1] = 0;
			errcode = regcomp (&preg, regex_expect, cflags);
			if (errcode != 0) {
				regerror (errcode, &preg, errbuf, MAX_INPUT_BUFFER);
				printf (_("Could Not Compile Regular Expression"));
				return ERROR;
			}
			eval_method[jj++] = CRIT_REGEX;
			ii++;
			break;

	/* Format */
		case 'd':									/* delimiter */
			delimiter = strscpy (delimiter, optarg);
			break;
		case 'D':									/* output-delimiter */
			output_delim = strscpy (output_delim, optarg);
			break;
		case 'l':									/* label */
			label = optarg;
			nlabels++;
			if (nlabels >= labels_size) {
				labels_size += 8;
				labels = realloc (labels, labels_size);
				if (labels == NULL)
					die (STATE_UNKNOWN, _("Could not reallocate labels[%d]"), (int)nlabels);
			}
			labels[nlabels - 1] = optarg;
			ptr = thisarg (optarg);
			labels[nlabels - 1] = ptr;
			if (strstr (ptr, "'") == ptr)
				labels[nlabels - 1] = ptr + 1;
			while (ptr && (ptr = nextarg (ptr))) {
				if (nlabels >= labels_size) {
					labels_size += 8;
					labels = realloc (labels, labels_size);
					if (labels == NULL)
						die (STATE_UNKNOWN, _("Could not reallocate labels\n"));
				}
				labels++;
				ptr = thisarg (ptr);
				if (strstr (ptr, "'") == ptr)
					labels[nlabels - 1] = ptr + 1;
				else
					labels[nlabels - 1] = ptr;
			}
			break;
		case 'u':									/* units */
			units = optarg;
			nunits++;
			if (nunits >= unitv_size) {
				unitv_size += 8;
				unitv = realloc (unitv, unitv_size);
				if (unitv == NULL)
					die (STATE_UNKNOWN, _("Could not reallocate units [%d]\n"), (int)nunits);
			}
			unitv[nunits - 1] = optarg;
			ptr = thisarg (optarg);
			unitv[nunits - 1] = ptr;
			if (strstr (ptr, "'") == ptr)
				unitv[nunits - 1] = ptr + 1;
			while (ptr && (ptr = nextarg (ptr))) {
				if (nunits >= unitv_size) {
					unitv_size += 8;
					unitv = realloc (unitv, unitv_size);
					if (units == NULL)
						die (STATE_UNKNOWN, _("Could not realloc() units\n"));
				}
				nunits++;
				ptr = thisarg (ptr);
				if (strstr (ptr, "'") == ptr)
					unitv[nunits - 1] = ptr + 1;
				else
					unitv[nunits - 1] = ptr;
			}
			break;

		}
	}

	if (server_address == NULL)
		server_address = argv[optind];

	if (community == NULL)
		community = strdup (DEFAULT_COMMUNITY);
	


	return validate_arguments ();
}


/******************************************************************************

@@-
<sect3>
<title>validate_arguments</title>

<para>&PROTO_validate_arguments;</para>

<para>Checks to see if the default miblist needs to be loaded. Also verifies 
the authentication and authorization combinations based on protocol version 
selected.</para>

<para></para>

</sect3>
-@@
******************************************************************************/



int
validate_arguments ()
{
	/* check whether to load locally installed MIBS (CPU/disk intensive) */
	if (miblist == NULL) {
		if ( needmibs  == TRUE ) {
			miblist = strdup (DEFAULT_MIBLIST);
		}else{
			miblist = "''";			/* don't read any mib files for numeric oids */
		}
	}


	/* Need better checks to verify seclevel and authproto choices */
	
	if (seclevel == NULL) 
		asprintf (&seclevel, "noAuthNoPriv");


	if (authproto == NULL ) 
		asprintf(&authproto, DEFAULT_AUTH_PROTOCOL);
	
	 
	
	if (proto == NULL || (strcmp(proto,DEFAULT_PROTOCOL) == 0) ) {	/* default protocol version */
		asprintf(&proto, DEFAULT_PROTOCOL);
		asprintf(&authpriv, "%s%s", "-c ", community);
	}
	else if ( strcmp (proto, "2c") == 0 ) {		/* snmpv2c args */
		asprintf(&authpriv, "%s%s", "-c ", community);
	}
	else if ( strcmp (proto, "3") == 0 ) {		/* snmpv3 args */
		asprintf(&proto, "%s", "3");
		
		if ( (strcmp(seclevel, "noAuthNoPriv") == 0) || seclevel == NULL ) {
			asprintf(&authpriv, "%s", "-l noAuthNoPriv" );
		}
		else if ( strcmp(seclevel, "authNoPriv") == 0 ) {
			if ( secname == NULL || authpasswd == NULL) {
				printf (_("Missing secname (%s) or authpassword (%s) ! \n"),secname, authpasswd );
				print_usage ();
				exit (STATE_UNKNOWN);
			}
			asprintf(&authpriv, "-l authNoPriv -a %s -u %s -A %s ", authproto, secname, authpasswd);
		}
		else if ( strcmp(seclevel, "authPriv") == 0 ) {
			if ( secname == NULL || authpasswd == NULL || privpasswd == NULL ) {
				printf (_("Missing secname (%s), authpassword (%s), or privpasswd (%s)! \n"),secname, authpasswd,privpasswd );
				print_usage ();
				exit (STATE_UNKNOWN);
			}
			asprintf(&authpriv, "-l authPriv -a %s -u %s -A %s -x DES -X %s ", authproto, secname, authpasswd, privpasswd);
		}
		
	}
	else {
		usage2 (_("Invalid SNMP version"), proto);
	}
			
	return OK;
}



char *
clarify_message (char *msg)
{
	int i = 0;
	int foo;
	char tmpmsg_c[MAX_INPUT_BUFFER];
	char *tmpmsg = (char *) &tmpmsg_c;
	tmpmsg = strcpy (tmpmsg, msg);
	if (!strncmp (tmpmsg, " Hex:", 5)) {
		tmpmsg = strtok (tmpmsg, ":");
		while ((tmpmsg = strtok (NULL, " "))) {
			foo = strtol (tmpmsg, NULL, 16);
			/* Translate chars that are not the same value in the printers
			 * character set.
			 */
			switch (foo) {
			case 208:
				{
					foo = 197;
					break;
				}
			case 216:
				{
					foo = 196;
					break;
				}
			}
			msg[i] = foo;
			i++;
		}
		msg[i] = 0;
	}
	return (msg);
}



int
check_num (int i)
{
	int result;
	result = STATE_OK;
	if (eval_method[i] & WARN_GT && eval_method[i] & WARN_LT &&
			lower_warn_lim[i] > upper_warn_lim[i]) {
		if (response_value[i] <= lower_warn_lim[i] &&
				response_value[i] >= upper_warn_lim[i]) {
			result = STATE_WARNING;
		}
	}
	else if
		((eval_method[i] & WARN_GT && response_value[i] > upper_warn_lim[i]) ||
		 (eval_method[i] & WARN_GE && response_value[i] >= upper_warn_lim[i]) ||
		 (eval_method[i] & WARN_LT && response_value[i] < lower_warn_lim[i]) ||
		 (eval_method[i] & WARN_LE && response_value[i] <= lower_warn_lim[i]) ||
		 (eval_method[i] & WARN_EQ && response_value[i] == upper_warn_lim[i]) ||
		 (eval_method[i] & WARN_NE && response_value[i] != upper_warn_lim[i])) {
		result = STATE_WARNING;
	}

	if (eval_method[i] & CRIT_GT && eval_method[i] & CRIT_LT &&
			lower_crit_lim[i] > upper_crit_lim[i]) {
		if (response_value[i] <= lower_crit_lim[i] &&
				response_value[i] >= upper_crit_lim[i]) {
			result = STATE_CRITICAL;
		}
	}
	else if
		((eval_method[i] & CRIT_GT && response_value[i] > upper_crit_lim[i]) ||
		 (eval_method[i] & CRIT_GE && response_value[i] >= upper_crit_lim[i]) ||
		 (eval_method[i] & CRIT_LT && response_value[i] < lower_crit_lim[i]) ||
		 (eval_method[i] & CRIT_LE && response_value[i] <= lower_crit_lim[i]) ||
		 (eval_method[i] & CRIT_EQ && response_value[i] == upper_crit_lim[i]) ||
		 (eval_method[i] & CRIT_NE && response_value[i] != upper_crit_lim[i])) {
		result = STATE_CRITICAL;
	}

	return result;
}



int
llu_getll (unsigned long long *ll, char *str)
{
	char tmp[100];
	if (strchr (str, ':') == NULL)
		return 0;
	if (strchr (str, ',') != NULL && (strchr (str, ',') < strchr (str, ':')))
		return 0;
	if (sscanf (str, "%llu%[:]", ll, tmp) == 2)
		return 1;
	return 0;
}



int
llu_getul (unsigned long long *ul, char *str)
{
	char tmp[100];
	if (sscanf (str, "%llu%[^,]", ul, tmp) == 1)
		return 1;
	if (sscanf (str, ":%llu%[^,]", ul, tmp) == 1)
		return 1;
	if (sscanf (str, "%*u:%llu%[^,]", ul, tmp) == 1)
		return 1;
	return 0;
}



/* trim leading whitespace
	 if there is a leading quote, make sure it balances */

char *
thisarg (char *str)
{
	str += strspn (str, " \t\r\n");	/* trim any leading whitespace */
	if (strstr (str, "'") == str) {	/* handle SIMPLE quoted strings */
		if (strlen (str) == 1 || !strstr (str + 1, "'"))
			die (STATE_UNKNOWN, _("Unbalanced quotes\n"));
	}
	return str;
}



/* if there's a leading quote, advance to the trailing quote
	 set the trailing quote to '\x0'
	 if the string continues, advance beyond the comma */

char *
nextarg (char *str)
{
	if (strstr (str, "'") == str) {
		str[0] = 0;
		if (strlen (str) > 1) {
			str = strstr (str + 1, "'");
			return (++str);
		}
		else {
			return NULL;
		}
	}
	if (strstr (str, ",") == str) {
		str[0] = 0;
		if (strlen (str) > 1) {
			return (++str);
		}
		else {
			return NULL;
		}
	}
	if ((str = strstr (str, ",")) && strlen (str) > 1) {
		str[0] = 0;
		return (++str);
	}
	return NULL;
}



void
print_help (void)
{
	print_revision (progname, revision);

	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("Check status of remote machines and obtain sustem information via SNMP"));

  printf ("\n\n");

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_(UT_HOST_PORT), 'p', DEFAULT_PORT);

	/* SNMP and Authentication Protocol */
	printf (" %s\n", "-n, --next");
  printf ("    %s\n", _("Use SNMP GETNEXT instead of SNMP GET"));
  printf (" %s\n", "-P, --protocol=[1|2c|3]");
  printf ("    %s\n", _("SNMP protocol version"));
  printf (" %s\n", "-L, --seclevel=[noAuthNoPriv|authNoPriv|authPriv]");
  printf ("    %s\n", _("SNMPv3 securityLevel"));
  printf (" %s\n", "-a, --authproto=[MD5|SHA]");
  printf ("    %s\n", _("SNMPv3 auth proto"));

	/* Authentication Tokens*/
	printf (" %s\n", "-C, --community=STRING");
  printf ("    %s ", _("Optional community string for SNMP communication"));
  printf ("(%s \"%s\")\n", _("default is") ,DEFAULT_COMMUNITY);
  printf (" %s\n", "-U, --secname=USERNAME");
  printf ("    %s\n", _("SNMPv3 username"));
  printf (" %s\n", "-A, --authpassword=PASSWORD");
  printf ("    %s\n", _("SNMPv3 authentication password"));
  printf (" %s\n", "-X, --privpasswd=PASSWORD");
  printf ("    %s\n", _("SNMPv3 privacy password"));

	/* OID Stuff */
	printf (" %s\n", "-o, --oid=OID(s)");
  printf ("    %s\n", _("Object identifier(s) or SNMP variables whose value you wish to query"));
  printf (" %s\n", "-m, --miblist=STRING");
  printf ("    %s\n", _("List of MIBS to be loaded (default = none if using numeric oids or 'ALL'"));
  printf ("    %s\n", _("for symbolic oids.)"));
  printf (" %s\n", "-d, --delimiter=STRING");
  printf (_("    Delimiter to use when parsing returned data. Default is \"%s\""), DEFAULT_DELIMITER);
  printf ("    %s\n", _("Any data on the right hand side of the delimiter is considered"));
  printf ("    %s\n", _("to be the data that should be used in the evaluation."));

	/* Tests Against Integers */
	printf (" %s\n", "-w, --warning=INTEGER_RANGE(s)");
  printf ("    %s\n", _("Range(s) which will not result in a WARNING status"));
  printf (" %s\n", "-c, --critical=INTEGER_RANGE(s)");
  printf ("    %s\n", _("Range(s) which will not result in a CRITICAL status"));

	/* Tests Against Strings */
	printf (" %s\n", "-s, --string=STRING");
  printf ("    %s\n", _("Return OK state (for that OID) if STRING is an exact match"));
  printf (" %s\n", "-r, --ereg=REGEX");
  printf ("    %s\n", _("Return OK state (for that OID) if extended regular expression REGEX matches"));
  printf (" %s\n", "-R, --eregi=REGEX");
  printf ("    %s\n", _("Return OK state (for that OID) if case-insensitive extended REGEX matches"));
  printf (" %s\n", "-l, --label=STRING");
  printf ("    %s\n", _("Prefix label for output from plugin (default -s 'SNMP')"));

	/* Output Formatting */
	printf (" %s\n", "-u, --units=STRING");
  printf ("    %s\n", _("Units label(s) for output data (e.g., 'sec.')."));
  printf (" %s\n", "-D, --output-delimiter=STRING");
  printf ("    %s\n", _("Separates output on multiple OID requests"));

	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);

	printf (_(UT_VERBOSE));

	printf ("%s\n", _("This plugin uses the 'snmpget' command included with the NET-SNMP package."));
  printf ("%s\n", _("if you don't have the package installed, you will need to download it from"));
  printf ("%s\n", _("http://net-snmp.sourceforge.net before you can use this plugin."));

	printf ("%s\n", _("- Multiple OIDs may be indicated by a comma- or space-delimited list (lists with"));
  printf ("%s\n", _(" internal spaces must be quoted) [max 8 OIDs]"));

	printf ("%s\n", _("- Ranges are inclusive and are indicated with colons. When specified as"));
  printf ("%s\n", _(" 'min:max' a STATE_OK will be returned if the result is within the indicated"));
  printf ("%s\n", _(" range or is equal to the upper or lower bound. A non-OK state will be"));
  printf ("%s\n", _(" returned if the result is outside the specified range."));

	printf ("%s\n", _("- If specified in the order 'max:min' a non-OK state will be returned if the"));
  printf ("%s\n", _(" result is within the (inclusive) range."));

	printf ("%s\n", _("- Upper or lower bounds may be omitted to skip checking the respective limit."));
  printf ("%s\n", _("- Bare integers are interpreted as upper limits."));
  printf ("%s\n", _("- When checking multiple OIDs, separate ranges by commas like '-w 1:10,1:,:20'"));
  printf ("%s\n", _("- Note that only one string and one regex may be checked at present"));
  printf ("%s\n", _("- All evaluation methods other than PR, STR, and SUBSTR expect that the value"));
  printf ("%s\n", _(" returned from the SNMP query is an unsigned integer."));

	printf (_(UT_SUPPORT));
}



void
print_usage (void)
{
  printf (_("Usage:"));
	printf ("%s -H <ip_address> -o <OID> [-w warn_range] [-c crit_range]\n",progname);
  printf ("[-C community] [-s string] [-r regex] [-R regexi] [-t timeout] [-e retries]\n");
  printf ("[-l label] [-u units] [-p port-number] [-d delimiter] [-D output-delimiter]\n");
  printf ("[-m miblist] [-P snmp version] [-L seclevel] [-U secname] [-a authproto]\n");
  printf ("[-A authpasswd] [-X privpasswd]\n");
}
