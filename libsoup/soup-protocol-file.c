/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "soup-protocol-file.h"

G_DEFINE_TYPE (SoupProtocolFile, soup_protocol_file, SOUP_TYPE_PROTOCOL);

GInputStream	     *soup_protocol_file_load_uri	(SoupProtocol	       *protocol,
							 SoupURI	       *uri,
							 GCancellable	       *cancellable,
							 GError		      **error);

void		      soup_protocol_file_load_uri_async	(SoupProtocol	       *protocol,
							 SoupURI	       *uri,
							 GCancellable	       *cancellable,
							 GAsyncReadyCallback	callback,
							 gpointer		user_data);

GInputStream	     *soup_protocol_file_load_uri_finish (SoupProtocol	       *protocol,
							 GAsyncResult	       *result,
							 GError		      **error);

static void
soup_protocol_file_class_init (SoupProtocolFileClass *klass)
{
	SoupProtocolClass *protocol_class = SOUP_PROTOCOL_CLASS (klass);

	/* virtual method definition */
	protocol_class->load_uri = soup_protocol_file_load_uri;
	protocol_class->load_uri_async = soup_protocol_file_load_uri_async;
	protocol_class->load_uri_finish = soup_protocol_file_load_uri_finish;
}

static void
soup_protocol_file_init (SoupProtocolFile *self)
{
}

SoupProtocol *
soup_protocol_file_new (void)
{
	return g_object_new (SOUP_TYPE_PROTOCOL_FILE, NULL);
}

GInputStream *
soup_protocol_file_load_uri (SoupProtocol  *protocol,
			     SoupURI       *uri,
			     GCancellable  *cancellable,
			     GError       **error)
{
	char *uristr;
	GFile *file;
	GFileInputStream *stream;

	uristr = soup_uri_to_string (uri, FALSE);
	file = g_file_new_for_uri (uristr);
	g_free (uristr);

	stream = g_file_read (file, cancellable, error);
	g_object_unref (file);
	return G_INPUT_STREAM (stream);
}

static void
read_async_callback (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GFile *file = G_FILE (source);
	GSimpleAsyncResult *simple = user_data;
	GFileInputStream *stream;
	GError *error = NULL;

	stream = g_file_read_finish (file, result, &error);
	if (stream)
		g_simple_async_result_set_op_res_gpointer (simple, stream, g_object_unref);
	else {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}

	g_simple_async_result_complete (simple);
	g_object_unref (simple);
}

void
soup_protocol_file_load_uri_async (SoupProtocol        *protocol,
				   SoupURI             *uri,
				   GCancellable        *cancellable,
				   GAsyncReadyCallback  callback,
				   gpointer             user_data)
{
	GSimpleAsyncResult *simple;
	char *uristr;
	GFile *file;

	simple = g_simple_async_result_new (G_OBJECT (protocol),
					    callback,
					    user_data,
					    soup_protocol_file_load_uri_async);

	uristr = soup_uri_to_string (uri, FALSE);
	file = g_file_new_for_uri (uristr);
	g_free (uristr);

	g_file_read_async (file, G_PRIORITY_DEFAULT, cancellable,
			   read_async_callback, simple);
}

GInputStream *
soup_protocol_file_load_uri_finish (SoupProtocol  *protocol,
				    GAsyncResult  *result,
				    GError       **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (SOUP_IS_PROTOCOL_FILE (protocol), NULL);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (protocol), soup_protocol_file_load_uri_async), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_object_ref (g_simple_async_result_get_op_res_gpointer (simple));
}
