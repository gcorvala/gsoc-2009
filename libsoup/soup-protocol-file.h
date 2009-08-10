#ifndef  SOUP_PROTOCOL_FILE_H
#define  SOUP_PROTOCOL_FILE_H

#include "libsoup/soup-protocol.h"
#include "libsoup/soup-uri.h"
#include <gio/gio.h>

G_BEGIN_DECLS

#define SOUP_TYPE_PROTOCOL_FILE            (soup_protocol_file_get_type ())
#define SOUP_PROTOCOL_FILE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_PROTOCOL_FILE, SoupProtocolFile))
#define SOUP_PROTOCOL_FILE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_PROTOCOL_FILE, SoupProtocolFile))
#define SOUP_IS_PROTOCOL_FILE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_PROTOCOL_FILE))
#define SOUP_IS_PROTOCOL_FILE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_PROTOCOL_FILE))
#define SOUP_PROTOCOL_FILE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_PROTOCOL_FILE, SoupProtocolFileClass))

typedef struct _SoupProtocolFile SoupProtocolFile;
typedef struct _SoupProtocolFileClass SoupProtocolFileClass;

struct _SoupProtocolFile {
	SoupProtocol parent;

};

struct _SoupProtocolFileClass {
	SoupProtocolClass parent;

};

GType		 soup_protocol_file_get_type (void);

SoupProtocol	*soup_protocol_file_new	    (void);

G_END_DECLS

#endif /*SOUP_URI_PROTOCOL_H*/
