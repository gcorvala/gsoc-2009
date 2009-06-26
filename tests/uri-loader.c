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

	g_type_init ();
	g_thread_init (NULL);

	uri = soup_uri_new ("ftp://anonymous:abc@ftp.gnome.org:21/about/index.html");
	g_print ("uri-scheme - %s\n", uri->scheme);
	g_print ("uri-user   - %s\n", uri->user);
	g_print ("uri-pass   - %s\n", uri->password);
	g_print ("uri-host   - %s\n", uri->host);
	g_print ("uri-port   - %u\n", uri->port);
	g_print ("uri-path   - %s\n", uri->path);
	g_print ("uri-query  - %s\n", uri->query);
	g_print ("uri-frag   - %s\n", uri->fragment);

	loader = soup_uri_loader_new ();
	
	GInputStream *test = soup_uri_loader_load_uri (loader, uri, NULL, &error);
	
	if (error)
	{
		g_debug ("error->code : %u\nerror->message : %s", error->code, error->message);
		return 1;
	}
	
	GDataInputStream *data = g_data_input_stream_new (test);
	gsize len;
	gchar *buffer = g_data_input_stream_read_line       (data, &len, NULL, NULL);

	g_debug ("-len = %u\n-buffer = \n%s", len, buffer);

	return 0;
}
