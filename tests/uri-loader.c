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

	g_type_init ();
	g_thread_init (NULL);

	uri = soup_uri_new ("ftp://user:pass@127.0.0.1:21000/rep1/");
	g_print ("uri-scheme - %s\n", uri->scheme);
	g_print ("uri-user   - %s\n", uri->user);
	g_print ("uri-pass   - %s\n", uri->password);
	g_print ("uri-host   - %s\n", uri->host);
	g_print ("uri-port   - %u\n", uri->port);
	g_print ("uri-path   - %s\n", uri->path);
	g_print ("uri-query  - %s\n", uri->query);
	g_print ("uri-frag   - %s\n", uri->fragment);

	loader = soup_uri_loader_new ();
	
	soup_uri_loader_load_uri (loader, uri, NULL, NULL);

	return 0;
}
