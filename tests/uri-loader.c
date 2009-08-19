#include "libsoup/soup.h"

#include <glib.h>
#include <gio/gio.h>

void
callback (GObject *source,
	  GAsyncResult *res,
	  gpointer user_data)
{
	SoupURILoader *loader;
	GInputStream *input;
	GDataInputStream *data;
	gchar *buffer;
	gsize len;
	GError *error = NULL;

	g_debug ("user callback called");

	g_warn_if_fail (SOUP_IS_URI_LOADER (source));

	loader = SOUP_URI_LOADER (source);
	input = soup_uri_loader_load_uri_finish (source, res, &error);

	if (input) {
		data = g_data_input_stream_new (input);

		buffer = g_data_input_stream_read_line (data, &len, NULL, NULL);

		while (buffer != NULL) {
			g_debug ("[async] <--- %s", buffer);
			g_free (buffer);
			buffer = g_data_input_stream_read_line (data, &len, NULL, NULL);
		}
		g_object_unref (data);
	}
	else {
		g_debug ("error detected : [%u] %s", error->code, error->message);
	}
	g_object_unref (loader);
	exit (0);
}

void
display_directory (gpointer data,
		   gpointer user_data)
{
	GFileInfo *info;

	g_return_if_fail (G_IS_FILE_INFO (data));

	info = G_FILE_INFO (data);
	g_debug ("[%c] %25s %60u Bytes",
		 g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY ? 'd' : 'f',
		 g_file_info_get_name (info),
		 g_file_info_get_size (info));
}

int
main (int argc, char **argv)
{
	SoupURILoader *loader;
	SoupURI *uri1, *uri2, *uri3, *uri4, *uri5;
	GError *error = NULL;
	GInputStream *input;
	GDataInputStream *data;
	gchar *buffer;
	gsize len;
	gssize count;
	GFileInfo *info;
	GList *file_list = NULL;

	g_type_init ();
	g_thread_init (NULL);

	/**
	 * Construct SoupURI's.
	 * Every cases are define here.
	 * 1 - URI seems to point on a file -> it's a file
	 * 2 - URI seems to point on a file -> it's a directory
	 * 3 - URI seems to point on a directory -> it's a directory
	 **/
	uri1 = soup_uri_new ("ftp://anonymous:anonymous@tgftp.nws.noaa.gov/README.TXT");
	uri2 = soup_uri_new ("ftp://anonymous:abc@ftp.gnome.org/about");
	uri3 = soup_uri_new ("ftp://anonymous:abc@ftp.gnome.org/");
	uri4 = soup_uri_new ("file:///proc/meminfo");
	uri5 = soup_uri_new ("file:///proc/cpuinfo");
	/**
	 * Construct SoupURILoader
	 **/
	loader = soup_uri_loader_new ();

	/**
	 * Test sync
	 **/
	input = soup_uri_loader_load_uri (loader, uri1, NULL, &error);
	g_object_get (input,
		      "file-info", &info,
		      NULL);
	if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
		g_object_get (input,
			      "children", &file_list,
			      NULL);
		g_list_foreach (file_list,
				display_directory,
				NULL);
	}
	else {
		data = g_data_input_stream_new (input);
		buffer = g_data_input_stream_read_line (data, &len, NULL, NULL);
		while (buffer != NULL) {
			g_debug ("[sync] <--- %s", buffer);
			buffer = g_data_input_stream_read_line (data, &len, NULL, NULL);
		}
		g_object_unref (data);
	}

	input = soup_uri_loader_load_uri (loader, uri2, NULL, &error);
	g_object_get (input,
		      "file-info", &info,
		      NULL);
	if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
		g_object_get (input,
			      "children", &file_list,
			      NULL);
		g_list_foreach (file_list,
				display_directory,
				NULL);
	}
	else {
		data = g_data_input_stream_new (input);
		buffer = g_data_input_stream_read_line (data, &len, NULL, NULL);
		while (buffer != NULL) {
			g_debug ("[sync] <--- %s", buffer);
			buffer = g_data_input_stream_read_line (data, &len, NULL, NULL);
		}
		g_object_unref (data);
	}

	input = soup_uri_loader_load_uri (loader, uri3, NULL, &error);
	g_object_get (input,
		      "file-info", &info,
		      NULL);
	if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
		g_object_get (input,
			      "children", &file_list,
			      NULL);
		g_list_foreach (file_list,
				display_directory,
				NULL);
	}
	else {
		data = g_data_input_stream_new (input);
		buffer = g_data_input_stream_read_line (data, &len, NULL, NULL);
		while (buffer != NULL) {
			g_debug ("[sync] <--- %s", buffer);
			buffer = g_data_input_stream_read_line (data, &len, NULL, NULL);
		}
		g_object_unref (data);
	}

	input = soup_uri_loader_load_uri (loader, uri4, NULL, &error);
	data = g_data_input_stream_new (input);
	buffer = g_data_input_stream_read_line (data, &len, NULL, NULL);
	while (buffer != NULL) {
		g_debug ("[sync] <--- %s", buffer);
		buffer = g_data_input_stream_read_line (data, &len, NULL, NULL);
	}
	g_object_unref (data);

	input = soup_uri_loader_load_uri (loader, uri5, NULL, &error);
	data = g_data_input_stream_new (input);
	buffer = g_data_input_stream_read_line (data, &len, NULL, NULL);
	while (buffer != NULL) {
		g_debug ("[sync] <--- %s", buffer);
		buffer = g_data_input_stream_read_line (data, &len, NULL, NULL);
	}
	g_object_unref (data);

	/**
	 * Test async
	 **/

	//GMainLoop *loop = g_main_loop_new (NULL, TRUE);

	//soup_uri_loader_load_uri_async (loader,
					//uri2,
					//NULL,
					//callback,
					//NULL);

	//g_main_loop_run (loop);

	return 0;
}
