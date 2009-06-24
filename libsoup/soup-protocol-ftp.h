#ifndef  SOUP_PROTOCOL_FTP_H
#define  SOUP_PROTOCOL_FTP_H

#include "libsoup/soup-protocol.h"
#include "libsoup/soup-uri.h"
#include <gio/gio.h>

G_BEGIN_DECLS

#define SOUP_TYPE_PROTOCOL_FTP            (soup_protocol_ftp_get_type ())
#define SOUP_PROTOCOL_FTP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_PROTOCOL_FTP, SoupProtocolFtp))
#define SOUP_PROTOCOL_FTP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_PROTOCOL_FTP, SoupProtocolFtp))
#define SOUP_IS_PROTOCOL_FTP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_PROTOCOL_FTP))
#define SOUP_IS_PROTOCOL_FTP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_PROTOCOL_FTP))
#define SOUP_PROTOCOL_FTP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_PROTOCOL_FTP, SoupProtocolFtpClass))

typedef struct _SoupProtocolFtp SoupProtocolFtp;
typedef struct _SoupProtocolFtpClass SoupProtocolFtpClass;
typedef struct _SoupProtocolFtpPrivate SoupProtocolFtpPrivate;
typedef struct _SoupProtocolFtpReply SoupProtocolFtpReply;

struct _SoupProtocolFtp {
	SoupProtocol parent;
	/* < private > */
	SoupProtocolFtpPrivate *priv;
};

struct _SoupProtocolFtpClass {
	SoupProtocolClass parent;
};

GType		 soup_protocol_ftp_get_type			(void);

SoupProtocolFtp	*soup_protocol_ftp_new				(void);

GInputStream	*soup_protocol_ftp_load_uri    (SoupProtocol		*protocol,
					  	SoupURI			*uri,
						GCancellable		*cancellable,
						GError		       **error);

void		 soup_protocol_ftp_load_uri_async (SoupProtocol		*protocol,
						   SoupURI		*uri,
						   GCancellable		*cancellable,
						   GAsyncReadyCallback	 callback,
						   gpointer		 user_data);

GInputStream	*soup_protocol_ftp_load_uri_finish (SoupProtocol	*protocol,
						    GAsyncResult	*result,
						    GError	       **error);

G_END_DECLS

#endif /*SOUP_URI_PROTOCOL_H*/
