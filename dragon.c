// dragon - very lightweight DnD file source/target
// Copyright 2014 Michael Homer.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 500
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERSION "1.2.0"

#define MAX_SIZE 100

static GtkWidget *window;
static GtkWidget *vbox;
static GtkIconTheme *icon_theme;

static char *progname;
static bool verbose = false;
static int thumb_size = 96;
static bool and_exit;
static bool keep;
static bool log_drop = false;
static bool target = false;
static bool print_path = false;
static bool icons_only = false;
static bool always_on_top = false;

#define TARGET_TYPE_TEXT 1
#define TARGET_TYPE_URI 2

struct draggable_thing {
	char *text;
	char *uri;
};

// MODE_ALL
static char **uri_collection;
static int uri_count = 0;
static bool drag_all = false;
static bool all_compact = false;
static char file_num_label[10];
static struct draggable_thing fake_dragdata;
static GtkWidget *all_button;
// ---

static void short_usage(int code)
{
	fprintf(stderr, "Usage: %s [OPTION] [FILENAME]\n", progname);
	exit(code);
}

static void usage(int code)
{
	fprintf(stderr, "dragon - lightweight DnD source/target\n");
	fprintf(stderr, "Usage: %s [OPTION] [FILENAME]\n", progname);
	fprintf(stderr, "  --and-exit,    -x  exit after a single completed drop\n");
	fprintf(stderr, "  --target,      -t  act as a target instead of source\n");
	fprintf(stderr, "  --keep,        -k  with --target, keep files to drag out\n");
	fprintf(stderr, "  --print-path,  -p  with --target, print file paths instead of URIs\n");
	fprintf(stderr, "  --all,         -a  drag all files at once\n");
	fprintf(stderr, "  --all-compact, -A  drag all files at once, only displaying the number of files\n");
	fprintf(stderr, "  --icon-only,   -i  only show icons in drag-and-drop windows\n");
	fprintf(stderr, "  --on-top,      -T  make window always-on-top\n");
	fprintf(stderr, "  --stdin,       -I  read input from stdin\n");
	fprintf(stderr, "  --thumb-size,  -s  set thumbnail size (default 96)\n");
	fprintf(stderr, "  --verbose,     -v  be verbose\n");
	fprintf(stderr, "  --log-drop,    -l  log file to stdout after it has been dropped\n");
	fprintf(stderr, "  --help            show help\n");
	fprintf(stderr, "  --version         show version details\n");
	exit(code);
}

static void version(int code)
{
	fprintf(stderr, "dragon " VERSION "\n");
	fprintf(stderr, "Copyright (C) 2014-2022 Michael Homer and contributors\n");
	fprintf(stderr, "This program comes with ABSOLUTELY NO WARRANTY.\n");
	fprintf(stderr, "See the source for copying conditions.\n");
	exit(code);
}

static void add_target_button(void);

static void do_quit(GtkWidget *widget, gpointer data)
{
	gtk_main_quit();
}

static void button_clicked(GtkWidget *widget, gpointer user_data)
{
	struct draggable_thing *dd = (struct draggable_thing *)user_data;
	pid_t pid;

	switch ((pid = fork())) {
	default:
		return;
	case -1:
		break;
	case 0:
		// Return value of setsid may be ignored
		setsid();

		// @todo: Possible file descriptor leak?

		execlp("xdg-open", "xdg-open", dd->uri, NULL);
		break;
	}

	fprintf(stderr, "Executing xdg-open for %s failed: %s\n", dd->uri, strerror(errno));
}

static void drag_data_get(GtkWidget *widget, GdkDragContext *context, GtkSelectionData *data, guint info, guint time, gpointer user_data)
{
	struct draggable_thing *dd = (struct draggable_thing *)user_data;
	if (info == TARGET_TYPE_URI) {
		char **uris;
		char *single_uri_data[2] = { dd->uri, NULL };
		if (drag_all) {
			uri_collection[uri_count] = NULL;
			uris = uri_collection;
		} else {
			uris = single_uri_data;
		}
		if (verbose) {
			if (drag_all)
				fputs("Sending all as URI\n", stderr);
			else
				fprintf(stderr, "Sending as URI: %s\n", dd->uri);
		}

		gtk_selection_data_set_uris(data, uris);
		g_signal_stop_emission_by_name(widget, "drag-data-get");
	} else if (info == TARGET_TYPE_TEXT) {
		if (verbose)
			fprintf(stderr, "Sending as TEXT: %s\n", dd->text);
		gtk_selection_data_set_text(data, dd->text, -1);
	} else {
		fprintf(stderr, "Error: bad target type %i\n", info);
	}
}

static void drag_end(GtkWidget *widget, GdkDragContext *context, gpointer user_data)
{
	struct draggable_thing *dd = (struct draggable_thing *)user_data;

	gboolean succeeded = gdk_drag_drop_succeeded(context);
	GdkDragAction action = gdk_drag_context_get_selected_action(context);

	if (verbose) {
		char *action_str = NULL;

		switch (action) {
		case GDK_ACTION_COPY:
			action_str = "COPY";
			break;
		case GDK_ACTION_MOVE:
			action_str = "MOVE";
			break;
		case GDK_ACTION_LINK:
			action_str = "LINK";
			break;
		case GDK_ACTION_ASK:
			action_str = "ASK";
			break;
		default:
			fprintf(stderr, "Selected drop action: %d (invalid); Succeeded: %d\n", action, succeeded);
			break;
		}

		if (action_str)
			fprintf(stderr, "Selected drop action: %s; Succeeded: %d\n", action_str, succeeded);
	}

	if (log_drop && succeeded && action) {
		if (print_path) {
			GFile *file = g_file_new_for_uri(dd->uri);
			char *filename = g_file_get_path(file);

			printf("%s\n", filename);

			g_free(filename);
			g_object_unref(file);
		} else {
			printf("%s\n", dd->uri);
		}
	}

	if (and_exit)
		gtk_main_quit();
}

static void add_uri(char *uri)
{
	if (uri_count < MAX_SIZE) {
		uri_collection[uri_count] = uri;
		uri_count++;
	} else {
		fprintf(stderr, "Exceeded maximum number of files for drag_all (%d)\n", MAX_SIZE);
	}
}

static GtkButton *add_button(char *label, struct draggable_thing *dragdata, int type)
{
	GtkWidget *button;

	if (icons_only) {
		button = gtk_button_new();
	} else {
		button = gtk_button_new_with_label(label);
	}

	GtkTargetList *targetlist = gtk_drag_source_get_target_list(GTK_WIDGET(button));
	if (targetlist)
		gtk_target_list_ref(targetlist);
	else
		targetlist = gtk_target_list_new(NULL, 0);
	if (type == TARGET_TYPE_URI)
		gtk_target_list_add_uri_targets(targetlist, TARGET_TYPE_URI);
	else
		gtk_target_list_add_text_targets(targetlist, TARGET_TYPE_TEXT);

	gtk_drag_source_set(GTK_WIDGET(button), GDK_BUTTON1_MASK, NULL, 0, GDK_ACTION_COPY | GDK_ACTION_LINK | GDK_ACTION_ASK);
	gtk_drag_source_set_target_list(GTK_WIDGET(button), targetlist);
	g_signal_connect(GTK_WIDGET(button), "drag-data-get", G_CALLBACK(drag_data_get), dragdata);
	g_signal_connect(GTK_WIDGET(button), "clicked", G_CALLBACK(button_clicked), dragdata);
	g_signal_connect(GTK_WIDGET(button), "drag-end", G_CALLBACK(drag_end), dragdata);

	gtk_container_add(GTK_CONTAINER(vbox), button);

	if (drag_all)
		add_uri(dragdata->uri);
	else
		uri_count++;

	return (GtkButton *)button;
}

static void left_align_button(GtkButton *button)
{
	GList *child = g_list_first(gtk_container_get_children(GTK_CONTAINER(button)));
	if (child)
		gtk_widget_set_halign(GTK_WIDGET(child->data), GTK_ALIGN_START);
}

static GtkIconInfo *icon_info_from_content_type(char *content_type)
{
	GIcon *icon = g_content_type_get_icon(content_type);
	return gtk_icon_theme_lookup_by_gicon(icon_theme, icon, 48, 0);
}

static void add_file_button(GFile *file)
{
	char *filename = g_file_get_path(file);
	if (!g_file_query_exists(file, NULL)) {
		fprintf(stderr, "The file `%s' does not exist.\n", filename);
		exit(EXIT_FAILURE);
	}
	char *uri = g_file_get_uri(file);
	if (all_compact) {
		add_uri(uri);
		return;
	}
	struct draggable_thing *dragdata = malloc(sizeof(struct draggable_thing));
	dragdata->text = filename;
	dragdata->uri = uri;

	GtkButton *button = add_button(filename, dragdata, TARGET_TYPE_URI);
	GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_size(filename, thumb_size, thumb_size, NULL);
	if (pb) {
		GtkWidget *image = gtk_image_new_from_pixbuf(pb);
		gtk_button_set_always_show_image(button, true);
		gtk_button_set_image(button, image);
		gtk_button_set_always_show_image(button, true);
	} else {
		GFileInfo *fileinfo = g_file_query_info(file, "*", 0, NULL, NULL);
		GIcon *icon = g_file_info_get_icon(fileinfo);
		GtkIconInfo *icon_info = gtk_icon_theme_lookup_by_gicon(icon_theme, icon, 48, 0);

		// Try a few fallback mimetypes if no icon can be found
		if (!icon_info)
			icon_info = icon_info_from_content_type("application/octet-stream");
		if (!icon_info)
			icon_info = icon_info_from_content_type("text/x-generic");
		if (!icon_info)
			icon_info = icon_info_from_content_type("text/plain");

		if (icon_info) {
			GtkWidget *image = gtk_image_new_from_pixbuf(gtk_icon_info_load_icon(icon_info, NULL));
			gtk_button_set_image(button, image);
			gtk_button_set_always_show_image(button, true);
		}
	}

	if (!icons_only)
		left_align_button(button);
}

static void add_uri_button(char *uri)
{
	if (all_compact) {
		add_uri(uri);
		return;
	}
	struct draggable_thing *dragdata = malloc(sizeof(struct draggable_thing));
	dragdata->text = uri;
	dragdata->uri = uri;
	GtkButton *button = add_button(uri, dragdata, TARGET_TYPE_URI);
	left_align_button(button);
}

static bool is_file_uri(char *uri)
{
	char *prefix = "file:";
	return strncmp(prefix, uri, strlen(prefix)) == 0;
}

static gboolean drag_drop(GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint time, gpointer user_data)
{
	GtkTargetList *targetlist = gtk_drag_dest_get_target_list(widget);
	GList *list = gdk_drag_context_list_targets(context);
	if (list) {
		while (list) {
			GdkAtom atom = (GdkAtom)g_list_nth_data(list, 0);
			if (gtk_target_list_find(targetlist, GDK_POINTER_TO_ATOM(g_list_nth_data(list, 0)), NULL)) {
				gtk_drag_get_data(widget, context, atom, time);
				return true;
			}
			list = g_list_next(list);
		}
	}

	gtk_drag_finish(context, false, false, time);
	return true;
}

static void update_all_button(void)
{
	sprintf(file_num_label, "%d files", uri_count);
	gtk_button_set_label((GtkButton *)all_button, file_num_label);
}

static void drag_data_received(GtkWidget *widget, GdkDragContext *context, gint x, gint y, GtkSelectionData *data, guint info, guint time)
{
	gchar **uris = gtk_selection_data_get_uris(data);
	unsigned char *text = gtk_selection_data_get_text(data);
	if (!uris && !text)
		gtk_drag_finish(context, FALSE, FALSE, time);
	if (uris) {
		if (verbose)
			fputs("Received URIs\n", stderr);
		gtk_container_remove(GTK_CONTAINER(vbox), widget);
		for (; *uris; uris++) {
			if (is_file_uri(*uris)) {
				GFile *file = g_file_new_for_uri(*uris);

				if (print_path) {
					char *filename = g_file_get_path(file);
					printf("%s\n", filename);
					g_free(filename);
				} else {
					printf("%s\n", *uris);
				}

				if (keep)
					add_file_button(file);

				g_object_unref(file);
			} else {
				printf("%s\n", *uris);
				if (keep)
					add_uri_button(*uris);
			}
		}
		if (all_compact)
			update_all_button();
		add_target_button();
		gtk_widget_show_all(window);
	} else if (text) {
		if (verbose)
			fputs("Received Text\n", stderr);
		printf("%s\n", text);
	} else if (verbose)
		fputs("Received nothing\n", stderr);
	gtk_drag_finish(context, TRUE, FALSE, time);
	if (and_exit)
		gtk_main_quit();
}

static void add_target_button(void)
{
	GtkWidget *label = gtk_button_new();
	gtk_button_set_label(GTK_BUTTON(label), "Drag something here...");
	gtk_container_add(GTK_CONTAINER(vbox), label);
	GtkTargetList *targetlist = gtk_drag_dest_get_target_list(GTK_WIDGET(label));
	if (targetlist)
		gtk_target_list_ref(targetlist);
	else
		targetlist = gtk_target_list_new(NULL, 0);
	gtk_target_list_add_text_targets(targetlist, TARGET_TYPE_TEXT);
	gtk_target_list_add_uri_targets(targetlist, TARGET_TYPE_URI);
	gtk_drag_dest_set(GTK_WIDGET(label), GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT, NULL, 0, GDK_ACTION_COPY);
	gtk_drag_dest_set_target_list(GTK_WIDGET(label), targetlist);
	g_signal_connect(GTK_WIDGET(label), "drag-drop", G_CALLBACK(drag_drop), NULL);
	g_signal_connect(GTK_WIDGET(label), "drag-data-received", G_CALLBACK(drag_data_received), NULL);
}

static void make_btn(char *filename)
{
	bool valid_uri = g_uri_is_valid(filename, G_URI_FLAGS_PARSE_RELAXED, NULL);

	if (valid_uri && !is_file_uri(filename))
		return add_uri_button(filename);

	GFile *file;

	if (!valid_uri) {
		file = g_file_new_for_path(filename);
	} else if (is_file_uri(filename)) {
		file = g_file_new_for_uri(filename);
	}

	add_file_button(file);
	g_object_unref(file);
}

static bool read_file_list(FILE *fp)
{
	bool rc = false;
	size_t offset = 0;
	size_t bufsize = 2;
	char *buf = malloc(bufsize);

	if (buf == NULL) {
		perror("Failed to allocate memory for file list");
		goto error;
	}

	while ((fgets(buf + offset, bufsize - offset, fp))) {
		size_t total_size = offset + strlen(buf + offset);

		// total_size > 0 ensured by fgets if BUFSIZ > 1
		if (buf[total_size - 1] == '\n') {
			buf[total_size - 1] = '\0';
			total_size--;
		}

		// allocate more
		if (total_size >= bufsize - 1) {
			bufsize += BUFSIZ;

			char *new_buf = realloc(buf, bufsize);
			if (new_buf == NULL) {
				perror("Failed to realloc memory for file list");
				goto error;
			}

			offset = total_size;
			buf = new_buf;
			continue;
		}

		if (total_size == 0) {
			continue;
		}

		make_btn(buf);
	}

	if (ferror(stdin)) {
		perror("Failed to read stdin");
		goto error;
	}

	rc = true;

error:
	free(buf);
	return rc;
}

static void create_all_button(void)
{
	sprintf(file_num_label, "%d files", uri_count);
	all_button = gtk_button_new_with_label(file_num_label);

	GtkTargetList *targetlist = gtk_target_list_new(NULL, 0);
	gtk_target_list_add_uri_targets(targetlist, TARGET_TYPE_URI);

	// fake uri to avoid segfault when callback deference it
	fake_dragdata.uri = file_num_label;

	gtk_drag_source_set(GTK_WIDGET(all_button), GDK_BUTTON1_MASK, NULL, 0, GDK_ACTION_COPY | GDK_ACTION_LINK | GDK_ACTION_ASK);
	gtk_drag_source_set_target_list(GTK_WIDGET(all_button), targetlist);
	g_signal_connect(GTK_WIDGET(all_button), "drag-data-get", G_CALLBACK(drag_data_get), &fake_dragdata);
	g_signal_connect(GTK_WIDGET(all_button), "drag-end", G_CALLBACK(drag_end), &fake_dragdata);

	gtk_container_add(GTK_CONTAINER(vbox), all_button);
}

int main(int argc, char **argv)
{
	bool from_stdin = false;
	progname = argv[0];
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0) {
			usage(0);
		} else if (strcmp(argv[i], "--version") == 0) {
			version(0);
		} else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--log-drop") == 0) {
			log_drop = true;
		} else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
			verbose = true;
		} else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--target") == 0) {
			target = true;
		} else if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--and-exit") == 0) {
			and_exit = true;
		} else if (strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--keep") == 0) {
			keep = true;
		} else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--print-path") == 0) {
			print_path = true;
		} else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0) {
			drag_all = true;
		} else if (strcmp(argv[i], "-A") == 0 || strcmp(argv[i], "--all-compact") == 0) {
			drag_all = true;
			all_compact = true;
		} else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--icon-only") == 0) {
			icons_only = true;
		} else if (strcmp(argv[i], "-T") == 0 || strcmp(argv[i], "--on-top") == 0) {
			always_on_top = true;
		} else if (strcmp(argv[i], "-I") == 0 || strcmp(argv[i], "--stdin") == 0) {
			from_stdin = true;
		} else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--thumb-size") == 0) {
			if (argv[++i] == NULL || (thumb_size = atoi(argv[i])) <= 0) {
				fprintf(stderr, "%s: error: bad argument for %s `%s'.\n", progname, argv[i - 1], argv[i]);
				exit(EXIT_FAILURE);
			}
			argv[i][0] = '\0';
		} else if (argv[i][0] == '-') {
			fprintf(stderr, "%s: error: unknown option `%s'.\n", progname, argv[i]);
		}
	}
	setvbuf(stdout, NULL, _IOLBF, BUFSIZ);

	GtkAccelGroup *accelgroup;
	GClosure *closure;

	gtk_init(&argc, &argv);

	icon_theme = gtk_icon_theme_get_default();

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	closure = g_cclosure_new(G_CALLBACK(do_quit), NULL, NULL);
	accelgroup = gtk_accel_group_new();
	gtk_accel_group_connect(accelgroup, GDK_KEY_Escape, 0, 0, closure);
	closure = g_cclosure_new(G_CALLBACK(do_quit), NULL, NULL);
	gtk_accel_group_connect(accelgroup, GDK_KEY_q, 0, 0, closure);
	gtk_window_add_accel_group(GTK_WINDOW(window), accelgroup);

	gtk_window_set_title(GTK_WINDOW(window), "Run");
	gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
	gtk_window_set_keep_above(GTK_WINDOW(window), always_on_top);

	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

	gtk_container_add(GTK_CONTAINER(window), vbox);

	gtk_window_set_title(GTK_WINDOW(window), "dragon");

	if (all_compact)
		create_all_button();

	if (target) {
		if (drag_all)
			uri_collection = malloc(sizeof(char *) * (MAX_SIZE + 1));

		add_target_button();
	} else {
		if (from_stdin)
			uri_collection = malloc(sizeof(char *) * (MAX_SIZE + 1));
		else if (drag_all)
			uri_collection = malloc(sizeof(char *) * ((argc > MAX_SIZE ? argc : MAX_SIZE) + 1));

		for (int i = 1; i < argc; i++) {
			if (argv[i][0] != '-' && argv[i][0] != '\0')
				make_btn(argv[i]);
		}

		if (from_stdin && !read_file_list(stdin))
			return EXIT_FAILURE;

		if (!uri_count)
			short_usage(EXIT_FAILURE);

		if (all_compact)
			update_all_button();
	}

	gtk_widget_show_all(window);
	gtk_main();

	if (fflush(stdout) == EOF) {
		fprintf(stderr, "Unable to write some data to stdout\n");
		return EXIT_FAILURE;
	}

	free(uri_collection);
	return EXIT_SUCCESS;
}
