#include "libsoup/soup.h"
#include "libsoup/soup-uri.h"
#include "libsoup/soup-uri-loader.h"

#include <glib.h>
#include <gio/gio.h>

void
callback (GObject *source,
	  GAsyncResult *res,
	  gpointer user_data)
{
	GInputStream *input;
	GDataInputStream *data;
	gchar *buffer;
	gsize len;
	GError *error = NULL;

	g_debug ("user callback called");

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
}

int
main (int argc, char **argv)
{
	SoupURILoader *loader;
	SoupURI *uri1, *uri2, *uri3;
	GError *error = NULL;
	GInputStream *input;
	GDataInputStream *data;
	gchar *buffer;
	gsize len;

	g_type_init ();
	g_thread_init (NULL);

	/**
	 * Construct SoupURI
	 **/
	uri1 = soup_uri_new ("ftp://anonymous:abc@ftp.kernel.org/pub/linux/kernel/v2.6/ChangeLog-2.6.30");
	uri2 = soup_uri_new ("ftp://anonymous:abc@ftp.gnome.org/welcome.msg");
	uri3 = soup_uri_new ("ftp://anonymous:abc@ftp.gnome.org/welcome2.msg");
	/**
	 * Construct SoupURILoader
	 **/
	loader = soup_uri_loader_new ();
	input = soup_uri_loader_load_uri (loader, uri2, NULL, &error);
	
	/**
	 * SoupURILoader failed
	 **/
	if (error)
	{
		g_debug ("error->code : %u", error->code);
		g_debug ("error->message : %s", error->message);
		return 1;
	}

	/**
	 * SoupURILoader success
	 * Try to read GInputStream content
	 **/
	data = g_data_input_stream_new (input);

	buffer = g_data_input_stream_read_line (data, &len, NULL, NULL);

	while (buffer != NULL) {
		g_debug ("[sync] <--- %s", buffer);
		buffer = g_data_input_stream_read_line (data, &len, NULL, NULL);
	}

	g_object_unref (data);

	/**
	 * test async
	 **/

	GMainLoop *loop = g_main_loop_new (NULL, TRUE);

	soup_uri_loader_load_uri_async (loader,
					uri3,
					NULL,
					callback,
					NULL);

	g_main_loop_run (loop);

	return 0;
}
