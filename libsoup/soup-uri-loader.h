#ifndef  SOUP_URI_LOADER_H
#define  SOUP_URI_LOADER_H

#include <gio/gio.h>
#include "soup-uri.h"
#include "soup-protocol.h"

G_BEGIN_DECLS

#define SOUP_TYPE_URI_LOADER            (soup_uri_loader_get_type ())
#define SOUP_URI_LOADER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_URI_LOADER, SoupURILoader))
#define SOUP_URI_LOADER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_URI_LOADER, SoupURILoaderClass))
#define SOUP_IS_URI_LOADER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_URI_LOADER))
#define SOUP_IS_URI_LOADER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_URI_LOADER))
#define SOUP_URI_LOADER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_URI_LOADER, SoupURILoaderClass))

typedef struct _SoupURILoader SoupURILoader;
typedef struct _SoupURILoaderClass SoupURILoaderClass;
typedef struct _SoupURILoaderPrivate SoupURILoaderPrivate;

struct _SoupURILoader {
	GObject parent;
	/* < private > */
	SoupURILoaderPrivate *priv;
};

struct _SoupURILoaderClass {
	GObjectClass parent;
	/* class members */
};

GType		 soup_uri_loader_get_type		 (void);

SoupURILoader	*soup_uri_loader_new		 (void);

gboolean	 soup_uri_loader_add_protocol	 (SoupURILoader	 *loader,
						  gchar		 *scheme,
						  GType		  type);

GInputStream	*soup_uri_loader_load_uri	 (SoupURILoader	 *loader,
						  SoupURI	 *uri,
						  GCancellable	 *cancellable,
						  GError	**error);

void		 soup_uri_loader_load_uri_async	 (SoupURILoader		*loader,
						  SoupURI		*uri,
						  GCancellable		*cancellable,
						  GAsyncReadyCallback	callback,
						  gpointer		user_data);

GInputStream	*soup_uri_loader_load_uri_finish (SoupURILoader	 *loader,
						  GAsyncResult	 *result,
						  GError	**error);

G_END_DECLS

#endif /*SOUP_URI_LOADER_H*/
