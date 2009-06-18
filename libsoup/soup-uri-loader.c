#include "soup-uri-loader.h"

#define SOUP_URI_LOADER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SOUP_TYPE_URI_LOADER, SoupURILoaderPrivate))

struct _SoupURILoaderPrivate
{
	SoupURI *uri;
};

G_DEFINE_TYPE (SoupURILoader, soup_uri_loader, G_TYPE_OBJECT); 

static void
soup_uri_loader_class_init (SoupURILoaderClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (SoupURILoaderPrivate));
}

static void
soup_uri_loader_init (SoupURILoader *self)
{
	SoupURILoaderPrivate *priv;

	self->priv = priv = SOUP_URI_LOADER_GET_PRIVATE (self);
}

GInputStream*
soup_uri_loader_load_uri (SoupURILoader	 *loader,
						  SoupURI		 *uri,
						  GCancellable	 *cancellable,
						  GError		**err)
{
	GInputStream *input_stream;
	
	return input_stream;
}
