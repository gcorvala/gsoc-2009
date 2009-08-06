#ifndef  SOUP_INPUT_STREAM_H
#define  SOUP_INPUT_STREAM_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define SOUP_TYPE_INPUT_STREAM            (soup_input_stream_get_type ())
#define SOUP_INPUT_STREAM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_INPUT_STREAM, SoupInputStream))
#define SOUP_INPUT_STREAM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_INPUT_STREAM, SoupInputStreamClass))
#define SOUP_IS_INPUT_STREAM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_INPUT_STREAM))
#define SOUP_IS_INPUT_STREAM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_INPUT_STREAM))
#define SOUP_INPUT_STREAM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_INPUT_STREAM, SoupInputStreamClass))

typedef struct _SoupInputStream SoupInputStream;
typedef struct _SoupInputStreamClass SoupInputStreamClass;
typedef struct _SoupInputStreamPrivate SoupInputStreamPrivate;

struct _SoupInputStream {
  GObject parent;
  /* < private > */
  SoupInputStreamPrivate *priv;
};

struct _SoupInputStreamClass {
  GObjectClass parent;
  /* class members */
};

GType		 ftp_server_get_type		 (void);

SoupInputStream	*soup_uri_loader_new		 (void);

G_END_DECLS

#endif /*SOUP_INPUT_STREAM_H*/
