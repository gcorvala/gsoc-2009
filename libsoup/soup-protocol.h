#ifndef  SOUP_PROTOCOL_H
#define  SOUP_PROTOCOL_H

#include <libsoup/soup-types.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define SOUP_TYPE_PROTOCOL            (soup_protocol_get_type ())
#define SOUP_PROTOCOL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_PROTOCOL, SoupProtocol))
#define SOUP_PROTOCOL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_PROTOCOL, SoupProtocolClass))
#define SOUP_IS_PROTOCOL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_PROTOCOL))
#define SOUP_IS_PROTOCOL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_PROTOCOL))
#define SOUP_PROTOCOL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_PROTOCOL, SoupProtocolClass))

typedef struct _SoupProtocol SoupProtocol;
typedef struct _SoupProtocolClass SoupProtocolClass;

struct _SoupProtocol {
	GObject parent;
};

struct _SoupProtocolClass {
	GObjectClass parent;

	/* protocol methods */
	GInputStream *(*load_uri)	 (SoupProtocol		 *protocol,
					  SoupURI		 *uri,
					  GCancellable		 *cancellable,
					  GError			**error);
	void	      (*load_uri_async)	 (SoupProtocol		 *protocol,
					  SoupURI		 *uri,
					  GCancellable		 *cancellable,
					  GAsyncReadyCallback	  callback,
					  gpointer		  user_data);
	GInputStream *(*load_uri_finish) (SoupProtocol		 *protocol,
					  GAsyncResult		 *result,
					  GError		**error);
	gboolean      (*can_load_uri)	 (SoupProtocol		 *protocol,
					  SoupURI		 *uri);
};

GType       	 soup_protocol_get_type	       (void);

GInputStream	*soup_protocol_load_uri	       (SoupProtocol		*protocol,
					  	SoupURI			*uri,
						GCancellable		*cancellable,
						GError		       **error);

void		 soup_protocol_load_uri_async  (SoupProtocol		*protocol,
						SoupURI			*uri,
						GCancellable		*cancellable,
						GAsyncReadyCallback	 callback,
						gpointer		 user_data);

GInputStream	*soup_protocol_load_uri_finish (SoupProtocol		*protocol,
						GAsyncResult		*result,
						GError		       **error);

gboolean	 soup_protocol_can_load_uri    (SoupProtocol		*protocol,
						SoupURI			*uri);

G_END_DECLS

#endif /*SOUP_PROTOCOL_H*/
