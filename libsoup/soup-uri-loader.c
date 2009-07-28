#include "soup-uri-loader.h"
#include "soup-protocol.h"
#include "soup-protocol-ftp.h"

#define SOUP_URI_LOADER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SOUP_TYPE_URI_LOADER, SoupURILoaderPrivate))

struct _SoupURILoaderPrivate
{
	GHashTable *protocols; //hash table of protocols that contain a hash table of connections
};

G_DEFINE_TYPE (SoupURILoader, soup_uri_loader, G_TYPE_OBJECT); 

static void
soup_uri_loader_finalize (GObject *object)
{
	SoupURILoader *loader;
	SoupURILoaderPrivate *priv;

	loader = SOUP_URI_LOADER (object);
	priv = SOUP_URI_LOADER_GET_PRIVATE (loader);

	g_hash_table_destroy (priv->protocols);

	G_OBJECT_CLASS (soup_uri_loader_parent_class)->finalize (object);
}

static void
soup_uri_loader_class_init (SoupURILoaderClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	
	g_type_class_add_private (klass, sizeof (SoupURILoaderPrivate));
	
	gobject_class->finalize = soup_uri_loader_finalize;
}

static void
soup_uri_loader_init (SoupURILoader *self)
{
	SoupURILoaderPrivate *priv;

	self->priv = priv = SOUP_URI_LOADER_GET_PRIVATE (self);
	
	priv->protocols = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_hash_table_destroy);
}

SoupURILoader *
soup_uri_loader_new (void)
{
	SoupURILoader *self;
	
	g_debug ("soup_uri_loader_new called");

	self = g_object_new (SOUP_TYPE_URI_LOADER, NULL);
	
	return self;
}

gboolean
uri_hash_equal (gconstpointer a,
		gconstpointer b)
{
	const SoupURI *uri_a, *uri_b;

	uri_a = (SoupURI *) a;
	uri_b = (SoupURI *) b;

	if (!soup_uri_host_equal (uri_a, uri_b) ||
	    g_strcmp0 (uri_a->user, uri_b->user) ||
	    g_strcmp0 (uri_a->password, uri_b->password))
	    return FALSE;

	return TRUE;
}

GInputStream*
soup_uri_loader_load_uri (SoupURILoader	 *loader,
			  SoupURI	 *uri,
			  GCancellable	 *cancellable,
			  GError	**error)
{
	SoupURILoaderPrivate *priv;
	GInputStream *input_stream;
	SoupProtocol *protocol;
	GHashTable *connections;

	g_return_if_fail (SOUP_IS_URI_LOADER (loader));
	g_return_if_fail (uri != NULL);

	priv = SOUP_URI_LOADER_GET_PRIVATE (loader);
	connections = g_hash_table_lookup (priv->protocols, uri->scheme);
	if (!connections) {
		if (uri->scheme == SOUP_URI_SCHEME_HTTP)
			g_debug ("http");
		else if (uri->scheme == SOUP_URI_SCHEME_HTTPS)
			g_debug ("https");
		else if (uri->scheme == SOUP_URI_SCHEME_FTP) {
			connections = g_hash_table_new_full (soup_uri_host_hash,
							     uri_hash_equal,
							     (GDestroyNotify) soup_uri_free,
							     g_object_unref);
			protocol = soup_protocol_ftp_new ();
		}
		else
			g_debug ("error");
		g_hash_table_insert (priv->protocols, g_strdup (uri->scheme), connections);
		g_hash_table_insert (connections, soup_uri_copy (uri), protocol);
	}
	else {
		protocol = g_hash_table_lookup (connections, uri);
		if (!protocol) {
			if (uri->scheme == SOUP_URI_SCHEME_HTTP)
				g_debug ("http");
			else if (uri->scheme == SOUP_URI_SCHEME_HTTPS)
				g_debug ("https");
			else if (uri->scheme == SOUP_URI_SCHEME_FTP)
				protocol = soup_protocol_ftp_new ();
			else
				g_debug ("error");
			g_hash_table_insert (connections, soup_uri_copy (uri), protocol);
		}
	}
	input_stream = soup_protocol_load_uri (protocol, uri, cancellable, error);
	
	return input_stream;
}

void
load_uri_cb (GObject *source_object,
	     GAsyncResult *res,
	     gpointer user_data)
{
	SoupProtocol *protocol;
	GSimpleAsyncResult *result;
	GInputStream *input_stream;
	GError *error = NULL;

	g_return_if_fail (SOUP_IS_PROTOCOL_FTP (source_object));
	g_return_if_fail (G_IS_ASYNC_RESULT (res));
	g_return_if_fail (G_IS_SIMPLE_ASYNC_RESULT (user_data));

	protocol = SOUP_PROTOCOL_FTP (source_object);
	result = user_data;
	input_stream = soup_protocol_load_uri_finish (protocol, res, error);
	if (input_stream) {
		g_simple_async_result_set_op_res_gpointer (result,
							   input_stream,
							   g_object_unref);
	}
	else {
		g_simple_async_result_set_from_error (result, error);
		g_error_free (error);
	}
	g_simple_async_result_complete (result);
	g_object_unref (result);
}

void
soup_uri_loader_load_uri_async (SoupURILoader		*loader,
				SoupURI			*uri,
				GCancellable		*cancellable,
				GAsyncReadyCallback	 callback,
				gpointer		 user_data)
{
	SoupURILoaderPrivate *priv = SOUP_URI_LOADER_GET_PRIVATE (loader);
	SoupProtocol *protocol;
	GSimpleAsyncResult *result;
	GHashTable *connections;

	g_return_if_fail (SOUP_IS_URI_LOADER (loader));
	g_return_if_fail (uri != NULL);

	result = g_simple_async_result_new (loader,
					    callback,
					    user_data,
					    soup_uri_loader_load_uri_async);
	connections = g_hash_table_lookup (priv->protocols, uri->scheme);
	if (!connections) {
		if (uri->scheme == SOUP_URI_SCHEME_HTTP)
			g_debug ("http");
		else if (uri->scheme == SOUP_URI_SCHEME_HTTPS)
			g_debug ("https");
		else if (uri->scheme == SOUP_URI_SCHEME_FTP) {
			connections = g_hash_table_new_full (soup_uri_host_hash,
							     uri_hash_equal,
							     (GDestroyNotify) soup_uri_free,
							     g_object_unref);
			protocol = soup_protocol_ftp_new ();
		}
		else
			g_debug ("error");
		g_hash_table_insert (priv->protocols, g_strdup (uri->scheme), connections);
		g_hash_table_insert (connections, soup_uri_copy (uri), protocol);
	}
	else {
		protocol = g_hash_table_lookup (connections, uri);
		if (!protocol) {
			if (uri->scheme == SOUP_URI_SCHEME_HTTP)
				g_debug ("http");
			else if (uri->scheme == SOUP_URI_SCHEME_HTTPS)
				g_debug ("https");
			else if (uri->scheme == SOUP_URI_SCHEME_FTP)
				protocol = soup_protocol_ftp_new ();
			else
				g_debug ("error");
			g_hash_table_insert (connections, soup_uri_copy (uri), protocol);
		}
	}
	soup_protocol_load_uri_async (protocol, uri, cancellable, load_uri_cb, result);
}

GInputStream *
soup_uri_loader_load_uri_finish (SoupURILoader	 *loader,
				 GAsyncResult	 *result,
				 GError		**error)
{
	GInputStream *input_stream;
	GSimpleAsyncResult *simple;

	g_debug ("soup_uri_loader_load_uri_finish finish");

	g_return_if_fail (SOUP_IS_URI_LOADER (loader));
	g_return_if_fail (G_IS_ASYNC_RESULT (result));
	g_return_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (loader), soup_uri_loader_load_uri_async));

	simple = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;
	input_stream = g_simple_async_result_get_op_res_gpointer (simple);

	return input_stream;
}

GList *
soup_uri_loader_get_list (SoupURILoader	 *loader,
			  SoupURI	 *uri,
			  GCancellable	 *cancellable,
			  GError	**error)
{
	SoupURILoaderPrivate *priv;
	GList *file_list;
	SoupProtocol *protocol;

	g_return_if_fail (SOUP_IS_URI_LOADER (loader));
	g_return_if_fail (uri != NULL);

	priv = SOUP_URI_LOADER_GET_PRIVATE (loader);
	protocol = g_hash_table_lookup (priv->protocols, uri->scheme);
	if (!protocol) {
		if (uri->scheme == SOUP_URI_SCHEME_HTTP)
			g_debug ("http");
		else if (uri->scheme == SOUP_URI_SCHEME_HTTPS)
			g_debug ("https");
		else if (uri->scheme == SOUP_URI_SCHEME_FTP)
			protocol = soup_protocol_ftp_new ();
		else
			g_debug ("error");
		g_hash_table_insert (priv->protocols, g_strdup (uri->scheme), protocol);
	}
	file_list = soup_protocol_get_list (protocol, uri, cancellable, error);

	return file_list;
}
