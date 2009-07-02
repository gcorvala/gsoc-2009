#include "libsoup/soup.h"
#include "libsoup/soup-uri.h"
#include "libsoup/soup-uri-loader.h"

#include <glib.h>
#include <gio/gio.h>

int
main (int argc, char **argv)
{
	SoupURILoader *loader;
	SoupURI *uri;
	GError *error = NULL;
	GInputStream *input;
	GDataInputStream *data;
	gchar *buffer;
	gsize len;

	g_type_init ();

	/**
	 * Construct SoupURI
	 **/
	uri = soup_uri_new ("ftp://anonymous:abc@ftp.gnome.org:21/about/index.html");

	/**
	 * Construct SoupURILoader
	 **/
	loader = soup_uri_loader_new ();
	input = soup_uri_loader_load_uri (loader, uri, NULL, &error);
	
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

	g_debug ("buffer_length  = %u", len);
	g_debug ("buffer_content = %s", buffer);

	return 0;
}
