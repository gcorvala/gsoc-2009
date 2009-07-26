#include "soup-uri-loader.h"
#include "soup-protocol.h"
#include "soup-protocol-ftp.h"

#define SOUP_URI_LOADER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SOUP_TYPE_URI_LOADER, SoupURILoaderPrivate))

struct _SoupURILoaderPrivate
{
	GHashTable *protocols;
};

G_DEFINE_TYPE (SoupURILoader, soup_uri_loader, G_TYPE_OBJECT); 

static void
soup_uri_loader_finalize (GObject *object)
{
	SoupURILoader *loader;
	SoupURILoaderPrivate *priv;

	loader = SOUP_URI_LOADER (object);
	priv = SOUP_URI_LOADER_GET_PRIVATE (loader);

	g_hash_table_destroy (priv->protocols);

	G_OBJECT_CLASS (soup_uri_loader_parent_class)->finalize (object);
}

static void
soup_uri_loader_class_init (SoupURILoaderClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	
	g_type_class_add_private (klass, sizeof (SoupURILoaderPrivate));
	
	gobject_class->finalize = soup_uri_loader_finalize;
}

static void
soup_uri_loader_init (SoupURILoader *self)
{
	SoupURILoaderPrivate *priv;

	self->priv = priv = SOUP_URI_LOADER_GET_PRIVATE (self);
	
	priv->protocols = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
}

SoupURILoader *
soup_uri_loader_new (void)
{
	SoupURILoader *self;
	
	g_debug ("soup_uri_loader_new called");

	self = g_object_new (SOUP_TYPE_URI_LOADER, NULL);
	
	return self;
}


GInputStream*
soup_uri_loader_load_uri (SoupURILoader	 *loader,
			  SoupURI	 *uri,
			  GCancellable	 *cancellable,
			  GError	**error)
{
	SoupURILoaderPrivate *priv = SOUP_URI_LOADER_GET_PRIVATE (loader);
	GInputStream *input_stream;
	SoupProtocol *protocol;

	if (uri->scheme == SOUP_URI_SCHEME_HTTP)
		g_debug ("http");

	else if (uri->scheme == SOUP_URI_SCHEME_HTTPS)
		g_debug ("https");

	else if (uri->scheme == SOUP_URI_SCHEME_FTP)
	{
		g_debug ("ftp protocol detected!");
		protocol = g_hash_table_lookup (priv->protocols, uri->scheme);
		if (protocol == NULL)
		{
			protocol = soup_protocol_ftp_new ();
			g_hash_table_insert (priv->protocols, g_strdup (uri->scheme), protocol);
		}
	}
	else
		g_debug ("error");
	
	input_stream = soup_protocol_load_uri (protocol, uri, cancellable, error);
	
	return input_stream;
}

void
soup_uri_loader_load_uri_async (SoupURILoader		*loader,
				SoupURI			*uri,
				GCancellable		*cancellable,
				GAsyncReadyCallback	 callback,
				gpointer		 user_data)
{
	SoupURILoaderPrivate *priv = SOUP_URI_LOADER_GET_PRIVATE (loader);
	SoupProtocol *protocol;
	GSimpleAsyncResult *result;

	result = g_simple_async_result_new (loader,
					    callback,
					    user_data,
					    soup_uri_loader_load_uri_async);
	protocol = g_hash_table_lookup (priv->protocols, uri->scheme);
	if (!protocol) {
		if (uri->scheme == SOUP_URI_SCHEME_HTTP)
			g_debug ("http");
		else if (uri->scheme == SOUP_URI_SCHEME_HTTPS)
			g_debug ("https");
		else if (uri->scheme == SOUP_URI_SCHEME_FTP)
			protocol = soup_protocol_ftp_new ();
		else
			g_debug ("error");
		g_hash_table_insert (priv->protocols, g_strdup (uri->scheme), protocol);
	}
	soup_protocol_load_uri_async (protocol, uri, cancellable, callback, user_data);
}
