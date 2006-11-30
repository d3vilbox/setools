/**
 *  @file seaudit.c
 *  Main driver for the seaudit application.  This file also
 *  implements the main class seaudit_t.
 *
 *  @author Jeremy A. Mowery jmowery@tresys.com
 *  @author Jason Tang jtang@tresys.com
 *
 *  Copyright (C) 2003-2007 Tresys Technology, LLC
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

#include "seaudit.h"
#include "toplevel.h"

#include <apol/util.h>
#include <seaudit/model.h>
#include <seaudit/util.h>

#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <glade/glade.h>
#include <glib.h>
#include <gtk/gtk.h>

struct seaudit
{
	preferences_t *prefs;
	apol_policy_t *policy;
	char *policy_path;
	seaudit_log_t *log;
	char *log_path;
	size_t num_log_messages;
	struct tm *first, *last;
	toplevel_t *top;
};

static struct option const opts[] = {
	{"log", required_argument, NULL, 'l'},
	{"policy", required_argument, NULL, 'p'},
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'v'},
	{NULL, 0, NULL, 0}
};

preferences_t *seaudit_get_prefs(seaudit_t * s)
{
	return s->prefs;
}

apol_policy_t *seaudit_get_policy(seaudit_t * s)
{
	return s->policy;
}

char *seaudit_get_policy_path(seaudit_t * s)
{
	return s->policy_path;
}

void seaudit_set_log(seaudit_t * s, seaudit_log_t * log, const char *filename)
{
	seaudit_log_destroy(&s->log);
	if ((s->log = log) != NULL) {
		seaudit_model_t *model = NULL;
		apol_vector_t *messages = NULL;
		char *t = NULL;
		seaudit_message_t *message;
		if ((model = seaudit_model_create(NULL, s->log)) == NULL ||
		    (messages = seaudit_model_get_messages(s->log, model)) == NULL ||
		    (t = strdup(filename)) == NULL || preferences_add_recent_log(s->prefs, filename) < 0) {
			toplevel_ERR(s->top, "%s", strerror(errno));
			seaudit_model_destroy(&model);
			apol_vector_destroy(&messages, NULL);
			free(t);
			return;
		}
		/* do it in this order, for filename could be pointing to
		 * s->log_path */
		free(s->log_path);
		s->log_path = t;
		s->num_log_messages = apol_vector_get_size(messages);
		message = apol_vector_get_element(messages, 0);
		s->first = seaudit_message_get_time(message);
		message = apol_vector_get_element(messages, s->num_log_messages - 1);
		s->last = seaudit_message_get_time(message);
		seaudit_model_destroy(&model);
		apol_vector_destroy(&messages, NULL);
	} else {
		free(s->log_path);
		s->log_path = NULL;
		s->num_log_messages = 0;
		s->first = s->last = NULL;
	}
}

seaudit_log_t *seaudit_get_log(seaudit_t * s)
{
	return s->log;
}

char *seaudit_get_log_path(seaudit_t * s)
{
	return s->log_path;
}

size_t seaudit_get_num_log_messages(seaudit_t * s)
{
	return s->num_log_messages;
}

struct tm *seaudit_get_log_first(seaudit_t * s)
{
	return s->first;
}

struct tm *seaudit_get_log_last(seaudit_t * s)
{
	return s->last;
}

static seaudit_t *seaudit_create(preferences_t * prefs)
{
	seaudit_t *s = calloc(1, sizeof(*s));
	if (s != NULL) {
		s->prefs = prefs;
	}
	return s;
}

static void seaudit_destroy(seaudit_t ** s)
{
	if (s != NULL && *s != NULL) {
		apol_policy_destroy(&(*s)->policy);
		seaudit_log_destroy(&(*s)->log);
		preferences_destroy(&(*s)->prefs);
		toplevel_destroy(&(*s)->top);
		free((*s)->policy_path);
		free((*s)->log_path);
		free(*s);
		*s = NULL;
	}
}

static void print_version_info(void)
{
	printf("Audit Log analysis tool for Security Enhanced Linux\n\n");
	printf("   GUI version %s\n", VERSION);
	printf("   libapol version %s\n", libapol_get_version());
	printf("   libseaudit version %s\n\n", libseaudit_get_version());
}

static void print_usage_info(const char *program_name, int brief)
{
	printf("Usage:%s [options]\n", program_name);
	if (brief) {
		printf("\tTry %s --help for more help.\n", program_name);
		return;
	}
	printf("Audit Log analysis tool for Security Enhanced Linux\n\n");
	printf("   -l FILE, --log FILE     open log file named FILE\n");
	printf("   -p FILE, --policy FILE  open policy file named FILE\n");
	printf("   -h, --help              display this help and exit\n");
	printf("   -v, --version           display version information\n\n");
}

static void seaudit_parse_command_line(seaudit_t * seaudit, int argc, char **argv, char **log, char **policy)
{
	int optc;
	*log = NULL;
	*policy = NULL;
	while ((optc = getopt_long(argc, argv, "l:p:hv", opts, NULL)) != -1) {
		switch (optc) {
		case 'l':{
				*log = optarg;
				break;
			}
		case 'p':{
				*policy = optarg;
				break;
			}
		case 'h':{
				print_usage_info(argv[0], 0);
				seaudit_destroy(&seaudit);
				exit(EXIT_SUCCESS);
			}
		case 'v':{
				print_version_info();
				seaudit_destroy(&seaudit);
				exit(EXIT_SUCCESS);
			}
		case '?':
		default:{
				/* unrecognized argument give full usage */
				print_usage_info(argv[0], 0);
				seaudit_destroy(&seaudit);
				exit(EXIT_FAILURE);
			}
		}
	}
	if (optind < argc) {	       /* trailing non-options */
		print_usage_info(argv[0], 0);
		seaudit_destroy(&seaudit);
		exit(EXIT_FAILURE);
	}
	if (*log == NULL) {
		*log = preferences_get_log(seaudit->prefs);
	}
	if (*policy == NULL) {
		*policy = preferences_get_policy(seaudit->prefs);
	}
}

/*
 * We don't want to do the heavy work of loading and displaying the
 * log and policy before the main loop has started because it will
 * freeze the gui for too long.  To solve this, the function is called
 * from an idle callback set-up in main.
 */
struct delay_file_data
{
	toplevel_t *top;
	char *log_filename;
	char *policy_filename;
};

static gboolean delayed_main(gpointer data)
{
	struct delay_file_data *dfd = (struct delay_file_data *)data;
	if (dfd->log_filename != NULL && strcmp(dfd->log_filename, "") != 0) {
		toplevel_open_log(dfd->top, dfd->log_filename);
	}
	if (dfd->policy_filename != NULL && strcmp(dfd->policy_filename, "") != 0) {
		/*          toplevel_open_policy(dfd->top, dfd->log_filename); */
	}
	return FALSE;
}

int main(int argc, char **argv)
{
	preferences_t *prefs;
	seaudit_t *app;
	char *log, *policy;
	struct delay_file_data file_data;

	gtk_init(&argc, &argv);
	glade_init();
	if (!g_thread_supported())
		g_thread_init(NULL);
	if ((prefs = preferences_create()) == NULL) {
		ERR(NULL, "%s", strerror(ENOMEM));
		exit(EXIT_FAILURE);
	}
	if ((app = seaudit_create(prefs)) == NULL) {
		ERR(NULL, "%s", strerror(ENOMEM));
		exit(EXIT_FAILURE);
	}
	seaudit_parse_command_line(app, argc, argv, &log, &policy);
	if ((app->top = toplevel_create(app)) == NULL) {
		ERR(NULL, "%s", strerror(ENOMEM));
		seaudit_destroy(&app);
		exit(EXIT_FAILURE);
	}
	file_data.top = app->top;
	file_data.log_filename = log;
	file_data.policy_filename = policy;
	g_idle_add(&delayed_main, &file_data);
	gtk_main();
	if (preferences_write_to_conf_file(app->prefs) < 0) {
		ERR(NULL, "%s", strerror(ENOMEM));
	}
	seaudit_destroy(&app);
	exit(EXIT_SUCCESS);
}

#if 0

#include "auditlogmodel.h"
#include "filter_window.h"
#include "preferences.h"
#include "query_window.h"
#include "report_window.h"
#include "seaudit.h"
#include "seaudit_callback.h"
#include "utilgui.h"

#include <seaudit/log.h>
#include <seaudit/parse.h>

#include <stdio.h>
#include <string.h>

#include <gdk-pixbuf/gdk-pixbuf.h>

static void seaudit_set_real_time_log_button_state(bool_t state);
static int seaudit_read_policy_conf(const char *fname);
static void seaudit_print_version_info(void);
static void seaudit_print_usage_info(const char *program_name, bool_t brief);
static void seaudit_parse_command_line(int argc, char **argv, GString ** policy_filename, GString ** log_filename);
static void seaudit_update_title_bar(void *user_data);
static void seaudit_set_recent_logs_submenu(seaudit_conf_t * conf_file);
static void seaudit_set_recent_policys_submenu(seaudit_conf_t * conf_file);
static void seaudit_policy_file_open_from_recent_menu(GtkWidget * widget, gpointer user_data);
static void seaudit_log_file_open_from_recent_menu(GtkWidget * widget, gpointer user_data);
static gboolean seaudit_real_time_update_log(gpointer callback_data);
static void seaudit_exit_app(void);

/* this is just a public method to set this button */
void seaudit_view_entire_selection_update_sensitive(bool_t disable)
{
	seaudit_widget_update_sensitive("view_entire_message1", disable);
}

void seaudit_update_status_bar(seaudit_t * seaudit)
{
	char str[STR_SIZE];
	char *ver_str = NULL, *tmp = NULL;
	int num_log_msgs, num_filtered_log_msgs;
	char old_time[TIME_SIZE], recent_time[TIME_SIZE];
	seaudit_filtered_view_t *view;

	if (!seaudit || !seaudit->window || !seaudit->window->xml)
		return;

	GtkLabel *v_status_bar = (GtkLabel *) glade_xml_get_widget(seaudit->window->xml, "PolicyVersionLabel");
	GtkLabel *l_status_bar = (GtkLabel *) glade_xml_get_widget(seaudit->window->xml, "LogNumLabel");
	GtkLabel *d_status_bar = (GtkLabel *) glade_xml_get_widget(seaudit->window->xml, "LogDateLabel");

	if (seaudit->cur_policy == NULL) {
		ver_str = "Policy Version: No policy";
		gtk_label_set_text(v_status_bar, ver_str);
		seaudit_policy_menu_items_update(TRUE);
	} else {
		snprintf(str, STR_SIZE, "Policy Version: %s", apol_policy_get_version_type_mls_str(seaudit->cur_policy));
		free(tmp);
		gtk_label_set_text(v_status_bar, str);
		seaudit_policy_menu_items_update(FALSE);
	}

	if (seaudit->cur_log == NULL) {
		snprintf(str, STR_SIZE, "Log Messages: No log");
		gtk_label_set_text(l_status_bar, str);
		snprintf(str, STR_SIZE, "Dates: No log");
		gtk_label_set_text(d_status_bar, str);
		seaudit_log_menu_items_update(TRUE);
		seaudit_view_entire_selection_update_sensitive(TRUE);
	} else {
		view = seaudit_window_get_current_view(seaudit->window);
		if (view)
			num_filtered_log_msgs = view->store->log_view->num_fltr_msgs;
		else
			num_filtered_log_msgs = 0;

		num_log_msgs = apol_vector_get_size(seaudit->cur_log->msg_list);
		snprintf(str, STR_SIZE, "Log Messages: %d/%d", num_filtered_log_msgs, num_log_msgs);
		gtk_label_set_text(l_status_bar, str);
		if (num_log_msgs > 0) {
			msg_t *msg;
			msg = apol_vector_get_element(seaudit->cur_log->msg_list, 0);
			strftime(old_time, TIME_SIZE, "%b %d %H:%M:%S", msg->date_stamp);
			msg = apol_vector_get_element(seaudit->cur_log->msg_list, num_log_msgs - 1);
			strftime(recent_time, TIME_SIZE, "%b %d %H:%M:%S", msg->date_stamp);
			snprintf(str, STR_SIZE, "Dates: %s - %s", old_time, recent_time);
			gtk_label_set_text(d_status_bar, str);
		} else {
			snprintf(str, STR_SIZE, "Dates: No messages");
			gtk_label_set_text(d_status_bar, str);
		}
		seaudit_log_menu_items_update(FALSE);
		seaudit_view_entire_selection_update_sensitive(TRUE);
	}
}

int seaudit_open_policy(seaudit_t * seaudit, const char *filename)
{
	FILE *file;
	apol_policy_t *tmp_policy = NULL;
	int rt;
	const int SEAUDIT_STR_SZ = 128;
	GString *msg;
	GtkWidget *dialog;
	gint response;

	if (filename == NULL)
		return -1;

	show_wait_cursor(GTK_WIDGET(seaudit->window->window));
	if (seaudit->cur_policy != NULL) {
		dialog = gtk_message_dialog_new(seaudit->window->window,
						GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
						GTK_MESSAGE_WARNING,
						GTK_BUTTONS_YES_NO,
						"Opening a new policy will close all \"Query Policy\" windows\n"
						"Do you wish to continue anyway?");
		g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(get_dialog_response), &response);
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		if (response != GTK_RESPONSE_YES) {
			clear_wait_cursor(GTK_WIDGET(seaudit->window->window));
			return 0;
		}
	}
	if (g_file_test(filename, G_FILE_TEST_IS_DIR)) {
		msg = g_string_new("Error opening file: File is a directory!\n");
		message_display(seaudit->window->window, GTK_MESSAGE_ERROR, msg->str);
		g_string_free(msg, TRUE);
		clear_wait_cursor(GTK_WIDGET(seaudit->window->window));
		return -1;
	}

	file = fopen(filename, "r");
	if (!file) {
		msg = g_string_new("Error opening file: ");
		if (strlen(filename) > SEAUDIT_STR_SZ) {
			char *tmp = NULL;
			tmp = g_strndup(filename, SEAUDIT_STR_SZ);
			g_string_append(msg, tmp);
			g_string_append(msg, "...");
			g_free(tmp);
		} else {
			g_string_append(msg, filename);
		}
		g_string_append(msg, "!\n");
		g_string_append(msg, strerror(errno));
		message_display(seaudit->window->window, GTK_MESSAGE_ERROR, msg->str);
		g_string_free(msg, TRUE);
		clear_wait_cursor(GTK_WIDGET(seaudit->window->window));
		return -1;
	} else
		fclose(file);

	rt = apol_policy_open(filename, &tmp_policy, NULL, NULL);
	if (rt != 0) {
		apol_policy_destroy(&tmp_policy);
		msg = g_string_new("");
		g_string_append(msg, "The specified file does not appear to be a valid\nSE Linux Policy\n\n");
		message_display(seaudit->window->window, GTK_MESSAGE_ERROR, msg->str);
		clear_wait_cursor(GTK_WIDGET(seaudit->window->window));
		return -1;
	}
	apol_policy_destroy(&seaudit->cur_policy);
	seaudit->cur_policy = tmp_policy;

	g_string_assign(seaudit->policy_file, filename);
	if (!apol_policy_is_binary(seaudit_app->cur_policy)) {
		seaudit_read_policy_conf(filename);
	}
	policy_load_signal_emit();

	add_path_to_recent_policy_files(filename, &(seaudit->seaudit_conf));
	seaudit_set_recent_policys_submenu(&(seaudit->seaudit_conf));
	save_seaudit_conf_file(&(seaudit->seaudit_conf));
	clear_wait_cursor(GTK_WIDGET(seaudit->window->window));
	return 0;
}

static int seaudit_export_selected_msgs_to_file(const audit_log_view_t * log_view, const char *filename)
{
	msg_t *message = NULL;
	audit_log_t *audit_log = NULL;
	char *message_header = NULL;
	GtkTreeSelection *sel = NULL;
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	int fltr_msg_idx, msg_list_idx;
	GList *glist, *item = NULL;
	GtkTreePath *path = NULL;
	seaudit_filtered_view_t *view = NULL;
	FILE *log_file = NULL;

	assert(log_view != NULL && filename != NULL && (strlen(filename) < PATH_MAX));

	view = seaudit_window_get_current_view(seaudit_app->window);
	audit_log = log_view->my_log;
	sel = gtk_tree_view_get_selection(view->tree_view);
	glist = gtk_tree_selection_get_selected_rows(sel, &model);
	if (!glist) {
		message_display(seaudit_app->window->window, GTK_MESSAGE_ERROR, "You must select messages to export.");
		return -1;
	} else {
		log_file = fopen(filename, "w+");
		if (log_file == NULL) {
			message_display(seaudit_app->window->window,
					GTK_MESSAGE_WARNING, "Error: Could not open file for writing!");
			return -1;
		}

		for (item = glist; item != NULL; item = g_list_next(item)) {
			path = item->data;
			assert(path != NULL);
			if (gtk_tree_model_get_iter(model, &iter, path) == 0) {
				fprintf(stderr, "Could not get valid iterator for the selected path.\n");
				g_list_foreach(glist, (GFunc) gtk_tree_path_free, NULL);
				g_list_free(glist);
				fclose(log_file);
				return -1;
			}
			fltr_msg_idx = seaudit_log_view_store_iter_to_idx((SEAuditLogViewStore *) model, &iter);
			msg_list_idx = fltr_msg_idx;
			message = apol_vector_get_element(audit_log->msg_list, msg_list_idx);

			message_header = (char *)malloc((TIME_SIZE + STR_SIZE) * sizeof(char));
			if (message_header == NULL) {
				fprintf(stderr, "memory error\n");
				fclose(log_file);
				g_list_foreach(glist, (GFunc) gtk_tree_path_free, NULL);
				g_list_free(glist);
				return -1;
			}

			generate_message_header(message_header, audit_log, message->date_stamp,
						(char *)audit_log_get_host(audit_log, message->host));
			if (message->msg_type == AVC_MSG)
				write_avc_message_to_file(log_file, message->msg_data.avc_msg, message_header, audit_log);
			else if (message->msg_type == LOAD_POLICY_MSG)
				write_load_policy_message_to_file(log_file, message->msg_data.load_policy_msg, message_header);
			else if (message->msg_type == BOOLEAN_MSG)
				write_boolean_message_to_file(log_file, message->msg_data.boolean_msg, message_header, audit_log);
		}
		fclose(log_file);
		if (message_header)
			free(message_header);
	}

	g_list_foreach(glist, (GFunc) gtk_tree_path_free, NULL);
	g_list_free(glist);

	return 0;
}

void seaudit_save_log_file(bool_t selected_only)
{
	GtkWidget *file_selector, *confirmation;
	gint response, confirm;
	const gchar *filename;

	if (selected_only) {
		file_selector = gtk_file_selection_new("Export Selected Messages");
	} else {
		file_selector = gtk_file_selection_new("Export View");
	}
	/* set up transient so that it will center on the main window */
	gtk_window_set_transient_for(GTK_WINDOW(file_selector), seaudit_app->window->window);
	gtk_file_selection_complete(GTK_FILE_SELECTION(file_selector), (*(seaudit_app->audit_log_file)).str);

	g_signal_connect(GTK_OBJECT(file_selector), "response", G_CALLBACK(get_dialog_response), &response);

	while (1) {
		gtk_dialog_run(GTK_DIALOG(file_selector));

		if (response != GTK_RESPONSE_OK) {
			gtk_widget_destroy(file_selector);
			return;
		}

		filename = gtk_file_selection_get_filename(GTK_FILE_SELECTION(file_selector));

		if (!g_file_test(filename, G_FILE_TEST_IS_DIR) && strcmp(filename, DEFAULT_LOG) != 0) {
			if (g_file_test(filename, G_FILE_TEST_EXISTS)) {
				confirmation = gtk_message_dialog_new(seaudit_app->window->window,
								      GTK_DIALOG_DESTROY_WITH_PARENT,
								      GTK_MESSAGE_QUESTION,
								      GTK_BUTTONS_YES_NO,
								      "Overwrite existing file: %s ?", filename);

				confirm = gtk_dialog_run(GTK_DIALOG(confirmation));
				gtk_widget_destroy(confirmation);

				if (confirm == GTK_RESPONSE_YES)
					break;
			} else
				break;
		} else if (strcmp(filename, DEFAULT_LOG) == 0)
			message_display(seaudit_app->window->window,
					GTK_MESSAGE_WARNING, "Cannot overwrite the system default log file!");

		if (g_file_test(filename, G_FILE_TEST_IS_DIR))
			gtk_file_selection_complete(GTK_FILE_SELECTION(file_selector), filename);
	}

	gtk_widget_destroy(file_selector);

	if (selected_only) {
		seaudit_export_selected_msgs_to_file(seaudit_get_current_audit_log_view(), filename);
	} else {
		seaudit_write_log_file(seaudit_get_current_audit_log_view(), filename);
	}

	return;
}

int seaudit_write_log_file(const audit_log_view_t * log_view, const char *filename)
{
	int i;
	FILE *log_file;
	msg_t *message;
	audit_log_t *audit_log;
	char *message_header;

	assert(log_view != NULL && filename != NULL && (strlen(filename) < PATH_MAX));

	audit_log = log_view->my_log;
	log_file = fopen(filename, "w+");
	if (log_file == NULL) {
		message_display(seaudit_app->window->window, GTK_MESSAGE_WARNING, "Error: Could not open file for writing!");
		return -1;
	}

	message_header = (char *)malloc((TIME_SIZE + STR_SIZE) * sizeof(char));
	if (message_header == NULL) {
		fclose(log_file);
		fprintf(stderr, "memory error\n");
		return -1;
	}

	for (i = 0; i < apol_vector_get_size(audit_log->msg_list); i++) {
		message = apol_vector_get_element(audit_log->msg_list, i);
		/* If the multifilter member is NULL, then there are no
		 * filters for this view, so all messages are ok for writing to file. */
		if (log_view->multifilter == NULL ||
		    seaudit_multifilter_should_message_show(log_view->multifilter, message, audit_log)) {
			generate_message_header(message_header, audit_log, message->date_stamp,
						(char *)audit_log_get_host(audit_log, message->host));

			if (message->msg_type == AVC_MSG)
				write_avc_message_to_file(log_file, message->msg_data.avc_msg, message_header, audit_log);
			else if (message->msg_type == LOAD_POLICY_MSG)
				write_load_policy_message_to_file(log_file, message->msg_data.load_policy_msg, message_header);
			else if (message->msg_type == BOOLEAN_MSG)
				write_boolean_message_to_file(log_file, message->msg_data.boolean_msg, message_header, audit_log);
		}
	}

	fclose(log_file);

	if (message_header)
		free(message_header);

	return 0;
}

static void
write_avc_message_to_gtk_text_buf(GtkTextBuffer * buffer, const avc_msg_t * message, const char *message_header,
				  audit_log_t * audit_log)
{
	int i;
	GString *str;

	assert(buffer != NULL && message != NULL && message_header != NULL && audit_log != NULL);
	str = g_string_new("");
	if (str == NULL) {
		fprintf(stderr, "Unable to create string buffer.\n");
		return;
	}
	g_string_append_printf(str, "%s", message_header);
	if (!(message->tm_stmp_sec == 0 && message->tm_stmp_nano == 0 && message->serial == 0))
		g_string_append_printf(str, "audit(%lu.%03lu:%u): ", message->tm_stmp_sec, message->tm_stmp_nano, message->serial);

	g_string_append_printf(str, "avc:  %s  {", ((message->msg == AVC_GRANTED) ? "granted" : "denied"));

	for (i = 0; i < apol_vector_get_size(message->perms); i++)
		g_string_append_printf(str, " %s", (char *)apol_vector_get_element(message->perms, i));

	g_string_append_printf(str, " } for ");

	if (message->is_pid)
		g_string_append_printf(str, " pid=%i", message->pid);

	if (message->exe)
		g_string_append_printf(str, " exe=%s", message->exe);

	if (message->comm)
		g_string_append_printf(str, " comm=%s", message->comm);

	if (message->path)
		g_string_append_printf(str, " path=%s", message->path);

	if (message->name)
		g_string_append_printf(str, " name=%s", message->name);

	if (message->dev)
		g_string_append_printf(str, " dev=%s", message->dev);

	if (message->is_inode)
		g_string_append_printf(str, " ino=%li", message->inode);

	if (message->ipaddr)
		g_string_append_printf(str, " ipaddr=%s", message->ipaddr);

	if (message->saddr)
		g_string_append_printf(str, " saddr=%s", message->saddr);

	if (message->source != 0)
		g_string_append_printf(str, " src=%i", message->source);

	if (message->daddr)
		g_string_append_printf(str, " daddr=%s", message->daddr);

	if (message->dest != 0)
		g_string_append_printf(str, " dest=%i", message->dest);

	if (message->netif)
		g_string_append_printf(str, " netif=%s", message->netif);

	if (message->laddr)
		g_string_append_printf(str, " laddr=%s", message->laddr);

	if (message->lport != 0)
		g_string_append_printf(str, " lport=%i", message->lport);

	if (message->faddr)
		g_string_append_printf(str, " faddr=%s", message->faddr);

	if (message->fport != 0)
		g_string_append_printf(str, " fport=%i", message->fport);

	if (message->port != 0)
		g_string_append_printf(str, " port=%i", message->port);

	if (message->is_src_sid)
		g_string_append_printf(str, " ssid=%i", message->src_sid);

	if (message->is_tgt_sid)
		g_string_append_printf(str, " tsid=%i", message->tgt_sid);

	if (message->is_capability)
		g_string_append_printf(str, " capability=%i", message->capability);

	if (message->is_key)
		g_string_append_printf(str, " key=%i", message->key);

	if (message->is_src_con)
		g_string_append_printf(str, " scontext=%s:%s:%s",
				       audit_log_get_user(audit_log, message->src_user),
				       audit_log_get_role(audit_log, message->src_role),
				       audit_log_get_type(audit_log, message->src_type));

	if (message->is_tgt_con)
		g_string_append_printf(str, " tcontext=%s:%s:%s",
				       audit_log_get_user(audit_log, message->tgt_user),
				       audit_log_get_role(audit_log, message->tgt_role),
				       audit_log_get_type(audit_log, message->tgt_type));

	if (message->is_obj_class)
		g_string_append_printf(str, " tclass=%s", audit_log_get_obj(audit_log, message->obj_class));

	g_string_append_printf(str, "\n");
	gtk_text_buffer_set_text(buffer, str->str, str->len);
	g_string_free(str, TRUE);
}

static void
write_load_policy_message_to_gtk_text_buf(GtkTextBuffer * buffer, const load_policy_msg_t * message, const char *message_header)
{
	GString *str;

	assert(buffer != NULL && message != NULL && message_header != NULL);
	str = g_string_new("");
	if (str == NULL) {
		fprintf(stderr, "Unable to create string buffer.\n");
		return;
	}

	g_string_append_printf(str, "%ssecurity:  %i users, %i roles, %i types, %i bools\n", message_header, message->users,
			       message->roles, message->types, message->bools);
	g_string_append_printf(str, "%ssecurity:  %i classes, %i rules\n", message_header, message->classes, message->rules);
	gtk_text_buffer_set_text(buffer, str->str, str->len);
	g_string_free(str, TRUE);
}

static void
write_boolean_message_to_gtk_text_buf(GtkTextBuffer * buffer, const boolean_msg_t * message, const char *message_header,
				      audit_log_t * audit_log)
{
	int i;
	GString *str;

	assert(buffer != NULL && message != NULL && message_header != NULL && audit_log != NULL);
	str = g_string_new("");
	if (str == NULL) {
		fprintf(stderr, "Unable to create string buffer.\n");
		return;
	}
	g_string_append_printf(str, "%ssecurity: committed booleans { ", message_header);

	for (i = 0; i < message->num_bools; i++) {
		g_string_append_printf(str, "%s:%i", audit_log_get_bool(audit_log, message->booleans[i]), message->values[i]);

		if ((i + 1) < message->num_bools)
			g_string_append_printf(str, ", ");
	}
	g_string_append_printf(str, " }\n");
	gtk_text_buffer_set_text(buffer, str->str, str->len);
	g_string_free(str, TRUE);
}

audit_log_view_t *seaudit_get_current_audit_log_view()
{
	seaudit_filtered_view_t *filtered_view;
	SEAuditLogViewStore *log_view_store;

	filtered_view = seaudit_window_get_current_view(seaudit_app->window);

	if (filtered_view == NULL)
		return NULL;

	log_view_store = filtered_view->store;

	if (log_view_store == NULL)
		return NULL;

	return log_view_store->log_view;
}

/*
 * glade autoconnected callbacks for menus
 *
 */
void seaudit_on_new_tab_clicked(GtkMenuItem * menu_item, gpointer user_data)
{
	seaudit_window_add_new_view(seaudit_app->window, seaudit_app->cur_log, seaudit_app->seaudit_conf.column_visibility, NULL);

}

void seaudit_on_open_view_clicked(GtkMenuItem * menu_item, gpointer user_data)
{
	seaudit_window_open_view(seaudit_app->window, seaudit_app->cur_log, seaudit_app->seaudit_conf.column_visibility);
}

void seaudit_on_save_view_clicked(GtkMenuItem * menu_item, gpointer user_data)
{
	seaudit_window_save_current_view(seaudit_app->window, FALSE);
}

void seaudit_on_saveas_view_clicked(GtkMenuItem * menu_item, gpointer user_data)
{
	seaudit_window_save_current_view(seaudit_app->window, TRUE);
}

void seaudit_window_view_entire_message_in_textbox(int *tree_item_idx)
{
	GtkWidget *view, *window, *scroll;
	GtkTextBuffer *buffer;
	GtkTreeSelection *sel;
	GList *glist = NULL, *item = NULL;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path = NULL;
	int fltr_msg_idx, msg_list_idx;
	char *message_header = NULL;
	msg_t *message = NULL;
	audit_log_view_t *log_view;
	audit_log_t *audit_log = NULL;
	seaudit_filtered_view_t *seaudit_view;

	seaudit_view = seaudit_window_get_current_view(seaudit_app->window);
	assert(seaudit_view != NULL);
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	scroll = gtk_scrolled_window_new(NULL, NULL);
	view = gtk_text_view_new();
	gtk_window_set_title(GTK_WINDOW(window), "View Entire Message");
	gtk_window_set_default_size(GTK_WINDOW(window), 480, 300);
	gtk_container_add(GTK_CONTAINER(window), scroll);
	gtk_container_add(GTK_CONTAINER(scroll), view);
	gtk_text_view_set_wrap_mode((GtkTextView *) view, GTK_WRAP_WORD);

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));

	log_view = seaudit_get_current_audit_log_view();
	if (log_view == NULL) {
		fprintf(stderr, "Unable to obtain current audit log view.\n");
		return;
	}
	audit_log = log_view->my_log;
	assert(audit_log != NULL);

	if (tree_item_idx == NULL) {
		sel = gtk_tree_view_get_selection(seaudit_view->tree_view);
		glist = gtk_tree_selection_get_selected_rows(sel, &model);
		if (glist == NULL) {
			return;
		}
		/* Only grab the top-most selected item */
		item = glist;
		path = item->data;
		if (gtk_tree_model_get_iter(model, &iter, path) == 0) {
			fprintf(stderr, "Could not get valid iterator for the selected path.\n");
			if (glist) {
				g_list_foreach(glist, (GFunc) gtk_tree_path_free, NULL);
				g_list_free(glist);
			}
			return;
		}
		fltr_msg_idx = seaudit_log_view_store_iter_to_idx((SEAuditLogViewStore *) model, &iter);
	} else {
		fltr_msg_idx = *tree_item_idx;
	}

	msg_list_idx = fltr_msg_idx;
	message = apol_vector_get_element(audit_log->msg_list, msg_list_idx);

	message_header = (char *)malloc((TIME_SIZE + STR_SIZE) * sizeof(char));
	if (message_header == NULL) {
		fprintf(stderr, "memory error\n");
		if (glist) {
			g_list_foreach(glist, (GFunc) gtk_tree_path_free, NULL);
			g_list_free(glist);
		}
		return;
	}

	generate_message_header(message_header, audit_log, message->date_stamp,
				(char *)audit_log_get_host(audit_log, message->host));
	if (message->msg_type == AVC_MSG)
		write_avc_message_to_gtk_text_buf(buffer, message->msg_data.avc_msg, message_header, audit_log);
	else if (message->msg_type == LOAD_POLICY_MSG)
		write_load_policy_message_to_gtk_text_buf(buffer, message->msg_data.load_policy_msg, message_header);
	else if (message->msg_type == BOOLEAN_MSG)
		write_boolean_message_to_gtk_text_buf(buffer, message->msg_data.boolean_msg, message_header, audit_log);

	if (message_header)
		free(message_header);

	if (glist) {
		g_list_foreach(glist, (GFunc) gtk_tree_path_free, NULL);
		g_list_free(glist);
	}

	gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
	gtk_widget_show(view);
	gtk_widget_show(scroll);
	gtk_widget_show(window);
}

void seaudit_window_on_view_entire_msg_activated(GtkWidget * widget, GdkEvent * event, gpointer callback_data)
{
	seaudit_window_view_entire_message_in_textbox(NULL);
}

void seaudit_on_ExportLog_activate(GtkWidget * widget, GdkEvent * event, gpointer callback_data)
{
	seaudit_save_log_file(FALSE);

	return;
}

void seaudit_on_export_selection_activated(void)
{
	seaudit_save_log_file(TRUE);
}

/*
 * glade autoconnected callbacks for the main toolbar
 */
void seaudit_on_filter_log_button_clicked(GtkWidget * widget, GdkEvent * event, gpointer callback_data)
{
	seaudit_filtered_view_t *view;

	if (seaudit_app->cur_log == NULL) {
		message_display(seaudit_app->window->window, GTK_MESSAGE_ERROR, "There is no audit log loaded.");
		return;
	}

	view = seaudit_window_get_current_view(seaudit_app->window);
	seaudit_filtered_view_display(view, seaudit_app->window->window);
	return;
}

void seaudit_on_top_window_query_button_clicked(GtkWidget * widget, GdkEvent * event, gpointer callback_data)
{
	query_window_create(NULL);
}

void seaudit_on_create_standard_report_activate()
{
	if (!seaudit_app->report_window) {
		seaudit_app->report_window = report_window_create(seaudit_app->window, &seaudit_app->seaudit_conf, "Create Report");
		if (!seaudit_app->report_window) {
			fprintf(stderr, "Error: Out of memory!");
			return;
		}
	}

	report_window_display(seaudit_app->report_window);
}

void seaudit_on_real_time_button_pressed(GtkButton * button, gpointer user_data)
{
	bool_t state = seaudit_app->real_time_state;
	seaudit_set_real_time_log_button_state(!state);
}

/*
 * Gtk callbacks registered by seaudit_t object
 */
static void seaudit_set_recent_logs_submenu(seaudit_conf_t * conf_file)
{
	GtkWidget *submenu, *submenu_item;
	GtkMenuItem *recent;
	int i;

	recent = GTK_MENU_ITEM(glade_xml_get_widget(seaudit_app->window->xml, "OpenRecentLog"));
	g_assert(recent);
	gtk_menu_item_remove_submenu(recent);
	submenu = gtk_menu_new();
	for (i = 0; i < conf_file->num_recent_log_files; i++) {
		submenu_item = gtk_menu_item_new_with_label(conf_file->recent_log_files[i]);
		gtk_menu_shell_prepend(GTK_MENU_SHELL(submenu), submenu_item);
		gtk_widget_show(submenu_item);
		g_signal_connect(G_OBJECT(submenu_item), "activate", G_CALLBACK(seaudit_log_file_open_from_recent_menu),
				 conf_file->recent_log_files[i]);
	}
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(recent), submenu);
	return;
}

static void seaudit_set_recent_policys_submenu(seaudit_conf_t * conf_file)
{
	GtkWidget *submenu, *submenu_item;
	GtkMenuItem *recent;
	int i;

	recent = GTK_MENU_ITEM(glade_xml_get_widget(seaudit_app->window->xml, "OpenRecentPolicy"));
	g_assert(recent);
	submenu = gtk_menu_new();
	for (i = 0; i < conf_file->num_recent_policy_files; i++) {
		submenu_item = gtk_menu_item_new_with_label(conf_file->recent_policy_files[i]);
		gtk_menu_shell_prepend(GTK_MENU_SHELL(submenu), submenu_item);
		gtk_widget_show(submenu_item);
		g_signal_connect(G_OBJECT(submenu_item), "activate", G_CALLBACK(seaudit_policy_file_open_from_recent_menu),
				 conf_file->recent_policy_files[i]);
	}
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(recent), submenu);
	return;
}

static void seaudit_policy_file_open_from_recent_menu(GtkWidget * widget, gpointer user_data)
{
	const char *filename = (const char *)user_data;
	seaudit_open_policy(seaudit_app, filename);
}

static void seaudit_log_file_open_from_recent_menu(GtkWidget * widget, gpointer user_data)
{
	const char *filename = (const char *)user_data;
	seaudit_open_log_file(seaudit_app, filename);
}

/*
 * Timeout function used to keep the log up to date, always
 * return TRUE so we get called repeatedly */
static gboolean seaudit_real_time_update_log(gpointer callback_data)
{
	unsigned int rt = 0;
#define MSG_SIZE 64		       /* should be big enough */

	/* simply return if the log is not open */
	if (!seaudit_app->log_file_ptr)
		return TRUE;

	rt |= audit_log_parse(seaudit_app->cur_log, seaudit_app->log_file_ptr);
	if (rt & PARSE_RET_NO_SELINUX_ERROR)
		return TRUE;
	seaudit_window_filter_views(seaudit_app->window);
	return TRUE;
}

/*
 * Helper functions for seaudit_t
 */
static void seaudit_set_real_time_log_button_state(bool_t state)
{
	GtkWidget *widget, *image, *text, *lbl;

	widget = glade_xml_get_widget(seaudit_app->window->xml, "RealTimeButton");
	g_assert(widget);
	text = glade_xml_get_widget(seaudit_app->window->xml, "RealTimeLabel");
	g_assert(text);
	image = glade_xml_get_widget(seaudit_app->window->xml, "RealTimeImage");
	g_assert(image);
	lbl = glade_xml_get_widget(seaudit_app->window->xml, "monitor_lbl");
	g_assert(lbl);
	gtk_label_set_text(GTK_LABEL(text), "Toggle Monitor");
	gtk_image_set_from_stock(GTK_IMAGE(image), GTK_STOCK_REFRESH, GTK_ICON_SIZE_SMALL_TOOLBAR);

	/* remove timeout function if exists */
	if (seaudit_app->timeout_key)
		gtk_timeout_remove(seaudit_app->timeout_key);

	if (!state) {
		/*gtk_image_set_from_stock(GTK_IMAGE(image), GTK_STOCK_STOP, GTK_ICON_SIZE_SMALL_TOOLBAR); */
		gtk_label_set_markup(GTK_LABEL(lbl), "Monitor status: <span foreground=\"red\">OFF</span>");
		/* make inactive */
		seaudit_app->timeout_key = 0;
		seaudit_app->real_time_state = state;
	} else {
		gtk_image_set_from_stock(GTK_IMAGE(image), GTK_STOCK_REFRESH, GTK_ICON_SIZE_SMALL_TOOLBAR);
		gtk_label_set_markup(GTK_LABEL(lbl), "Monitor status: <span foreground=\"green\">ON</span>");
		/* make active */
		seaudit_app->timeout_key = g_timeout_add(seaudit_app->seaudit_conf.real_time_interval,
							 &seaudit_real_time_update_log, NULL);
		seaudit_app->real_time_state = state;
	}
}

static int seaudit_read_policy_conf(const char *fname)
{
	char *buf = NULL;
	size_t len;
	int rt;

	rt = apol_file_read_to_buffer(fname, &buf, &len);
	if (rt != 0) {
		if (buf)
			free(buf);
		return -1;
	}

	seaudit_app->policy_text = gtk_text_buffer_new(NULL);
	gtk_text_buffer_set_text(seaudit_app->policy_text, buf, len);
	free(buf);
	return 0;
}

/*
 * seaudit callbacks
 */
static void seaudit_update_title_bar(void *user_data)
{
	char str[STR_SIZE];
	char log_str[STR_SIZE];
	char policy_str[STR_SIZE];

	if (seaudit_app->cur_log != NULL) {
		g_assert(seaudit_app->audit_log_file->str);
		snprintf(log_str, STR_SIZE, "[Log file: %s]", (const char *)seaudit_app->audit_log_file->str);
	} else {
		snprintf(log_str, STR_SIZE, "[Log file: No Log]");
	}

	if (seaudit_app->cur_policy != NULL) {
		snprintf(policy_str, STR_SIZE, "[Policy file: %s]", (const char *)seaudit_app->policy_file->str);
	} else {
		snprintf(policy_str, STR_SIZE, "[Policy file: No Policy]");
	}
	snprintf(str, STR_SIZE, "seaudit - %s %s", log_str, policy_str);
	gtk_window_set_title(seaudit_app->window->window, (gchar *) str);
}

#endif
