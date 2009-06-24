#include "soup-uri-loader.h"
#include "soup-protocol.h"
#include "soup-protocol-ftp.h"

#define SOUP_URI_LOADER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SOUP_TYPE_URI_LOADER, SoupURILoaderPrivate))

struct _SoupURILoaderPrivate
{
	SoupURI *uri;
};

G_DEFINE_TYPE (SoupURILoader, soup_uri_loader, G_TYPE_OBJECT); 

static void
soup_uri_loader_class_init (SoupURILoaderClass *klass)
{
	g_type_class_add_private (klass, sizeof (SoupURILoaderPrivate));
}

static void
soup_uri_loader_init (SoupURILoader *self)
{
	SoupURILoaderPrivate *priv;

	self->priv = priv = SOUP_URI_LOADER_GET_PRIVATE (self);
}

SoupURILoader *
soup_uri_loader_new (void)
{
	SoupURILoader *self;

	self = g_object_new (SOUP_TYPE_URI_LOADER, NULL);
	
	return self;
}


GInputStream*
soup_uri_loader_load_uri (SoupURILoader	 *loader,
			  SoupURI	 *uri,
			  GCancellable	 *cancellable,
			  GError	**error)
{
	GInputStream *input_stream;
	SoupProtocol *protocol;

	if (uri->scheme == SOUP_URI_SCHEME_HTTP)
		g_debug ("http");

	else if (uri->scheme == SOUP_URI_SCHEME_HTTPS)
		g_debug ("https");

	else if (g_strcmp0 (uri->scheme ,"ftp") == 0)
	{
		g_debug ("ftp protocol detected!");
		protocol = SOUP_PROTOCOL (soup_protocol_ftp_new ());
	}
	else
		g_debug ("error");
	
	input_stream = soup_protocol_load_uri (protocol, uri, cancellable, error);
	//soup_protocol_load_uri_async (protocol, uri, cancellable, NULL, NULL);
	
	return input_stream;
}
