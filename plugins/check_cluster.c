/*****************************************************************************
 *
 * CHECK_CLUSTER2.C - Host and Service Cluster Plugin for Nagios 2.x
 *
 * Copyright (c) 2000-2004 Ethan Galstad (nagios@nagios.org)
 * Copyright (c) 2007 nagios-plugins team
 * License: GPL
 * Last Modified: $Date: 2007-12-11 05:57:35 +0000 (Tue, 11 Dec 2007) $
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
 * $Id: check_cluster.c 1861 2007-12-11 05:57:35Z dermoth $
 * 
******************************************************************************/

const char *progname = "check_cluster";
const char *revision = "$Revision: 1861 $";
const char *copyright = "2007";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "utils.h"
#include "utils_base.h"

#define CHECK_SERVICES	1
#define CHECK_HOSTS	2

void print_help (void);
void print_usage (void);

int total_services_ok=0;
int total_services_warning=0;
int total_services_unknown=0;
int total_services_critical=0;

int total_hosts_up=0;
int total_hosts_down=0;
int total_hosts_unreachable=0;

char *warn_threshold;
char *crit_threshold;

int check_type=CHECK_SERVICES;

char *data_vals=NULL;
char *label=NULL;

int verbose=0;

int process_arguments(int,char **);



int main(int argc, char **argv){
	char *ptr;
	int data_val;
	int return_code=STATE_OK;
	thresholds *thresholds = NULL;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	if(process_arguments(argc,argv)==ERROR)
		usage(_("Could not parse arguments"));

	/* Initialize the thresholds */
	set_thresholds(&thresholds, warn_threshold, crit_threshold);
	if(verbose)
		print_thresholds("check_cluster", thresholds);

	/* check the data values */
	for(ptr=strtok(data_vals,",");ptr!=NULL;ptr=strtok(NULL,",")){

		data_val=atoi(ptr);

		if(check_type==CHECK_SERVICES){
			switch(data_val){
			case 0:
				total_services_ok++;
				break;
			case 1:
				total_services_warning++;
				break;
			case 2:
				total_services_critical++;
				break;
			case 3:
				total_services_unknown++;
				break;
			default:
				break;
		        }
	        }
		else{
			switch(data_val){
			case 0:
				total_hosts_up++;
				break;
			case 1:
				total_hosts_down++;
				break;
			case 2:
				total_hosts_unreachable++;
				break;
			default:
				break;
		        }
	        }
        }
	

	/* return the status of the cluster */
	if(check_type==CHECK_SERVICES){
		return_code=get_status(total_services_warning+total_services_unknown+total_services_critical, thresholds);
		printf("CLUSTER %s: %s: %d ok, %d warning, %d unknown, %d critical\n",
			state_text(return_code), (label==NULL)?"Service cluster":label,
			total_services_ok,total_services_warning,
			total_services_unknown,total_services_critical);
	}
	else{
		return_code=get_status(total_hosts_down+total_hosts_unreachable, thresholds);
		printf("CLUSTER %s: %s: %d up, %d down, %d unreachable\n",
			state_text(return_code), (label==NULL)?"Host cluster":label,
			total_hosts_up,total_hosts_down,total_hosts_unreachable);
	}

	return return_code;
}



int process_arguments(int argc, char **argv){
	int c;
	int option=0;
	static struct option longopts[]={ 
		{"data",     required_argument,0,'d'},
		{"warning",  required_argument,0,'w'},
		{"critical", required_argument,0,'c'},
		{"label",    required_argument,0,'l'},
		{"host",     no_argument,      0,'h'},
		{"service",  no_argument,      0,'s'},
		{"verbose",  no_argument,      0,'v'},
		{"version",  no_argument,      0,'V'},
		{"help",     no_argument,      0,'H'},
		{0,0,0,0}
	};

	/* no options were supplied */
	if(argc<2)
		return ERROR;

	while(1){

		c=getopt_long(argc,argv,"hHsvVw:c:d:l:",longopts,&option);

		if(c==-1 || c==EOF || c==1)
			break;

		switch(c){

		case 'h': /* host cluster */
			check_type=CHECK_HOSTS;
			break;

		case 's': /* service cluster */
			check_type=CHECK_SERVICES;
			break;

		case 'w': /* warning threshold */
			warn_threshold = strdup(optarg);
			break;

		case 'c': /* warning threshold */
			crit_threshold = strdup(optarg);
			break;

		case 'd': /* data values */
			data_vals=(char *)strdup(optarg);
			break;

		case 'l': /* text label */
			label=(char *)strdup(optarg);
			break;

		case 'v': /* verbose */
			verbose++;
			break;

		case 'V': /* version */
			print_revision (progname, revision);
			exit (STATE_OK);
			break;

		case 'H': /* help */
			print_help();
			exit(STATE_UNKNOWN);
			break;

		default:
			return ERROR;
			break;
	        }
	}

	if(data_vals==NULL)
		return ERROR;

	return OK;
}

void
print_help(void)
{
	print_revision(progname, revision);
	printf ("Copyright (c) 2000-2004 Ethan Galstad (nagios@nagios.org)\n");
	printf(COPYRIGHT, copyright, email);

	printf(_("Host/Service Cluster Plugin for Nagios 2"));
	printf("\n\n");

	print_usage();

	printf("\n");
	printf("%s\n", _("Options:"));
	printf (" %s\n", "-s, --service");
	printf ("    %s\n", _("Check service cluster status"));
	printf (" %s\n", "-h, --host");
	printf ("    %s\n", _("Check host cluster status"));
	printf (" %s\n", "-l, --label=STRING");
	printf ("    %s\n", _("Optional prepended text output (i.e. \"Host cluster\")"));
	printf (" %s\n", "-w, --warning=THRESHOLD");
	printf ("    %s\n", _("Specifies the range of hosts or services in cluster that must be in a"));
	printf ("    %s\n", _("non-OK state in order to return a WARNING status level"));
	printf (" %s\n", "-c, --critical=THRESHOLD");
	printf ("    %s\n", _("Specifies the range of hosts or services in cluster that must be in a"));
	printf ("    %s\n", _("non-OK state in order to return a CRITICAL status level"));
	printf (" %s\n", "-d, --data=LIST");
	printf ("    %s\n", _("The status codes of the hosts or services in the cluster, separated by"));
	printf ("    %s\n", _("commas"));

	printf(_(UT_VERBOSE));

	printf("\n");
	printf("%s\n", _("Notes:"));
	printf(" %s\n", _("See:"));
	printf(" %s\n", ("http://nagiosplug.sourceforge.net/developer-guidelines.html#THRESHOLDFORMAT"));
	printf(" %s\n", _("for THRESHOLD format and examples."));

	printf(_(UT_SUPPORT));
	printf("\n");
}


void
print_usage(void)
{

	printf(_("Usage:"));
	printf(" %s (-s | -h) -d val1[,val2,...,valn] [-l label]\n", progname);
	printf("[-w threshold] [-c threshold] [-v] [--help]\n");

}

