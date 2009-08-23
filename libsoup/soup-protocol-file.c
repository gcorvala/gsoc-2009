/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "soup-protocol-file.h"
#include "soup-input-stream.h"

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

gboolean	      soup_protocol_file_can_load_uri    (SoupProtocol	       *protocol,
							  SoupURI	       *uri);

static void
soup_protocol_file_class_init (SoupProtocolFileClass *klass)
{
	SoupProtocolClass *protocol_class = SOUP_PROTOCOL_CLASS (klass);

	/* virtual method definition */
	protocol_class->load_uri = soup_protocol_file_load_uri;
	protocol_class->load_uri_async = soup_protocol_file_load_uri_async;
	protocol_class->load_uri_finish = soup_protocol_file_load_uri_finish;
	protocol_class->can_load_uri = soup_protocol_file_can_load_uri;
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

static gint
protocol_file_info_list_sort (gconstpointer	data1,
			      gconstpointer	data2)
{
	// FIXME : This code is duplicated (see protocol_ftp)
	GFileInfo *info1, *info2;

	g_return_val_if_fail (G_IS_FILE_INFO (data1), -1);
	g_return_val_if_fail (G_IS_FILE_INFO (data2), -1);

	info1 = G_FILE_INFO (data1);
	info2 = G_FILE_INFO (data2);

	if (g_file_info_get_file_type (info1) == G_FILE_TYPE_DIRECTORY &&
	    g_file_info_get_file_type (info2) != G_FILE_TYPE_DIRECTORY)
		return -1;
	else if (g_file_info_get_file_type (info1) != G_FILE_TYPE_DIRECTORY &&
		 g_file_info_get_file_type (info2) == G_FILE_TYPE_DIRECTORY)
		return 1;
	else
		return g_ascii_strcasecmp (g_file_info_get_name (info1),
					     g_file_info_get_name (info2));
}

GInputStream *
soup_protocol_file_load_uri (SoupProtocol  *protocol,
			     SoupURI       *uri,
			     GCancellable  *cancellable,
			     GError       **error)
{
	char *uristr;
	GFile *file;
	GInputStream *stream;
	SoupInputStream *sstream;
	GFileInfo *file_info, *children_info;
	GFileEnumerator *enumerator;
	GList *children = NULL;

	uristr = soup_uri_to_string (uri, FALSE);
	file = g_file_new_for_uri (uristr);
	g_free (uristr);
	file_info = g_file_query_info (file,
				       "standard::*",
				       G_FILE_QUERY_INFO_NONE,
				       cancellable,
				       error);
	if (file_info == NULL) {
		g_object_unref (file);
		return NULL;
	}
	if (g_file_info_get_file_type (file_info) == G_FILE_TYPE_DIRECTORY) {
		enumerator = g_file_enumerate_children (file,
							"standard::*",
							G_FILE_QUERY_INFO_NONE,
							cancellable,
							error);
		g_object_unref (file);
		if (enumerator == NULL) {
			g_object_unref (file_info);
			return NULL;
		}
		while ((children_info = g_file_enumerator_next_file (enumerator, cancellable, error)))
			children = g_list_prepend (children, children_info);
		g_object_unref (enumerator);
		if (*error != NULL) {
			g_object_unref (file_info);
			g_list_foreach (children, (GFunc) g_object_unref, NULL);
			return NULL;
		}
		children = g_list_sort (children, protocol_file_info_list_sort);
		stream = G_INPUT_STREAM (g_memory_input_stream_new ());
	}
	else {
		stream = G_INPUT_STREAM (g_file_read (file, cancellable, error));
		if (stream == NULL) {
			g_object_unref (file);
			g_object_unref (file_info);
			return NULL;
		}
	}
	sstream = soup_input_stream_new (stream, file_info, children);
	g_object_unref (file);

	return G_INPUT_STREAM (sstream);
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

gboolean
soup_protocol_file_can_load_uri (SoupProtocol	*protocol,
				 SoupURI	*uri)
{
	g_return_val_if_fail (SOUP_IS_PROTOCOL_FILE (protocol), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	if (uri->scheme == SOUP_URI_SCHEME_FILE &&
	    uri->user == NULL &&
	    uri->password == NULL &&
	    uri->host == NULL &&
	    uri->port == 0)
		return TRUE;
	else
		return FALSE;
}
