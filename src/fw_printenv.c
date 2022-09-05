/*
 * (C) Copyright 2019
 * Stefano Babic, DENX Software Engineering, sbabic@denx.de.
 *
 * SPDX-License-Identifier:     LGPL-2.1-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <dirent.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <stdbool.h>

#include <zconf.h>
#include "libuboot.h"

#ifndef VERSION
#define VERSION "0.1"
#endif

#define PROGRAM_SET	"fw_setenv"

static struct option long_options[] = {
	{"version", no_argument, NULL, 'V'},
	{"no-header", no_argument, NULL, 'n'},
	{"help", no_argument, NULL, 'h'},
	{"config", required_argument, NULL, 'c'},
	{"defenv", required_argument, NULL, 'f'},
	{"script", required_argument, NULL, 's'},
	{"verbose", no_argument, NULL, 'v'},		// MV 20220903
	{NULL, 0, NULL, 0}
};


//************************************************************************************************************
//
// Decode English three letter Month
//
//************************************************************************************************************
uint32_t
Month_to_Int(const char * Month)
{
	#define 	MonChars		3
	#define	Months		12
	char *     	MonthNames  = "DecNovOctSepAugJulJunMayAprMarFebJan";
	char *     	pMonths     = MonthNames;
	uint32_t 	iMonth      = Months;

	while( (strncmp (Month, pMonths, MonChars) != 0) && (iMonth > 0) ){
		iMonth--;            			// decrement to previous month
		pMonths += MonChars;        	// increment pointer in template
	}
	return iMonth;
}


//************************************************************************************************************
//
// English to Scientific Date Format YYYY_MM_DD
//
//************************************************************************************************************
uint32_t
English_to_ScientificDate(char * YYYY_MM_DD ,const char * EnglishDate)
// Assumes Input string formatted as "Jan 31 2018"
// Output string formatted as "2018-01-31". Assumes that buffer * YYYY_MM_DD has at least 10+1 char allocated.
// Brute force and not much checking - but this is the format CCS provides. 
// BTW - could have done this as compile time macro!!
// Or at least parse by searching for '\32' as separator. Also - locale dependent!
//
{
    uint32_t success     = 0;
    if (strlen(EnglishDate) == 11){
        success     = Month_to_Int(EnglishDate);
    }
    if (success) {
        strncpy (YYYY_MM_DD, EnglishDate + 7, 4);
        YYYY_MM_DD +=4;
        snprintf(YYYY_MM_DD, 9, "-%02d-", success);    // Print "-MM-" formatted Month
        YYYY_MM_DD +=4;
        strncpy (YYYY_MM_DD, EnglishDate + 4, 2);
        if (*(YYYY_MM_DD) == ' '){
            *(YYYY_MM_DD) = '0';                         // leading zero
        }
        YYYY_MM_DD +=2;
        *YYYY_MM_DD ='\0';                              // terminate string
    } else {
        strcpy( YYYY_MM_DD, EnglishDate);
    }
    return success;                                    // actually Month 9n case os a success ..
}


static void usage(char *program, bool setprogram)
{
//	fprintf(stdout, "%s (compiled %s)\n", program, __DATE__);

#define TXT_BUFF_LEN 64
	char		buffer[TXT_BUFF_LEN];
	English_to_ScientificDate(buffer ,__DATE__);
	strncat(buffer," ", TXT_BUFF_LEN-strlen(buffer));    //  strcat(p_txBuffer,__TIME__);
	strncat(buffer,__TIME__, TXT_BUFF_LEN-strlen(buffer));    //  strcat(p_txBuffer,__TIME__);
	fprintf(stdout, "%s (Compiled %s)\n", program, buffer);
	
	fprintf(stdout, "Usage %s [OPTION]\n",
			program);
	fprintf(stdout,
		" -h, --help                       : print this help\n"
		" -c, --config <filename>          : configuration file (old fw_env.config)\n"
		" -f, --defenv <filename>          : default environment if no one found\n"
		" -V, --version                    : print version and exit\n"
		" -v, --verbose                    : add debugging information\n"
	);
	if (!setprogram)
		fprintf(stdout,
		" -n, --no-header                  : do not print variable name\n"
		);
	else
		fprintf(stdout,
		" -s, --script <filename>          : read variables to be set from a script\n"
		"\n"
		"Script Syntax:\n"
		" key=value\n"
		" lines starting with '#' are treated as comment\n"
		" lines without '=' are ignored\n"
		"\n"
		"Script Example:\n"
		" netdev=eth0\n"
		" kernel_addr=400000\n"
		" foo=empty empty empty    empty empty empty\n"
		" bar\n"
		"\n"
		);
}


	
int main (int argc, char **argv) {
	struct uboot_ctx *ctx;
	char *options = "Vc:f:s:nhv";
	char *cfgfname = NULL;
	char *defenvfile = NULL;
	char *scriptfile = NULL;
	int c, i;
	int ret = 0;
	void *tmp;
	const char *name, *value;
	char *progname;
	bool is_setenv = false;
	bool noheader = false;
	bool default_used = false;
	bool verbose = false;

	/*
	 * As old tool, there is just a tool with symbolic link
	 */
	 

	progname = strrchr(argv[0], '/');
	if (!progname)
		progname = argv[0];
	else
		progname++;

	if (!strcmp(progname, PROGRAM_SET))
		is_setenv = true;

	while ((c = getopt_long(argc, argv, options,
				long_options, NULL)) != EOF) {
		switch (c) {
		case 'c':
			cfgfname = strdup(optarg);
			break;
		case 'n':
			noheader = true;
			break;
		case 'V':
			fprintf(stdout, "%s\n", VERSION);
			exit(0);
		case 'h':
			usage(progname, is_setenv);
			exit(0);
		case 'f':
			defenvfile = strdup(optarg);
			break;
		case 's':
			scriptfile = strdup(optarg);
			break;
		case 'v':
			verbose = true;
			break;
		default:
			fprintf(stdout, " Error: Unknown parameter\n", VERSION);
			exit(1); 
		}
	}

	// Verify if executed with root privileges
	c=getuid();		// euid is effective user id and uid is user id 
				// both euid and uid are zero when you are root user 
	if (c!=0){
		fprintf(stdout," Error: Please run the script as root user !\n");
		exit(1);
	}
	
	argc -= optind;
	argv += optind;

	if (libuboot_initialize(&ctx, NULL) < 0) {
		fprintf(stderr, "Cannot initialize environment\n");
		exit(1);
	}

	if (verbose)
		libuboot_set_verbose(ctx);

	if (!cfgfname)
		cfgfname = "/etc/fw_env.config";

	if ((ret = libuboot_read_config(ctx, cfgfname)) < 0) {
		fprintf(stderr, "Configuration file %s not found or not referring to proper structure\n");	// TODO
		exit (ret);
	}

	if (!defenvfile)
		defenvfile = "/etc/u-boot-initial-env";

	if ((ret = libuboot_open(ctx)) < 0) {
		fprintf(stderr, " Error: Cannot read environment, using default. Err= %d\n",ret );
		if ((ret = libuboot_load_file(ctx, defenvfile)) < 0) {
			fprintf(stderr, " Error: Cannot read default environment from file. Err= %d\n",ret );
			exit (ret);
		}
		default_used = true;
	}

	if (!is_setenv) {
		/* No variable given, print all environment */
		if (!argc) {
			tmp = NULL;
			while ((tmp = libuboot_iterator(ctx, tmp)) != NULL) {
				name = libuboot_getname(tmp);
				value = libuboot_getvalue(tmp);
				fprintf(stdout, "%s=%s\n", name, value);
			}
		} else {
			for (i = 0; i < argc; i++) {
				value = libuboot_get_env(ctx, argv[i]);
				if (noheader)
					fprintf(stdout, "%s\n", value ? value : "");
				else
					fprintf(stdout, "%s=%s\n", argv[i], value ? value : "");
			}
		}
	} else { /* setenv branch */
		bool need_store = false;
		if (scriptfile) {
			libuboot_load_file(ctx, scriptfile);
			need_store = true;
		} else {
			for (i = 0; i < argc; i += 2) {
				value = libuboot_get_env(ctx, argv[i]);
				if (i + 1 == argc) {
					if (value != NULL) {
						int ret;

						ret = libuboot_set_env(ctx, argv[i], NULL);
						if (ret) {
							fprintf(stderr, "libuboot_set_env failed: %d\n", ret);
							exit(-ret);
						}

						need_store = true;
					}
				} else {
					if (value == NULL || strcmp(value, argv[i+1]) != 0) {
						int ret;

						ret = libuboot_set_env(ctx, argv[i], argv[i+1]);
						if (ret) {
							fprintf(stderr, "libuboot_set_env failed: %d\n", ret);
							exit(-ret);
						}

						need_store = true;
					}
				}
			}
		}

		if (need_store || default_used) {
			ret = libuboot_env_store(ctx);
			if (ret)
				fprintf(stderr, "Error storing the env\n");
		}
	}

	libuboot_close(ctx);
	libuboot_exit(ctx);

	return ret;
}
