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

GType		 soup_protocol_ftp_get_type (void);

#define SOUP_PROTOCOL_FTP_ERROR soup_protocol_ftp_error_quark()

SoupProtocolFtp	*soup_protocol_ftp_new	    (void);

G_END_DECLS

#endif /*SOUP_URI_PROTOCOL_H*/
