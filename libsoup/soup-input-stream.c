#include "soup-input-stream.h"

#define SOUP_INPUT_STREAM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SOUP_TYPE_INPUT_STREAM, SoupInputStreamPrivate))

enum {
	PROP_0,
	PROP_FILE_INFO,
	PROP_CHILDREN
};

struct _SoupInputStreamPrivate
{
	GFileInfo *info;
	GList *children;
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
soup_input_stream_set_property (GObject         *object,
				guint            prop_id,
				const GValue    *value,
				GParamSpec      *pspec)
{
	SoupInputStream *soup_stream;
	SoupInputStreamPrivate *priv;
	GObject *obj;

	soup_stream = SOUP_INPUT_STREAM (object);
	priv = SOUP_INPUT_STREAM_GET_PRIVATE (soup_stream);

	switch (prop_id) {
		case PROP_FILE_INFO:
			obj = g_value_dup_object (value);
			priv->info = G_FILE_INFO (obj);
			break;
		case PROP_CHILDREN:
			priv->children = g_value_get_pointer (value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
soup_input_stream_get_property (GObject    *object,
				guint       prop_id,
				GValue     *value,
				GParamSpec *pspec)
{
	SoupInputStream *soup_stream;
	SoupInputStreamPrivate *priv;

	soup_stream = SOUP_INPUT_STREAM (object);
	priv = SOUP_INPUT_STREAM_GET_PRIVATE (soup_stream);

	switch (prop_id) {
		case PROP_FILE_INFO:
			g_value_set_object (value, priv->info);
			break;
		case PROP_CHILDREN:
			g_value_set_pointer (value, priv->children);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
soup_input_stream_class_init (SoupInputStreamClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GInputStreamClass *input_stream_class = G_INPUT_STREAM_CLASS (klass);

	g_type_class_add_private (klass, sizeof (SoupInputStreamPrivate));

	object_class->get_property = soup_input_stream_get_property;
	object_class->set_property = soup_input_stream_set_property;

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

	g_object_class_install_property (object_class,
					 PROP_FILE_INFO,
					 g_param_spec_object ("file-info",
							      "The File Informations",
							      "Stores information about a file referenced by a GInputStream.",
							      G_TYPE_FILE_INFO,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
							      G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB));

	g_object_class_install_property (object_class,
					 PROP_CHILDREN,
					 g_param_spec_pointer ("children",
							       "Directory's Children",
							       "Stores a list of GFileInfo",
							       G_PARAM_READWRITE |
							       G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB));
}

static void
soup_input_stream_init (SoupInputStream *self)
{
	SoupInputStreamPrivate *priv;

	self->priv = priv = SOUP_INPUT_STREAM_GET_PRIVATE (self);
}

SoupInputStream *
soup_input_stream_new (GInputStream	*base_stream,
		       GFileInfo	*file_info,
		       GList		*children)
{
	SoupInputStream *self;

	self = g_object_new (SOUP_TYPE_INPUT_STREAM,
			     "base-stream", base_stream,
			     "file-info", file_info,
			     "children", children,
			     NULL);

	return self;
}
