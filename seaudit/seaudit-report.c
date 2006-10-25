/**
 *  @file seaudit-report.c
 *  Command line tool for processing SELinux audit logs and generating
 *  a concise report containing standard information as well as
 *  customized information using seaudit views.  Reports are rendered
 *  in either HTML or plain text.  Future support will provide
 *  rendering into XML.  The HTML report can be formatted by providing
 *  an alternate stylesheet file or by configuring the default
 *  stylesheet.  This tool also provides the option for including
 *  malformed strings within the report.
 *
 *  @author Jeremy A. Mowery jmowery@tresys.com
 *  @author Jason Tang jtang@tresys.com
 *
 *  Copyright (C) 2004-2006 Tresys Technology, LLC
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <config.h>

#include "report.h"

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include <libxml/xmlreader.h>

#define COPYRIGHT_INFO "Copyright (C) 2004-2006 Tresys Technology, LLC"

static struct option const longopts[] = {
	{"html", no_argument, NULL, 'H'},
	{"malformed", no_argument, NULL, 'm'},
	{"output", required_argument, NULL, 'o'},
	{"stylesheet", required_argument, NULL, 'S'},
	{"stdin", no_argument, NULL, 's'},
	{"config", no_argument, NULL, 'c'},
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'v'},
	{NULL, 0, NULL, 0}
};

void seaudit_report_info_usage(const char *program_name, bool_t brief)
{
	printf("%s (seaudit-report ver. %s)\n\n", COPYRIGHT_INFO, VERSION);
	printf("\nDescription: Generate a customized SELinux log report.\n");
	printf("Usage: %s [OPTIONS] LOGFILES\n", program_name);
	if (brief) {
		printf("\n   Try %s --help for more help.\n\n", program_name);
		return;
	}
	printf("  -s, --stdin              Read LOGFILES from standard input.\n");
	printf("  -m, --malformed          Include malformed log messages.\n");
	printf("  -oFILE, --output=FILE    Output to file.\n");
	printf("  -cFILE, --config=FILE    Use alternate config file.\n");
	printf("  --html                   Set output format to HTML.  Default is plain text.\n");
	printf("  --stylesheet=FILE        HTML stylesheet for formatting HTML report.\n");
	printf("                           (Ignored if --html is not given.)\n");
	printf("  -v, --version            Display version information and exit.\n");
	printf("  -h, --help               Display this help and exit.\n");
	printf("\n");
	printf("Example stylesheet is at %s/%s.\n", APOL_INSTALL_DIR, STYLESHEET_FILE);
}

static void seaudit_report_parse_command_line_args(int argc, char **argv, seaudit_report_t *report_info) {
	int optc, i;

	/* get option arguments */
	while ((optc =
		getopt_long(argc, argv, "o:c:t:msvh", longopts, NULL)) != -1) {
		switch (optc) {
		case 0:
			break;
		case 'o':
			/* File to save output to */
			if (optarg != 0) {
				if (seaudit_report_add_outFile_path(optarg, report_info) != 0)
					goto err;
			}
			break;
		case 'c':
			/* Alternate config file path */
			if (optarg != 0) {
				if (seaudit_report_add_configFile_path(optarg, report_info) != 0)
					goto err;
			}
			break;
		case 'S':
			/* HTML style sheet file path */
			if (optarg != 0) {
				if (seaudit_report_add_stylesheet_path(optarg, report_info) != 0)
					goto err;
			}
			report_info->use_stylesheet = TRUE;
			break;
		case 'm':
			/* include malformed messages */
			report_info->malformed = TRUE;
			break;
		case 's':
			/* read LOGFILES from standard input */
			report_info->stdin = TRUE;
			break;
		case 'H':
			/* Set the output to format to html */
			report_info->html = TRUE;
			break;
		case 'v':
			/* display version */
			printf("\n%s (seaudit-report ver. %s)\n\n", COPYRIGHT_INFO,
					VERSION);
			seaudit_report_destroy(report_info);
			exit(0);
		case 'h':
			/* display help */
			seaudit_report_info_usage(argv[0], FALSE);
			seaudit_report_destroy(report_info);
			exit(0);
		default:
			/* display usage and handle error */
			seaudit_report_info_usage(argv[0], TRUE);
			goto err;
		}
	}

	/* Throw warning if a stylesheet was specified, but the --html option was not. */
	if (report_info->stylesheet_file != NULL && !report_info->html) {
		fprintf(stderr, "Warning: The --html option was not specified.\n");
		goto err;
	}

	/* Add required filenames */
	for (i = (argc - 1); i >= optind; i--) {
		if (seaudit_report_add_logfile_to_list(report_info, argv[i])) {
			fprintf(stderr, "Unable to add specified logfile file to data structure.\n");
			goto err;
		}
	}

	/* Ensure that logfiles were not specified in addition to the standard-in option */
	if ((report_info->num_logfiles > 0) && report_info->stdin) {
		fprintf(stderr,
			"Warning: Command line filename(s) will be ignored. Reading from stdin.\n");
	}

	if ((!report_info->stdin) && (report_info->num_logfiles == 0 || (argc == optind))) {
		/* display usage and handle error */
		seaudit_report_info_usage(argv[0], TRUE);
		goto err;
	}

	return;

err:
	seaudit_report_destroy(report_info);
	exit(-1);
}

int main (int argc, char **argv)
{
	seaudit_report_t *report_info;

	report_info = seaudit_report_create();
	if (!report_info) {
		return -1;
	}
	seaudit_report_parse_command_line_args(argc, argv, report_info);

	/* Load all audit messages into memory */
	if (seaudit_report_load_audit_messages_from_log_file(report_info) != 0)
		return -1;

	if (seaudit_report_generate_report(report_info) != 0) {
		seaudit_report_destroy(report_info);
		return -1;
	}
	seaudit_report_destroy(report_info);

	return 0;
}
