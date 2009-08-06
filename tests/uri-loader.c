#include "libsoup/soup.h"
#include "libsoup/soup-uri.h"
#include "libsoup/soup-uri-loader.h"
#include "libsoup/soup-input-stream.h"

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

void test (GObject *stream, gpointer user_data) { g_debug ("test called"); }

int
main (int argc, char **argv)
{
	SoupURILoader *loader;
	SoupURI *uri1, *uri2, *uri3;
	GError *error = NULL;
	GInputStream *input;
	GDataInputStream *data;
	SoupInputStream *soup_input;
	gchar buffer[65];
	gsize len;
	gssize count;

	g_type_init ();
	g_thread_init (NULL);

	/**
	 * Construct SoupURI
	 **/
	uri1 = soup_uri_new ("ftp://anonymous:abc@ftp.kernel.org/pub/linux/kernel/v2.6/ChangeLog-2.6.30");
	uri2 = soup_uri_new ("ftp://anonymous:abc@ftp.gnome.org/welcome.msg");
	uri3 = soup_uri_new ("ftp://anonymous:abc@ftp.kernel.org/welcome.msg");
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
	soup_input = soup_input_stream_new (input);
	g_signal_connect (soup_input, "end-of-stream", test, NULL);
	//data = g_data_input_stream_new (input);

	//buffer = g_data_input_stream_read_line (data, &len, NULL, NULL);
	count = soup_input_stream_read (soup_input, buffer, 64, NULL, NULL);
	buffer[64] = 0;

	//while (buffer != NULL) {
	while (count > 0) {
		g_debug ("[sync] <--- %s ---", buffer);
		//buffer = g_data_input_stream_read_line (data, &len, NULL, NULL);
		count = soup_input_stream_read (soup_input, buffer, 64, NULL, NULL);
	}

	//g_object_unref (data);

	/**
	 * test async
	 **/

	//GMainLoop *loop = g_main_loop_new (NULL, TRUE);

	//soup_uri_loader_load_uri_async (loader,
					//uri3,
					//NULL,
					//callback,
					//NULL);

	//g_main_loop_run (loop);

	return 0;
}
