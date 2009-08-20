#include "soup-protocol.h"

G_DEFINE_TYPE (SoupProtocol, soup_protocol, G_TYPE_OBJECT);

static void
soup_protocol_class_init (SoupProtocolClass *klass)
{
	/* virtual method definition */
	klass->load_uri = NULL;
	klass->load_uri_async = NULL;
	klass->load_uri_finish = NULL;
	klass->can_load_uri = NULL;
}

static void
soup_protocol_init (SoupProtocol *self)
{
}

GInputStream *
soup_protocol_load_uri (SoupProtocol		*protocol,
			SoupURI			*uri,
			GCancellable		*cancellable,
			GError		       **error)
{
	GInputStream *input_stream;

	g_debug ("soup_protocol_load_uri called");

	g_return_val_if_fail (SOUP_IS_PROTOCOL (protocol), NULL);
	g_return_val_if_fail (SOUP_PROTOCOL_GET_CLASS (protocol)->load_uri != NULL, NULL);
	g_return_val_if_fail (uri != NULL, NULL);

	input_stream = SOUP_PROTOCOL_GET_CLASS (protocol)->load_uri (protocol,
								     uri,
								     cancellable,
								     error);

	return input_stream;
}

void
soup_protocol_load_uri_async (SoupProtocol		*protocol,
			      SoupURI			*uri,
			      GCancellable		*cancellable,
			      GAsyncReadyCallback	 callback,
			      gpointer			 user_data)
{
	g_debug ("soup_protocol_load_uri_async called");

	g_return_if_fail (SOUP_IS_PROTOCOL (protocol));
	g_return_if_fail (SOUP_PROTOCOL_GET_CLASS (protocol)->load_uri_async != NULL);
	g_return_if_fail (uri != NULL);

	SOUP_PROTOCOL_GET_CLASS (protocol)->load_uri_async (protocol,
							    uri,
							    cancellable,
							    callback,
							    user_data);
}

GInputStream *
soup_protocol_load_uri_finish (SoupProtocol	 *protocol,
			       GAsyncResult	 *result,
			       GError		**error)
{
	GInputStream *input_stream;

	g_debug ("soup_protocol_load_uri_finish called");

	g_return_val_if_fail (SOUP_IS_PROTOCOL (protocol), NULL);
	g_return_val_if_fail (SOUP_PROTOCOL_GET_CLASS (protocol)->load_uri_finish != NULL, NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

	input_stream = SOUP_PROTOCOL_GET_CLASS (protocol)->load_uri_finish (protocol,
									    result,
									    error);

	return input_stream;
}

gboolean
soup_protocol_can_load_uri (SoupProtocol		*protocol,
			    SoupURI			*uri)
{
	g_return_val_if_fail (SOUP_IS_PROTOCOL (protocol), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);

	return SOUP_PROTOCOL_GET_CLASS (protocol)->can_load_uri (protocol, uri);
}
