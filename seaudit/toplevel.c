/**
 *  @file toplevel.c
 *  Implementation for the main toplevel window.
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

#include "message_view.h"
#include "preferences_view.h"
#include "progress.h"
#include "toplevel.h"
#include "utilgui.h"

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <apol/util.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>
#include <seaudit/parse.h>

struct toplevel
{
	seaudit_t *s;
	progress_t *progress;
	/** vector of message_view_t that are in the toplevel's notebook */
	apol_vector_t *views;
	GladeXML *xml;
	/** toplevel window widget */
	GtkWindow *w;
	GtkNotebook *notebook;
	/** serial number for models created, such that new models
	 * will be named Untitled <number> */
	int next_model_number;
};

/**
 * Given a view, return its index within the toplevel notebook pages.
 *
 * @param top Toplevel containing the notebook.
 * @param view View to look up.
 *
 * @return Index of the view (zero-indexed), or -1 if not found.
 */
static gint toplevel_notebook_find_view(toplevel_t * top, message_view_t * view)
{
	gint num_pages = gtk_notebook_get_n_pages(top->notebook);
	while (num_pages >= 1) {
		GtkWidget *child = gtk_notebook_get_nth_page(top->notebook, num_pages - 1);
		GtkWidget *tab = gtk_notebook_get_tab_label(top->notebook, child);
		message_view_t *v = g_object_get_data(G_OBJECT(tab), "view");
		if (v == view) {
			return num_pages;
		}
		num_pages--;
	}
	return -1;
}

/**
 * Return the view on the page that is currently raised, or NULL if
 * there are no views.
 */
static message_view_t *toplevel_get_current_view(toplevel_t * top)
{
	gint current = gtk_notebook_get_current_page(top->notebook);
	if (current >= 0) {
		GtkWidget *child = gtk_notebook_get_nth_page(top->notebook, current);
		GtkWidget *tab = gtk_notebook_get_tab_label(top->notebook, child);
		return g_object_get_data(G_OBJECT(tab), "view");
	}
	return NULL;
}

static void toplevel_update_status_bar(toplevel_t * top);

static void toplevel_on_notebook_switch_page(GtkNotebook * notebook, GtkNotebookPage * page, guint pagenum, toplevel_t * top)
{
	/* FIX ME */
	toplevel_update_status_bar(top);
}

/**
 * Callback invoked when a tab close button is clicked.
 */
static void toplevel_on_tab_close(GtkButton * button, toplevel_t * top)
{
	message_view_t *view = g_object_get_data(G_OBJECT(button), "view");
	gint index = toplevel_notebook_find_view(top, view);
	size_t i;
	assert(index >= 0);
	gtk_notebook_remove_page(top->notebook, index);
	apol_vector_get_index(top->views, view, NULL, NULL, &i);
	apol_vector_remove(top->views, i);
	message_view_destroy(&view);
}

/**
 * Create a new view associated with the given model, then create a
 * tab to place that view.  The newly created tab will then be raised.
 *
 * @param top Toplevel containing notebook to which add the view and tab.
 * @param model Model from which to create a view.
 */
static void toplevel_add_new_view(toplevel_t * top, seaudit_model_t * model)
{
	message_view_t *view;
	GtkWidget *tab, *button, *label, *image;
	gint index;
	if ((view = message_view_create(top, model)) == NULL) {
		return;
	}
	if (apol_vector_append(top->views, view) < 0) {
		toplevel_ERR(top, "%s", strerror(errno));
		message_view_destroy(&view);
		return;
	}
	tab = gtk_hbox_new(FALSE, 5);
	g_object_set_data(G_OBJECT(tab), "view", view);
	button = gtk_button_new();
	g_object_set_data(G_OBJECT(button), "view", view);
	image = gtk_image_new_from_stock(GTK_STOCK_CLOSE, GTK_ICON_SIZE_MENU);
	gtk_container_add(GTK_CONTAINER(button), image);
	gtk_widget_set_size_request(image, 8, 8);
	g_signal_connect(G_OBJECT(button), "pressed", G_CALLBACK(toplevel_on_tab_close), top);
	label = gtk_label_new(seaudit_model_get_name(model));
	g_object_set_data(G_OBJECT(tab), "label", label);
	gtk_box_pack_start(GTK_BOX(tab), label, TRUE, TRUE, 5);
	gtk_box_pack_end(GTK_BOX(tab), button, FALSE, FALSE, 5);
	gtk_widget_show(label);
	gtk_widget_show(button);
	gtk_widget_show(image);
	index = gtk_notebook_append_page(top->notebook, message_view_get_view(view), tab);
	gtk_notebook_set_current_page(top->notebook, index);
}

/**
 * Create a new model for the currently loaded log file (which could
 * be NULL), then create a view that watches that model.
 */
static void toplevel_add_new_model(toplevel_t * top)
{
	seaudit_log_t *log = seaudit_get_log(top->s);
	char *model_name = NULL;
	seaudit_model_t *model = NULL;
	if (asprintf(&model_name, "Untitled %d", top->next_model_number) < 0) {
		toplevel_ERR(top, "%s", strerror(errno));
		return;
	}
	model = seaudit_model_create(model_name, log);
	free(model_name);
	if (model == NULL) {
		toplevel_ERR(top, "%s", strerror(errno));
		return;
	} else {
		top->next_model_number++;
		toplevel_add_new_view(top, model);
	}
}

/**
 * Callback whenever an item from the recent logs submenu is activated.
 */
static void toplevel_on_open_recent_log_activate(GtkWidget * widget, gpointer user_data)
{
	GtkWidget *label = gtk_bin_get_child(GTK_BIN(widget));
	const char *path = gtk_label_get_text(GTK_LABEL(label));
	toplevel_t *top = (toplevel_t *) user_data;
	toplevel_open_log(top, path);
}

/**
 * Update the entries within recent logs submenu to match those in the
 * preferences object.
 */
static void toplevel_set_recent_logs_submenu(toplevel_t * top)
{
	GtkMenuItem *recent = GTK_MENU_ITEM(glade_xml_get_widget(top->xml, "OpenRecentLog"));
	apol_vector_t *paths = preferences_get_recent_logs(toplevel_get_prefs(top));
	GtkWidget *submenu, *submenu_item;
	size_t i;

	gtk_menu_item_remove_submenu(recent);
	submenu = gtk_menu_new();
	for (i = 0; i < apol_vector_get_size(paths); i++) {
		char *path = (char *)apol_vector_get_element(paths, i);
		submenu_item = gtk_menu_item_new_with_label(path);
		gtk_menu_shell_prepend(GTK_MENU_SHELL(submenu), submenu_item);
		gtk_widget_show(submenu_item);
		g_signal_connect(G_OBJECT(submenu_item), "activate", G_CALLBACK(toplevel_on_open_recent_log_activate), top);
	}
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(recent), submenu);
}

/**
 * Callback whenever an item from the recent policies submenu is
 * activated.
 */
static void toplevel_on_open_recent_policy_activate(GtkWidget * widget, gpointer user_data)
{
	GtkWidget *label = gtk_bin_get_child(GTK_BIN(widget));
	const char *path = gtk_label_get_text(GTK_LABEL(label));
	toplevel_t *top = (toplevel_t *) user_data;
	/* FIX ME!
	 * toplevel_open_policy(top, path);
	 */
}

/**
 * Update the entries within recent policies submenu to match those in
 * the preferences object.
 */
static void toplevel_set_recent_policies_submenu(toplevel_t * top)
{
	GtkMenuItem *recent = GTK_MENU_ITEM(glade_xml_get_widget(top->xml, "OpenRecentPolicy"));
	apol_vector_t *paths = preferences_get_recent_policies(toplevel_get_prefs(top));
	GtkWidget *submenu, *submenu_item;
	size_t i;

	gtk_menu_item_remove_submenu(recent);
	submenu = gtk_menu_new();
	for (i = 0; i < apol_vector_get_size(paths); i++) {
		char *path = (char *)apol_vector_get_element(paths, i);
		submenu_item = gtk_menu_item_new_with_label(path);
		gtk_menu_shell_prepend(GTK_MENU_SHELL(submenu), submenu_item);
		gtk_widget_show(submenu_item);
		g_signal_connect(G_OBJECT(submenu_item), "activate", G_CALLBACK(toplevel_on_open_recent_policy_activate), top);
	}
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(recent), submenu);
}

/**
 * Enable/disable all items (menus and buttons) that depend upon if a
 * log is loaded.
 *
 * @param top Toplevel object containing menu items.
 * @param TRUE to enable items, FALSE to disable.
 */
static void toplevel_enable_log_items(toplevel_t * top, gboolean sens)
{
	static const char *items[] = {
		"NewView", "OpenView", "SaveView", "SaveViewAs", "ModifyView",
		"ExportAll", "ExportSelected", "ViewMessage",
		"CreateReport", "MonitorLog", "ModifyViewButton", "MonitorLogButton",
		NULL
	};
	size_t i;
	const char *s;
	for (i = 0, s = items[0]; s != NULL; s = items[++i]) {
		GtkWidget *w = glade_xml_get_widget(top->xml, s);
		assert(w != NULL);
		gtk_widget_set_sensitive(w, sens);
	}
}

/**
 * Enable/disable all items (menus and buttons) that depend upon if a
 * policy is loaded.
 *
 * @param top Toplevel object containing menu items.
 * @param TRUE to enable items, FALSE to disable.
 */
static void toplevel_enable_policy_items(toplevel_t * top, gboolean sens)
{
	static const char *items[] = {
		"FindTERules", "FindTERulesButton",
		NULL
	};
	size_t i;
	const char *s;
	for (i = 0, s = items[0]; s != NULL; s = items[++i]) {
		GtkWidget *w = glade_xml_get_widget(top->xml, s);
		assert(w != NULL);
		gtk_widget_set_sensitive(w, sens);
	}
}

/**
 * Update the toplevel's title bar to list the log and policy files
 * opened.
 *
 * @param top Toplevel to modify.
 */
static void toplevel_update_title_bar(toplevel_t * top)
{
	char *log_path = seaudit_get_log_path(top->s);
	char *policy_path = seaudit_get_policy_path(top->s);
	char *s;

	if (log_path == NULL) {
		log_path = "No Log";
	}
	if (policy_path == NULL) {
		policy_path = "No Policy";
	}
	if (asprintf(&s, "seaudit - [Log file: %s] [Policy file: %s]", log_path, policy_path) < 0) {
		toplevel_ERR(top, "%s", strerror(errno));
		return;
	}
	gtk_window_set_title(top->w, s);
	free(s);
}

/**
 * Update the status bar to show the current policy, number of log
 * messages in the current view, range of messages in current view,
 * and monitor status.
 */
static void toplevel_update_status_bar(toplevel_t * top)
{
	apol_policy_t *policy = seaudit_get_policy(top->s);
	GtkLabel *policy_version = (GtkLabel *) glade_xml_get_widget(top->xml, "PolicyVersionLabel");
	GtkLabel *log_num = (GtkLabel *) glade_xml_get_widget(top->xml, "LogNumLabel");
	GtkLabel *log_dates = (GtkLabel *) glade_xml_get_widget(top->xml, "LogDateLabel");
	seaudit_log_t *log = toplevel_get_log(top);

	if (policy == NULL) {
		gtk_label_set_text(policy_version, "Policy Version: No policy");
	} else {
		char *policy_str = apol_policy_get_version_type_mls_str(policy);
		char *s;
		if (asprintf(&s, "Policy Version: %s", policy_str) < 0) {
			toplevel_ERR(top, "%s", strerror(errno));
			free(policy_str);
		} else {
			gtk_label_set_text(policy_version, s);
			free(s);
			free(policy_str);
		}
	}

	if (log == NULL) {
		gtk_label_set_text(log_num, "Log Messages: No log");
		gtk_label_set_text(log_dates, "Dates: No log");
	} else {
		message_view_t *view = toplevel_get_current_view(top);
		size_t num_messages = seaudit_get_num_log_messages(top->s);
		size_t num_view_messages;
		struct tm *first = seaudit_get_log_first(top->s);
		struct tm *last = seaudit_get_log_last(top->s);
		assert(view != NULL);
		num_view_messages = message_view_get_num_log_messages(view);
		char *s, t1[256], t2[256];
		if (asprintf(&s, "Log Messages: %zd/%zd", num_view_messages, num_messages) < 0) {
			toplevel_ERR(top, "%s", strerror(errno));
		} else {
			gtk_label_set_text(log_num, s);
			free(s);
		}
		strftime(t1, 256, "%b %d %H:%M:%S", first);
		strftime(t2, 256, "%b %d %H:%M:%S", last);
		if (asprintf(&s, "Dates: %s - %s", t1, t2) < 0) {
			toplevel_ERR(top, "%s", strerror(errno));
		} else {
			gtk_label_set_text(log_dates, s);
			free(s);
		}
		/* FIX ME
		 * seaudit_view_entire_selection_update_sensitive(TRUE);
		 */
	}
}

static void init_icons(toplevel_t * top)
{
	const char *icon_names[] = { "seaudit-small.png", "seaudit.png" };
	GdkPixbuf *icon;
	char *path;
	GList *icon_list = NULL;
	size_t i;
	for (i = 0; i < sizeof(icon_names) / sizeof(icon_names[0]); i++) {
		if ((path = apol_file_find_path(icon_names[i])) == NULL) {
			continue;
		}
		icon = gdk_pixbuf_new_from_file(path, NULL);
		free(path);
		if (icon == NULL) {
			continue;
		}
		icon_list = g_list_append(icon_list, icon);
	}
	gtk_window_set_default_icon_list(icon_list);
	gtk_window_set_icon_list(top->w, icon_list);
}

toplevel_t *toplevel_create(seaudit_t * s)
{
	toplevel_t *top;
	char *path;
	GtkWidget *vbox;
	int error = 0;

	if ((top = calloc(1, sizeof(*top))) == NULL || (top->views = apol_vector_create()) == NULL) {
		error = errno;
		goto cleanup;
	}
	top->s = s;
	top->next_model_number = 1;
	if ((path = apol_file_find_path("seaudit.glade")) == NULL) {
		error = EIO;
		goto cleanup;
	}
	top->xml = glade_xml_new(path, NULL, NULL);
	free(path);
	top->w = GTK_WINDOW(glade_xml_get_widget(top->xml, "TopLevel"));
	gtk_object_set_data(GTK_OBJECT(top->w), "toplevel", top);
	init_icons(top);
	top->notebook = GTK_NOTEBOOK(gtk_notebook_new());
	g_signal_connect_after(G_OBJECT(top->notebook), "switch-page", G_CALLBACK(toplevel_on_notebook_switch_page), top);
	vbox = glade_xml_get_widget(top->xml, "NotebookVBox");
	gtk_container_add(GTK_CONTAINER(vbox), GTK_WIDGET(top->notebook));
	gtk_widget_show(GTK_WIDGET(top->notebook));
	gtk_widget_show(GTK_WIDGET(top->w));
	toplevel_set_recent_logs_submenu(top);
	toplevel_set_recent_policies_submenu(top);

	glade_xml_signal_autoconnect(top->xml);

	/* create initial blank tab for the notebook */
	toplevel_add_new_model(top);

	if ((top->progress = progress_create(top, top->w)) == NULL) {
		error = errno;
		goto cleanup;
	}
      cleanup:
	if (error != 0) {
		toplevel_destroy(&top);
		errno = error;
		return NULL;
	}
	return top;
}

static void message_view_free(void *elem)
{
	message_view_t *view = elem;
	message_view_destroy(&view);
}

void toplevel_destroy(toplevel_t ** top)
{
	if (top != NULL && *top != NULL) {
		apol_vector_destroy(&(*top)->views, message_view_free);
		progress_destroy(&(*top)->progress);
		if ((*top)->w != NULL) {
			gtk_widget_destroy(GTK_WIDGET((*top)->w));
		}
		free(*top);
		*top = NULL;
	}
}

struct log_run_datum
{
	toplevel_t *top;
	const char *filename;
	seaudit_log_t *log;
	int result;
};

/**
 * Thread that loads and parses a log file.  It will write to
 * progress_seaudit_handle_func() its status during the load.
 *
 * @param data Pointer to a struct log_run_datum, for control
 * information.
 */
static gpointer toplevel_open_log_runner(gpointer data)
{
	struct log_run_datum *run = (struct log_run_datum *)data;
	FILE *f;
	progress_update(run->top->progress, "Parsing %s", run->filename);
	if ((f = fopen(run->filename, "r")) == NULL) {
		progress_abort(run->top->progress, "Could not open %s for reading.", run->filename);
		run->result = -1;
		goto cleanup;
	}
	if ((run->log = seaudit_log_create(progress_seaudit_handle_func, run->top->progress)) == NULL) {
		progress_update(run->top->progress, "%s", strerror(errno));
		run->result = -1;
		goto cleanup;
	}
	run->result = seaudit_log_parse(run->log, f);
      cleanup:
	if (f != NULL) {
		fclose(f);
	}
	if (run->result < 0) {
		seaudit_log_destroy(&run->log);
		progress_abort(run->top->progress, NULL);
	} else if (run->result > 0) {
		progress_warn(run->top->progress, NULL);
	} else {
		progress_done(run->top->progress);
	}
	return NULL;
}

/**
 * Destroy all views and their notebook tabs.
 */
static void toplevel_destroy_views(toplevel_t * top)
{
	gint num_pages = gtk_notebook_get_n_pages(top->notebook);
	while (num_pages >= 1) {
		message_view_t *view = apol_vector_get_element(top->views, num_pages - 1);
		gtk_notebook_remove_page(top->notebook, num_pages - 1);
		message_view_destroy(&view);
		apol_vector_remove(top->views, num_pages - 1);
		num_pages--;
	}
}

void toplevel_open_log(toplevel_t * top, const char *filename)
{
	struct log_run_datum run = { top, filename, NULL, 0 };

	util_cursor_wait(GTK_WIDGET(top->w));
	progress_show(top->progress, "Opening Log");
	g_thread_create(toplevel_open_log_runner, &run, FALSE, NULL);
	progress_wait(top->progress);
	progress_hide(top->progress);
	util_cursor_clear(GTK_WIDGET(top->w));

	if (run.result < 0) {
		return;
	}
	toplevel_destroy_views(top);
	top->next_model_number = 1;
	seaudit_set_log(top->s, run.log, filename);
	toplevel_set_recent_logs_submenu(top);
	toplevel_enable_log_items(top, TRUE);
	toplevel_add_new_model(top);
	toplevel_update_title_bar(top);
	toplevel_update_status_bar(top);
}

preferences_t *toplevel_get_prefs(toplevel_t * top)
{
	return seaudit_get_prefs(top->s);
}

seaudit_log_t *toplevel_get_log(toplevel_t * top)
{
	return seaudit_get_log(top->s);
}

GladeXML *toplevel_get_glade_xml(toplevel_t * top)
{
	return top->xml;
}

/**
 * Pop-up a dialog with a line of text and wait for the user to
 * dismiss the dialog.
 *
 * @param top Toplevel window; this message dialog will be centered
 * upon it.
 * @param msg_type Type of message being displayed.
 * @param fmt Format string to print, using syntax of printf(3).
 */
static void toplevel_message(toplevel_t * top, GtkMessageType msg_type, const char *fmt, va_list ap)
{
	GtkWidget *dialog;
	char *msg;
	if (vasprintf(&msg, fmt, ap) < 0) {
		ERR(NULL, "%s", strerror(errno));
		return;
	}
	dialog = gtk_message_dialog_new(top->w, GTK_DIALOG_DESTROY_WITH_PARENT, msg_type, GTK_BUTTONS_CLOSE, msg);
	free(msg);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

void toplevel_ERR(toplevel_t * top, const char *format, ...)
{
	va_list(ap);
	va_start(ap, format);
	toplevel_message(top, GTK_MESSAGE_ERROR, format, ap);
	va_end(ap);
}

void toplevel_WARN(toplevel_t * top, const char *format, ...)
{
	va_list(ap);
	va_start(ap, format);
	toplevel_message(top, GTK_MESSAGE_WARNING, format, ap);
	va_end(ap);
}

/************* below are callbacks for the toplevel menu items *************/

void toplevel_on_destroy(gpointer user_data, GtkObject * object __attribute__ ((unused)))
{
	toplevel_t *top = gtk_object_get_data(GTK_OBJECT(user_data), "toplevel");
	top->w = NULL;
	gtk_main_quit();
}

void toplevel_on_open_log_activate(gpointer user_data, GtkWidget * widget __attribute__ ((unused)))
{
	toplevel_t *top = gtk_object_get_data(GTK_OBJECT(user_data), "toplevel");
	char *path;
	GtkWidget *dialog =
		gtk_file_chooser_dialog_new("Open Log", top->w, GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					    GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
	path = seaudit_get_log_path(top->s);
	if (path != NULL) {
		gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), path);
	}
	if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy(dialog);
		return;
	}
	path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
	gtk_widget_destroy(dialog);
	toplevel_open_log(top, path);
	g_free(path);
}

void toplevel_on_open_policy_activate(gpointer user_data, GtkWidget * widget __attribute__ ((unused)))
{
	toplevel_t *top = gtk_object_get_data(GTK_OBJECT(user_data), "toplevel");
	char *path;
	GtkWidget *dialog = gtk_file_chooser_dialog_new("Open Policy", top->w, GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL,
							GTK_RESPONSE_CANCEL,
							GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
	path = seaudit_get_policy_path(top->s);
	if (path != NULL) {
		gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), path);
	}
	if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy(dialog);
		return;
	}
	path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
	gtk_widget_destroy(dialog);
	printf("loading policy at %s\n", path);
}

void toplevel_on_preferences_activate(gpointer user_data, GtkWidget * widget __attribute__ ((unused)))
{
	toplevel_t *top = gtk_object_get_data(GTK_OBJECT(user_data), "toplevel");
	if (preferences_view_run(top, top->w)) {
		size_t i;
		for (i = 0; i < apol_vector_get_size(top->views); i++) {
			message_view_t *v = apol_vector_get_element(top->views, i);
			message_view_update_visible_columns(v);
		}
	}
}

void toplevel_on_quit_activate(gpointer user_data, GtkWidget * widget __attribute__ ((unused)))
{
	toplevel_t *top = gtk_object_get_data(GTK_OBJECT(user_data), "toplevel");
	top->w = NULL;
	gtk_main_quit();
}

void toplevel_on_new_tab_activate(gpointer user_data, GtkWidget * widget __attribute__ ((unused)))
{
	toplevel_t *top = gtk_object_get_data(GTK_OBJECT(user_data), "toplevel");
	toplevel_add_new_model(top);
}

void toplevel_on_help_activate(gpointer user_data, GtkMenuItem * widget __attribute__ ((unused)))
{
	toplevel_t *top = gtk_object_get_data(GTK_OBJECT(user_data), "toplevel");
	GtkWidget *window;
	GtkWidget *scroll;
	GtkWidget *text_view;
	GtkTextBuffer *buffer;
	char *help_text = NULL;
	size_t len;
	int rt;
	char *dir;

	window = gtk_dialog_new_with_buttons("seaudit Help",
					     GTK_WINDOW(top->w),
					     GTK_DIALOG_DESTROY_WITH_PARENT, GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(window), GTK_RESPONSE_CLOSE);
	g_signal_connect_swapped(window, "response", G_CALLBACK(gtk_widget_destroy), window);
	scroll = gtk_scrolled_window_new(NULL, NULL);
	text_view = gtk_text_view_new();
	gtk_window_set_default_size(GTK_WINDOW(window), 520, 300);
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(window)->vbox), scroll);
	gtk_container_add(GTK_CONTAINER(scroll), text_view);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_NONE);
	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
	dir = apol_file_find_path("seaudit_help.txt");
	if (!dir) {
		toplevel_ERR(top, "Cannot find help file.");
		return;
	}
	rt = apol_file_read_to_buffer(dir, &help_text, &len);
	free(dir);
	if (rt != 0) {
		free(help_text);
		return;
	}
	gtk_text_buffer_set_text(buffer, help_text, len);
	gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ON_PARENT);
	gtk_widget_show(text_view);
	gtk_widget_show(scroll);
	gtk_dialog_run(GTK_DIALOG(window));
}

void toplevel_on_about_seaudit_activate(gpointer user_data, GtkMenuItem * widget __attribute__ ((unused)))
{
	toplevel_t *top = gtk_object_get_data(GTK_OBJECT(user_data), "toplevel");
	gtk_show_about_dialog(top->w,
			      "comments", "Audit Log Analysis Tool for Security Enhanced Linux",
			      "copyright", COPYRIGHT_INFO,
			      "name", "seaudit", "version", VERSION, "website", "http://oss.tresys.com/projects/setools", NULL);
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

void generate_message_header(char *message_header, audit_log_t * audit_log, struct tm *date_stamp, char *host)
{
	assert(message_header != NULL && audit_log != NULL && date_stamp != NULL);

	strftime(message_header, TIME_SIZE, "%b %d %T", date_stamp);
	strcat(message_header, " ");
	strcat(message_header, host);
	strcat(message_header, " kernel: ");

	return;
}

void write_avc_message_to_file(FILE * log_file, const avc_msg_t * message, const char *message_header, audit_log_t * audit_log)
{
	int i;

	assert(log_file != NULL && message != NULL && message_header != NULL && audit_log != NULL);

	fprintf(log_file, "%s", message_header);
	if (!(message->tm_stmp_sec == 0 && message->tm_stmp_nano == 0 && message->serial == 0))
		fprintf(log_file, "audit(%lu.%03lu:%u): ", message->tm_stmp_sec, message->tm_stmp_nano, message->serial);

	fprintf(log_file, "avc:  %s  {", ((message->msg == AVC_GRANTED) ? "granted" : "denied"));

	for (i = 0; i < apol_vector_get_size(message->perms); i++)
		fprintf(log_file, " %s", (char *)apol_vector_get_element(message->perms, i));

	fprintf(log_file, " } for ");

	if (message->is_pid)
		fprintf(log_file, " pid=%i", message->pid);

	if (message->exe)
		fprintf(log_file, " exe=%s", message->exe);

	if (message->comm)
		fprintf(log_file, " comm=%s", message->comm);

	if (message->path)
		fprintf(log_file, " path=%s", message->path);

	if (message->name)
		fprintf(log_file, " name=%s", message->name);

	if (message->dev)
		fprintf(log_file, " dev=%s", message->dev);

	if (message->is_inode)
		fprintf(log_file, " ino=%li", message->inode);

	if (message->ipaddr)
		fprintf(log_file, " ipaddr=%s", message->ipaddr);

	if (message->saddr)
		fprintf(log_file, " saddr=%s", message->saddr);

	if (message->source != 0)
		fprintf(log_file, " src=%i", message->source);

	if (message->daddr)
		fprintf(log_file, " daddr=%s", message->daddr);

	if (message->dest != 0)
		fprintf(log_file, " dest=%i", message->dest);

	if (message->netif)
		fprintf(log_file, " netif=%s", message->netif);

	if (message->laddr)
		fprintf(log_file, " laddr=%s", message->laddr);

	if (message->lport != 0)
		fprintf(log_file, " lport=%i", message->lport);

	if (message->faddr)
		fprintf(log_file, " faddr=%s", message->faddr);

	if (message->fport != 0)
		fprintf(log_file, " fport=%i", message->fport);

	if (message->port != 0)
		fprintf(log_file, " port=%i", message->port);

	if (message->is_src_sid)
		fprintf(log_file, " ssid=%i", message->src_sid);

	if (message->is_tgt_sid)
		fprintf(log_file, " tsid=%i", message->tgt_sid);

	if (message->is_capability)
		fprintf(log_file, " capability=%i", message->capability);

	if (message->is_key)
		fprintf(log_file, " key=%i", message->key);

	if (message->is_src_con)
		fprintf(log_file, " scontext=%s:%s:%s",
			audit_log_get_user(audit_log, message->src_user),
			audit_log_get_role(audit_log, message->src_role), audit_log_get_type(audit_log, message->src_type));

	if (message->is_tgt_con)
		fprintf(log_file, " tcontext=%s:%s:%s",
			audit_log_get_user(audit_log, message->tgt_user),
			audit_log_get_role(audit_log, message->tgt_role), audit_log_get_type(audit_log, message->tgt_type));

	if (message->is_obj_class)
		fprintf(log_file, " tclass=%s", audit_log_get_obj(audit_log, message->obj_class));

	fprintf(log_file, "\n");

	return;
}

void write_load_policy_message_to_file(FILE * log_file, const load_policy_msg_t * message, const char *message_header)
{

	assert(log_file != NULL && message != NULL && message_header != NULL);

	fprintf(log_file, "%ssecurity:  %i users, %i roles, %i types, %i bools\n", message_header, message->users, message->roles,
		message->types, message->bools);
	fprintf(log_file, "%ssecurity:  %i classes, %i rules\n", message_header, message->classes, message->rules);

	return;
}

void write_boolean_message_to_file(FILE * log_file, const boolean_msg_t * message, const char *message_header,
				   audit_log_t * audit_log)
{
	int i;

	assert(log_file != NULL && message != NULL && message_header != NULL && audit_log != NULL);

	fprintf(log_file, "%ssecurity: committed booleans { ", message_header);

	for (i = 0; i < message->num_bools; i++) {
		fprintf(log_file, "%s:%i", audit_log_get_bool(audit_log, message->booleans[i]), message->values[i]);

		if ((i + 1) < message->num_bools)
			fprintf(log_file, ", ");
	}

	fprintf(log_file, " }\n");

	return;
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

void seaudit_on_PolicyFileOpen_activate(GtkWidget * widget, GdkEvent * event, gpointer callback_data)
{
	GtkWidget *file_selector;
	gint response;
	const gchar *filename;

	file_selector = gtk_file_selection_new("Open Policy");
	/* set this window to be transient window, so that when it pops up it gets centered on it */
	gtk_window_set_transient_for(GTK_WINDOW(file_selector), seaudit_app->window->window);

	gtk_file_selection_hide_fileop_buttons(GTK_FILE_SELECTION(file_selector));
	if (seaudit_app->seaudit_conf.default_policy_file != NULL)
		gtk_file_selection_complete(GTK_FILE_SELECTION(file_selector), seaudit_app->seaudit_conf.default_policy_file);

	g_signal_connect(GTK_OBJECT(file_selector), "response", G_CALLBACK(get_dialog_response), &response);
	while (1) {
		gtk_dialog_run(GTK_DIALOG(file_selector));
		if (response != GTK_RESPONSE_OK) {
			gtk_widget_destroy(file_selector);
			return;
		}
		filename = gtk_file_selection_get_filename(GTK_FILE_SELECTION(file_selector));
		if (g_file_test(filename, G_FILE_TEST_EXISTS) && !g_file_test(filename, G_FILE_TEST_IS_DIR))
			break;
		if (g_file_test(filename, G_FILE_TEST_IS_DIR))
			gtk_file_selection_complete(GTK_FILE_SELECTION(file_selector), filename);
	}
	seaudit_open_policy(seaudit_app, filename);
	gtk_widget_destroy(file_selector);
	return;
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

/* Timeout function used to keep the log up to date, always
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

#endif
