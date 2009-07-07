#include "soup-protocol-ftp.h"
#include "soup-misc.h"
#include <string.h>

#define SOUP_PROTOCOL_FTP_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SOUP_TYPE_PROTOCOL_FTP, SoupProtocolFTPPrivate))

struct _SoupProtocolFTPPrivate
{
	GHashTable *connections;
};

struct _SoupProtocolFTPReply
{
	guint16		code;
	GString        *message;
};

typedef enum {
	SOUP_FTP_NONE,
	/* 1yz Positive Preliminary reply */
	SOUP_FTP_RESTART_MARKER = 110,
	SOUP_FTP_SERVICE_DELAY = 120,
	SOUP_FTP_DATA_CONNECTION_OPEN_START_TRANSFERT = 125,
	SOUP_FTP_FILE_STATUS_OK = 150,
	/* 2yz Positive Completion reply */
	SOUP_FTP_CMD_OK = 200,
	SOUP_FTP_CMD_UNKNOWN = 202,
	SOUP_FTP_SYSTEM_STATUS = 211,
	SOUP_FTP_DIRECTORY_STATUS = 212,
	SOUP_FTP_FILE_STATUS = 213,
	SOUP_FTP_HELP_MESSAGE = 214,
	SOUP_FTP_SYSTEM_TYPE = 215,
	SOUP_FTP_SERVICE_READY = 220,
	SOUP_FTP_SERVICE_CLOSE = 221,
	SOUP_FTP_DATA_CONNECTION_OPEN = 225,
	SOUP_FTP_DATA_CONNECTION_CLOSE = 226,
	SOUP_FTP_MODE_PASSIVE = 227,
	SOUP_FTP_USER_LOGGED = 230,
	SOUP_FTP_FILE_ACTION_OK = 250,
	SOUP_FTP_PATHNAME_CREATED = 257,
	/* 3yz Positive Intermediate reply */
	SOUP_FTP_USER_OK_NEED_PASS = 331,
	SOUP_FTP_LOGGIN_NEED_ACCOUNT = 332,
	SOUP_FTP_FILE_ACTION_PENDING = 350,
	/* 4yz Transient Negative Completion reply */
	SOUP_FTP_SERVICE_UNAVAILABLE = 421,
	SOUP_FTP_DATA_CONNECTION_CANT_OPEN = 425,
	SOUP_FTP_DATA_CONNECTION_ABORT = 426,
	SOUP_FTP_FILE_BUSY = 450,
	SOUP_FTP_INTERNAL_ERROR = 451,
	SOUP_FTP_STORAGE_INSUFFICIENT = 452,
	/* 5yz Permanent Negative Completion reply */
	SOUP_FTP_CMD_SYNTAX_ERROR = 500,
	SOUP_FTP_PARAM_SYNTAX_ERROR = 501,
	SOUP_FTP_CMD_NOT_IMPLEMENTED = 502,
	SOUP_FTP_BAD_SEQUENCE = 503,
	SOUP_FTP_PARAM_NOT_IMPLEMENTED = 504,
	SOUP_FTP_NOT_LOGGED = 530,
	SOUP_FTP_STORE_NEED_ACCOUNT = 532,
	SOUP_FTP_FILE_NOT_FOUND = 550,
	SOUP_FTP_PAGE_TYPE_UNKNOWN = 551,
	SOUP_FTP_STORAGE_EXCEEDED = 552,
	SOUP_FTP_BAD_FILE_NAME = 553
	
} SoupProtocolFTPReplyCode;

G_DEFINE_TYPE (SoupProtocolFTP, soup_protocol_ftp, SOUP_TYPE_PROTOCOL);


GInputStream	     *soup_protocol_ftp_load_uri	(SoupProtocol	       *protocol,
							 SoupURI	       *uri,
							 GCancellable	       *cancellable,
							 GError		      **error);

void		      soup_protocol_ftp_load_uri_async	(SoupProtocol	       *protocol,
							 SoupURI	       *uri,
							 GCancellable	       *cancellable,
							 GAsyncReadyCallback	callback,
							 gpointer		user_data);

GInputStream	     *soup_protocol_ftp_load_uri_finish (SoupProtocol	       *protocol,
							 GAsyncResult	       *result,
							 GError		      **error);

SoupProtocolFTPReply *ftp_receive_reply			(GSocketConnection     *conn,
							 GCancellable	       *cancellable,
							 GError		      **error);

gboolean	      ftp_send_command			(GSocketConnection     *conn,
							 const gchar	       *str,
							 GCancellable	       *cancellable,
							 GError		      **error);

GSocketConnection    *ftp_connection 			(SoupURI	       *uri,
							 GCancellable	       *cancellable,
							 GError		      **error);

GInputStream	     *ftp_get_data_input_stream		(SoupProtocolFTP       *protocol,
							 GSocketConnection     *control,
							 SoupURI	       *uri,
							 GCancellable	       *cancellable,
							 GError		      **error);

GQuark		      soup_protocol_ftp_error_quark	(void);

guint		      ftp_hash_uri			(gconstpointer key);

gboolean	      ftp_hash_equal			(gconstpointer a,
							 gconstpointer b);

static void
soup_protocol_ftp_finalize (GObject *object)
{
	SoupProtocolFTP *ftp;
	SoupProtocolFTPPrivate *priv;

	ftp = SOUP_PROTOCOL_FTP (object);
	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (ftp);

	g_hash_table_destroy (priv->connections);

	G_OBJECT_CLASS (soup_protocol_ftp_parent_class)->finalize (object);
}

static void
soup_protocol_ftp_class_init (SoupProtocolFTPClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	SoupProtocolClass *protocol_class = SOUP_PROTOCOL_CLASS (klass);

	g_debug ("soup_protocol_ftp_class_init called");

	g_type_class_add_private (klass, sizeof (SoupProtocolFTPPrivate));
	
	/* virtual method definition */
	protocol_class->load_uri = soup_protocol_ftp_load_uri;
	protocol_class->load_uri_async = soup_protocol_ftp_load_uri_async;
	protocol_class->load_uri_finish = soup_protocol_ftp_load_uri_finish;

	gobject_class->finalize = soup_protocol_ftp_finalize;
}

static void
soup_protocol_ftp_init (SoupProtocolFTP *self)
{
	SoupProtocolFTPPrivate *priv;
	
	g_debug ("soup_protocol_ftp_init called");

	self->priv = priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (self);
	
	priv->connections = g_hash_table_new_full (ftp_hash_uri, ftp_hash_equal, soup_uri_free, g_object_unref);
}

SoupProtocol *
soup_protocol_ftp_new (void)
{
	SoupProtocolFTP *self;

	self = g_object_new (SOUP_TYPE_PROTOCOL_FTP, NULL);

	return SOUP_PROTOCOL (self);
}

GInputStream *
soup_protocol_ftp_load_uri (SoupProtocol		*protocol,
			    SoupURI			*uri,
			    GCancellable		*cancellable,
			    GError		       **error)
{
	SoupProtocolFTP *protocol_ftp = SOUP_PROTOCOL_FTP (protocol);
	SoupProtocolFTPPrivate *priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol_ftp);
	GInputStream *input_stream;
	GSocketConnection *control;

	g_debug ("soup_protocol_ftp_load_uri called");
	
	g_return_val_if_fail (SOUP_IS_PROTOCOL_FTP (protocol), NULL);

	/**
	 * Check in the HashTable if this connection already exists
	 * if not, create a new one and push it into the HashTable
	 **/
	control = g_hash_table_lookup (priv->connections, uri);
	
	if (control != NULL)
	{
		g_debug ("connected ? %u", g_socket_is_connected (g_socket_connection_get_socket (control)));
	}
	
	if (control == NULL)
	{
		control = ftp_connection (uri, cancellable, error);

		if (control == NULL)
			return NULL;

		g_hash_table_insert (priv->connections, soup_uri_copy (uri), control);
	}

	/**
	 * Get the data input stream containing the file
	 **/
	input_stream = ftp_get_data_input_stream (protocol_ftp, control, uri, cancellable, error);

	return input_stream;
}

void
soup_protocol_ftp_load_uri_async (SoupProtocol		*protocol,
				  SoupURI		*uri,
				  GCancellable		*cancellable,
				  GAsyncReadyCallback	 callback,
				  gpointer		 user_data)
{
	g_debug ("soup_protocol_ftp_load_uri_async called");

	g_return_if_fail (SOUP_IS_PROTOCOL_FTP (protocol));
}

GInputStream *
soup_protocol_ftp_load_uri_finish (SoupProtocol	 *protocol,
				   GAsyncResult	 *result,
				   GError	**error)
{
	GInputStream *input_stream;

	g_debug ("soup_protocol_ftp_load_uri_finish called");
	
	g_return_val_if_fail (SOUP_IS_PROTOCOL_FTP (protocol), NULL);
	
	return input_stream;
}

gboolean
ftp_send_command (GSocketConnection *conn,
		  const gchar *str,
		  GCancellable *cancellable,
		  GError **error)
{
	GOutputStream *output;
	gchar *request;
	gboolean success;

	output = g_io_stream_get_output_stream (G_IO_STREAM (conn));

	request = g_strconcat (str, "\r\n", NULL);

	g_debug ("---> %s", str);

	success = g_output_stream_write_all (output,
					     request,
					     strlen (request),
					     NULL,
					     cancellable,
					     error);

	g_free (request);

	return success;
}

SoupProtocolFTPReply *
ftp_receive_reply (GSocketConnection *conn,
		   GCancellable      *cancellable,
		   GError	     **error)
{
	SoupProtocolFTPReply *reply = g_malloc0 (sizeof (SoupProtocolFTPReply));
	char *buffer;
	gsize len;
	gboolean multi_line = FALSE;

	GDataInputStream *input = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (conn)));
	g_data_input_stream_set_newline_type (input, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);

	buffer = g_data_input_stream_read_line (input, &len, cancellable, error);

	reply->code = 100 * g_ascii_digit_value (buffer[0])
		     + 10 * g_ascii_digit_value (buffer[1])
		     + g_ascii_digit_value (buffer[2]);

	if (buffer[3] == '-')
		multi_line = TRUE;

	reply->message = g_string_new (NULL);

	g_string_append (reply->message, buffer + 4);

	while (multi_line)
	{
		g_string_append_c (reply->message, '\n');

		buffer = g_data_input_stream_read_line (input, &len, cancellable, error);

		if (g_ascii_digit_value (buffer[0]) == reply->code / 100 &&
		    g_ascii_digit_value (buffer[1]) == reply->code / 10 % 10 &&
		    g_ascii_digit_value (buffer[2]) == reply->code % 10 &&
		    buffer[3] == ' ')
			multi_line = FALSE;
		else
			g_string_append (reply->message, buffer);
	}
	
	g_debug ("<--- [%u] %s", reply->code, reply->message->str);
	
	return reply;
}

GSocketConnection *
ftp_connection (SoupURI	     *uri,
		GCancellable *cancellable,
		GError	    **error)
{
	GSocketConnection *control;
	GString *msg = g_string_new (NULL);
	SoupProtocolFTPReply *reply;

	g_string_printf (msg, "%s:%u", uri->host, uri->port);

	control = g_socket_client_connect_to_host (g_socket_client_new (),
						   msg->str,
						   21,
						   cancellable,
						   error);

	if (*error)
	{
		g_debug ("error - %s", (*error)->message);
		
		return NULL;
	}
	
	reply = ftp_receive_reply (control, cancellable, error);
	
	/**
	 * Authentication USER + PASS
	 **/
	g_string_printf (msg, "USER %s", uri->user);
	ftp_send_command (control, msg->str, NULL, error);
	reply = ftp_receive_reply (control, NULL, error);

	if (reply->code == SOUP_FTP_NOT_LOGGED ||
	    reply->code == SOUP_FTP_CMD_SYNTAX_ERROR ||
	    reply->code == SOUP_FTP_PARAM_SYNTAX_ERROR ||
	    reply->code == SOUP_FTP_SERVICE_UNAVAILABLE ||
	    reply->code == SOUP_FTP_LOGGIN_NEED_ACCOUNT)
	{
		g_set_error_literal (error,
	    			     SOUP_PROTOCOL_FTP_ERROR,
				     reply->code,
				     reply->message->str);
		return NULL;
	}

	else if (reply->code == SOUP_FTP_USER_LOGGED)
		return NULL;

	/* else - SOUP_FTP_USER_OK_NEED_PASS */

	g_string_printf (msg, "PASS %s", uri->password);
	ftp_send_command (control, msg->str, NULL, error);
	reply = ftp_receive_reply (control, NULL, error);
	
	if (reply->code == SOUP_FTP_CMD_UNKNOWN ||
	    reply->code == SOUP_FTP_NOT_LOGGED ||
	    reply->code == SOUP_FTP_CMD_SYNTAX_ERROR ||
	    reply->code == SOUP_FTP_PARAM_SYNTAX_ERROR ||
	    reply->code == SOUP_FTP_BAD_SEQUENCE ||
	    reply->code == SOUP_FTP_SERVICE_UNAVAILABLE ||
	    reply->code == SOUP_FTP_LOGGIN_NEED_ACCOUNT)
	{
		g_set_error_literal (error,
	    			     SOUP_PROTOCOL_FTP_ERROR,
				     reply->code,
				     reply->message->str);
		return NULL;
	}

	/* else - SOUP_FTP_USER_LOGGED) */
	/**
	 * Authentication success
	 **/
	return control;
}

GInputStream *
ftp_get_data_input_stream (SoupProtocolFTP   *protocol,
			   GSocketConnection *control,
			   SoupURI 	     *uri,
			   GCancellable      *cancellable,
			   GError           **error)
{
	GSocketConnection *data_connection;
	SoupProtocolFTPReply *reply;
	GString *msg = g_string_new (NULL);
	GRegex *regex;
	guint16 port;
	gchar **split;
	/**
	 * Detect the PASSIVE/ACTIVE MODE
	 * By default : PASSIVE
	 * TODO : ACTIVE
	 **/
	
	/**
	 * Passive connection
	 * TODO: check ipv6 answer for PASV and maybe use the EPSV
	 **/
	ftp_send_command (control, "PASV", cancellable, error);
	reply = ftp_receive_reply (control, cancellable, error);

	if (reply->code == SOUP_FTP_CMD_SYNTAX_ERROR ||
	    reply->code == SOUP_FTP_PARAM_SYNTAX_ERROR ||
	    reply->code == SOUP_FTP_SERVICE_UNAVAILABLE ||
	    reply->code == SOUP_FTP_NOT_LOGGED)
	{
		g_set_error_literal (error,
	    			     SOUP_PROTOCOL_FTP_ERROR,
				     reply->code,
				     reply->message->str);
		return NULL;
	}

	else if (reply->code == SOUP_FTP_CMD_NOT_IMPLEMENTED)
	{
		/**
		 * Active connection
		 **/
	}
	
	else
	{
		/**
		 * Passive connection (h1,h2,h3,h4,p1,p2)
		 **/
		regex = g_regex_new ("([0-9]*),([0-9]*),([0-9]*),([0-9]*),([0-9]*),([0-9]*)", 0, 0, error);
		split = g_regex_split_full (regex, reply->message->str,
					    -1, 0, 0, 6, error);
		/**
		 * if IPV4
		 **/
		g_string_printf (msg, "%s.%s.%s.%s", split[1], split[2], split[3], split[4]);
		/**
		 * if IPV6
		 * USE PASV
		 * do not use the h1,h2,h3,h4
		 * use the same address as cmd_connection
		 * USE EPSV
		 **/

		port = 256 * soup_str_to_uint (split[5]) + soup_str_to_uint (split[6]);

		g_string_append_printf (msg, ":%u", port);
		data_connection = g_socket_client_connect_to_host (g_socket_client_new (),
									msg->str,
									21,
									NULL,
									NULL);
	}

	g_string_printf (msg, "RETR %s", uri->path);
	ftp_send_command (control, msg->str, cancellable, error);

	reply = ftp_receive_reply (control, cancellable, error);
	reply = ftp_receive_reply (control, cancellable, error);

	return g_io_stream_get_input_stream (G_IO_STREAM (data_connection));
}


GQuark
soup_protocol_ftp_error_quark (void)
{
	static GQuark error;
	if (!error)
		error = g_quark_from_static_string ("soup_protocol_ftp_error_quark");
	return error;
}

guint
ftp_hash_uri (gconstpointer key)
{
	return g_str_hash (((SoupURI *) key)->host);
}

gboolean
ftp_hash_equal (gconstpointer a,
		gconstpointer b)
{
	SoupURI *uri_a, *uri_b;

	uri_a = (SoupURI *) a;
	uri_b = (SoupURI *) b;

	if (!g_strcmp0 (uri_a->scheme, uri_b->scheme) &&
	    !g_strcmp0 (uri_a->user, uri_b->user) &&
	    !g_strcmp0 (uri_a->password, uri_b->password) &&
	    !g_strcmp0 (uri_a->host, uri_b->host) &&
	    uri_a->port == uri_b->port)
		return TRUE;

	return FALSE;
}
