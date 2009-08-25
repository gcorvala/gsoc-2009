#include "soup-uri-loader.h"
#include "soup-protocol.h"
#include "soup-protocol-ftp.h"
#include "soup-protocol-file.h"

#define SOUP_URI_LOADER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SOUP_TYPE_URI_LOADER, SoupURILoaderPrivate))

struct _SoupURILoaderPrivate {
	GHashTable *protocol_types; // scheme - GType
	GSList *protocols;
};

G_DEFINE_TYPE (SoupURILoader, soup_uri_loader, G_TYPE_OBJECT);

static void
soup_uri_loader_finalize (GObject *object)
{
	SoupURILoader *loader;
	SoupURILoaderPrivate *priv;

	loader = SOUP_URI_LOADER (object);
	priv = SOUP_URI_LOADER_GET_PRIVATE (loader);

	g_hash_table_destroy (priv->protocol_types);

	g_slist_foreach (priv->protocols, (GFunc) g_object_unref, NULL);
	g_slist_free (priv->protocols);

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

	priv->protocols = NULL;
	priv->protocol_types = g_hash_table_new_full (g_str_hash,
						      g_str_equal,
						      g_free,
						      NULL);
	soup_uri_loader_add_protocol (self, "ftp", SOUP_TYPE_PROTOCOL_FTP);
	soup_uri_loader_add_protocol (self, "file", SOUP_TYPE_PROTOCOL_FILE);
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
soup_uri_loader_add_protocol (SoupURILoader	 *loader,
			       gchar		 *scheme,
			       GType		  type)
{
	SoupURILoaderPrivate *priv;

	g_return_val_if_fail (SOUP_IS_URI_LOADER (loader), FALSE);
	g_return_val_if_fail (scheme != NULL, FALSE);
	g_return_val_if_fail (g_type_is_a (type, SOUP_TYPE_PROTOCOL), FALSE);

	priv = SOUP_URI_LOADER_GET_PRIVATE (loader);
	if (g_hash_table_lookup (priv->protocol_types, scheme) != NULL)
		return FALSE;
	g_hash_table_insert (priv->protocol_types, g_strdup (scheme), GSIZE_TO_POINTER (type));

	return TRUE;
}

gboolean
soup_uri_loader_remove_protocol (SoupURILoader	*loader,
				 gchar		*scheme)
{
	SoupURILoaderPrivate *priv;
	GType protocol_type;
	GSList *tmp, *prev = NULL;

	g_return_val_if_fail (SOUP_IS_URI_LOADER (loader), FALSE);
	g_return_val_if_fail (scheme != NULL, FALSE);

	priv = SOUP_URI_LOADER_GET_PRIVATE (loader);

	protocol_type = GPOINTER_TO_SIZE (g_hash_table_lookup (priv->protocol_types, scheme));
	if (protocol_type == 0)
		return FALSE;
	tmp = priv->protocols;
	while (tmp) {
		if (G_TYPE_CHECK_INSTANCE_TYPE (tmp->data, protocol_type)) {
			if (prev)
				prev->next = tmp->next;
			else
				priv->protocols = tmp->next;
			g_object_unref (tmp->data);
			g_slist_free_1 (tmp);
			if (prev)
				tmp = prev->next;
			else
				tmp = priv->protocols;
		}
		else {
			prev = tmp;
			tmp = prev->next;
		}
	}

	return g_hash_table_remove (priv->protocol_types, scheme);
}

static gint
compare_protocol (gconstpointer a,
		  gconstpointer b)
{
	g_return_val_if_fail (SOUP_IS_PROTOCOL (a), -1);
	g_return_val_if_fail (b != NULL, -1);

	return !soup_protocol_can_load_uri (SOUP_PROTOCOL (a), (SoupURI *) b);
}

GInputStream*
soup_uri_loader_load_uri (SoupURILoader	 *loader,
			  SoupURI	 *uri,
			  GCancellable	 *cancellable,
			  GError	**error)
{
	SoupURILoaderPrivate *priv;
	GInputStream *input_stream;
	SoupProtocol *protocol = NULL;
	GType protocol_type;
	GSList *search = NULL;

	g_return_val_if_fail (SOUP_IS_URI_LOADER (loader), NULL);
	g_return_val_if_fail (uri != NULL, NULL);

	priv = SOUP_URI_LOADER_GET_PRIVATE (loader);

	search = g_slist_find_custom (priv->protocols, uri, compare_protocol);
	if (search == NULL) {
		protocol_type = GPOINTER_TO_SIZE (g_hash_table_lookup (priv->protocol_types, uri->scheme));
		if (protocol_type == 0) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "Protocol not supported : %s",
				     uri->scheme);
			return NULL;
		}
		protocol = g_object_new (protocol_type, NULL);
		if (protocol != NULL)
			priv->protocols = g_slist_prepend (priv->protocols, protocol);
	}
	else
		protocol = SOUP_PROTOCOL (search->data);
	input_stream = soup_protocol_load_uri (protocol, uri, cancellable, error);

	return input_stream;
}

static void
load_uri_cb (GObject *source_object,
	     GAsyncResult *res,
	     gpointer user_data)
{
	SoupProtocolFTP *protocol;
	GSimpleAsyncResult *result;
	GInputStream *input_stream;
	GError *error = NULL;

	g_return_if_fail (SOUP_IS_PROTOCOL_FTP (source_object));
	g_return_if_fail (G_IS_ASYNC_RESULT (res));
	g_return_if_fail (G_IS_SIMPLE_ASYNC_RESULT (user_data));

	protocol = SOUP_PROTOCOL_FTP (source_object);
	result = G_SIMPLE_ASYNC_RESULT (user_data);
	input_stream = soup_protocol_load_uri_finish (SOUP_PROTOCOL (protocol), res, &error);
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
	GType protocol_type;
	GSList *search = NULL;

	g_return_if_fail (SOUP_IS_URI_LOADER (loader));
	g_return_if_fail (uri != NULL);

	result = g_simple_async_result_new (G_OBJECT (loader),
					    callback,
					    user_data,
					    soup_uri_loader_load_uri_async);
	search = g_slist_find_custom (priv->protocols, uri, compare_protocol);
	if (search == NULL) {
		protocol_type = GPOINTER_TO_SIZE (g_hash_table_lookup (priv->protocol_types, uri->scheme));
		if (protocol_type == 0) {
			g_debug ("Protocol not supported : %s", uri->scheme);
			return;
		}
		protocol = g_object_new (protocol_type, NULL);
		if (protocol != NULL)
			priv->protocols = g_slist_prepend (priv->protocols, protocol);
	}
	else
		protocol = SOUP_PROTOCOL (search->data);
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

	g_return_val_if_fail (SOUP_IS_URI_LOADER (loader), NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
	g_return_val_if_fail (g_simple_async_result_is_valid (result,
							      G_OBJECT (loader),
							      soup_uri_loader_load_uri_async), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;
	input_stream = g_simple_async_result_get_op_res_gpointer (simple);

	return input_stream;
}
