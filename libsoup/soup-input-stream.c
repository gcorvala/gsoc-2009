#include "soup-input-stream.h"

#define SOUP_INPUT_STREAM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SOUP_TYPE_INPUT_STREAM, SoupInputStreamPrivate))

struct _SoupInputStreamPrivate
{
	guint abc;
};

G_DEFINE_TYPE (SoupInputStream, soup_input_stream, G_TYPE_OBJECT);

static void
soup_input_stream_class_init (SoupInputStreamClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (SoupInputStreamPrivate));
}

static void
soup_input_stream_init (SoupInputStream *self)
{
	SoupInputStreamPrivate *priv;

	self->priv = priv = SOUP_INPUT_STREAM_GET_PRIVATE (self);
}

SoupInputStream *
soup_input_stream_new (void)
{
	SoupInputStream *self;

	g_debug ("soup_input_stream_new called");

	self = g_object_new (SOUP_TYPE_INPUT_STREAM, NULL);

	return self;
}
