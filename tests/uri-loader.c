#include "libsoup/soup.h"
#include "libsoup/soup-uri.h"
#include "libsoup/soup-uri-loader.h"

#include <glib.h>
#include <gio/gio.h>

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
	uri1 = soup_uri_new ("ftp://anonymous:abc@ftp.kernel.org:21/welcome.msg");
	uri2 = soup_uri_new ("ftp://anonymous:abc@ftp.gnome.org:21/welcome.msg");
	uri3 = soup_uri_new ("ftp://anonymous:ab@ftp.gnome.org:21/welcome2.msg");
	/**
	 * Construct SoupURILoader
	 **/
	loader = soup_uri_loader_new ();
	input = soup_uri_loader_load_uri (loader, uri1, NULL, &error);
	
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
		g_debug ("<--- %s", buffer);
		buffer = g_data_input_stream_read_line (data, &len, NULL, NULL);
	}

	return 0;
}
