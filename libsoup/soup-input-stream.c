#include "soup-input-stream.h"

#define SOUP_INPUT_STREAM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SOUP_TYPE_INPUT_STREAM, SoupInputStreamPrivate))

struct _SoupInputStreamPrivate
{
	guint abc;
};

G_DEFINE_TYPE (SoupInputStream, soup_input_stream, G_TYPE_FILTER_INPUT_STREAM);


static gssize
soup_input_stream_read (GInputStream	 *stream,
			void		 *buffer,
			gsize		  count,
			GCancellable	 *cancellable,
			GError		**error)
{
	gssize nread = 0;

	nread = G_INPUT_STREAM_CLASS (soup_input_stream_parent_class)->
		read_fn (stream, buffer, count, cancellable, error);

	if (nread == 0)
		g_signal_emit_by_name (stream, "end-of-stream", NULL);

	return nread;
}

static gboolean
soup_input_stream_close  (GInputStream        *stream,
			  GCancellable        *cancellable,
			  GError             **error)
{
	g_signal_emit_by_name (stream, "stream-closed", NULL);

	return G_INPUT_STREAM_CLASS (soup_input_stream_parent_class)->
		close_fn (stream, cancellable, error);
}

static void
soup_input_stream_class_init (SoupInputStreamClass *klass)
{
	GInputStreamClass *input_stream_class = G_INPUT_STREAM_CLASS (klass);

	g_type_class_add_private (klass, sizeof (SoupInputStreamPrivate));

	input_stream_class->read_fn = soup_input_stream_read;
	input_stream_class->close_fn = soup_input_stream_close;

	g_signal_new ("end-of-stream",
		      SOUP_TYPE_INPUT_STREAM,
		      G_SIGNAL_RUN_FIRST,
		      0,
		      NULL,
		      NULL,
		      g_cclosure_marshal_VOID__VOID,
		      G_TYPE_NONE, 0);

	g_signal_new ("stream-closed",
		      SOUP_TYPE_INPUT_STREAM,
		      G_SIGNAL_RUN_FIRST,
		      0,
		      NULL,
		      NULL,
		      g_cclosure_marshal_VOID__VOID,
		      G_TYPE_NONE, 0);
}

static void
soup_input_stream_init (SoupInputStream *self)
{
	SoupInputStreamPrivate *priv;

	self->priv = priv = SOUP_INPUT_STREAM_GET_PRIVATE (self);
}

SoupInputStream *
soup_input_stream_new (GInputStream *base_stream)
{
	SoupInputStream *self;

	g_debug ("soup_input_stream_new called");

	self = g_object_new (SOUP_TYPE_INPUT_STREAM,
			     "base-stream", base_stream,
			     NULL);

	return self;
}
