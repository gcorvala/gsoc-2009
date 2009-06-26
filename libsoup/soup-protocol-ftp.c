#include "soup-protocol-ftp.h"
#include <string.h>

#define SOUP_PROTOCOL_FTP_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SOUP_TYPE_PROTOCOL_FTP, SoupProtocolFtpPrivate))

struct _SoupProtocolFtpPrivate
{
	SoupURI *uri;
	GSocketConnection *cmd_connection;
	GSocketConnection *data_connection;
};

struct _SoupProtocolFtpReply
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
	
} SoupProtocolFtpReplyCode;

G_DEFINE_TYPE (SoupProtocolFtp, soup_protocol_ftp, SOUP_TYPE_PROTOCOL);


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

SoupProtocolFtpReply *ftp_receive_answer		(GSocketConnection     *conn,
							 GCancellable	       *cancellable,
							 GError		      **error);

gboolean	      ftp_send_request			(GSocketConnection     *conn,
							 const gchar	       *str,
							 GCancellable	       *cancellable,
							 GError		      **error);

GInputStream 	     *ftp_get_data_input_stream		(SoupProtocolFtp       *self,
							 GInetSocketAddress    *address);

GInetSocketAddress   *ftp_pasv_get_inet_socket_address	(SoupProtocolFtpReply  *reply);

GInputStream	     *ftp_get_data_input_stream_passive	(SoupProtocolFtp       *self,
							 GInetSocketAddress    *sock_address);
GQuark		      soup_protocol_ftp_error_quark	(void);

static void
soup_protocol_ftp_class_init (SoupProtocolFtpClass *klass)
{
	SoupProtocolClass *protocol_class = SOUP_PROTOCOL_CLASS (klass);

	g_debug ("soup_protocol_ftp_class_init called");

	g_type_class_add_private (klass, sizeof (SoupProtocolFtpPrivate));
	
	/* virtual method definition */
	protocol_class->load_uri = soup_protocol_ftp_load_uri;
	protocol_class->load_uri_async = soup_protocol_ftp_load_uri_async;
	protocol_class->load_uri_finish = soup_protocol_ftp_load_uri_finish;
}

static void
soup_protocol_ftp_init (SoupProtocolFtp *self)
{
	SoupProtocolFtpPrivate *priv;
	
	g_debug ("soup_protocol_ftp_init called");

	self->priv = priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (self);
}

SoupProtocolFtp *
soup_protocol_ftp_new (void)
{
	SoupProtocolFtp *self;

	self = g_object_new (SOUP_TYPE_PROTOCOL_FTP, NULL);
	
	return self;
}

GInputStream *
soup_protocol_ftp_load_uri (SoupProtocol		*protocol,
			    SoupURI			*uri,
			    GCancellable		*cancellable,
			    GError		       **error)
{
	SoupProtocolFtpPrivate *priv;
	GInputStream *input_stream;
	GString *host_and_port, *stat;
	SoupProtocolFtpReply *reply;
	GError *error2 = NULL;
	GString *msg = g_string_new (NULL);
	GInetSocketAddress *sock_address;


	g_debug ("soup_protocol_ftp_load_uri called");
	
	g_return_val_if_fail (SOUP_IS_PROTOCOL_FTP (protocol), NULL);

	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (SOUP_PROTOCOL_FTP (protocol));

	host_and_port = g_string_new (NULL);
	g_string_printf (host_and_port, "%s:%u", uri->host, uri->port);

	priv->cmd_connection = g_socket_client_connect_to_host (g_socket_client_new (),
								host_and_port->str,
								21,
								cancellable,
								error);

	if (*error)
	{
		g_debug ("error - %s", (*error)->message);
		
		return NULL;
	}
	
	reply = ftp_receive_answer (priv->cmd_connection, cancellable, error);

	ftp_send_request (priv->cmd_connection, "FEAT", cancellable, error);
	reply = ftp_receive_answer (priv->cmd_connection, cancellable, error);
	

	/**
	 * Authentication USER + PASS
	 **/
	g_string_printf (msg, "USER %s", uri->user);
	ftp_send_request (priv->cmd_connection, msg->str, NULL, error);
	reply = ftp_receive_answer (priv->cmd_connection, NULL, error);

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

	//else - SOUP_FTP_USER_OK_NEED_PASS

	g_string_printf (msg, "PASS %s", uri->password);
	ftp_send_request (priv->cmd_connection, msg->str, NULL, error);
	reply = ftp_receive_answer (priv->cmd_connection, NULL, error);
	
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

	//else - SOUP_FTP_USER_LOGGED)
	/**
	 * Authentication success
	 **/

	/**
	 * Detect the PASSIVE/ACTIVE MODE
	 * By default : PASSIVE
	 * TODO : ACTIVE
	 **/


	/**
	 * Passive connection
	 **/
	ftp_send_request (priv->cmd_connection, "PASV", cancellable, error);
	reply = ftp_receive_answer (priv->cmd_connection, cancellable, error);

	if (reply->code == SOUP_FTP_CMD_SYNTAX_ERROR ||
	    reply->code == SOUP_FTP_PARAM_SYNTAX_ERROR ||
	    reply->code == SOUP_FTP_CMD_NOT_IMPLEMENTED ||
	    reply->code == SOUP_FTP_SERVICE_UNAVAILABLE ||
	    reply->code == SOUP_FTP_NOT_LOGGED)
	{
		g_set_error_literal (error,
	    			     SOUP_PROTOCOL_FTP_ERROR,
				     reply->code,
				     reply->message->str);
		return NULL;
	}

	//else - SOUP_FTP_MODE_PASSIVE
	sock_address = ftp_pasv_get_inet_socket_address (reply);
	input_stream = ftp_get_data_input_stream_passive (SOUP_PROTOCOL_FTP (protocol), sock_address);
	/**
	 * Passive connection success
	 **/

	g_string_printf (msg, "RETR %s", uri->path);
	ftp_send_request (priv->cmd_connection, msg->str, cancellable, error);

	reply = ftp_receive_answer (priv->cmd_connection, cancellable, error);
	reply = ftp_receive_answer (priv->cmd_connection, cancellable, error);

	ftp_send_request (priv->cmd_connection, "QUIT", cancellable, error);
	reply = ftp_receive_answer (priv->cmd_connection, cancellable, error);

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

GInputStream *
ftp_get_data_input_stream_passive (SoupProtocolFtp *self, GInetSocketAddress *sock_address)
{
	SoupProtocolFtpPrivate *priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (self);
	GInputStream *stream;
	GInetAddress *address = g_inet_socket_address_get_address (sock_address);
	guint16 port = g_inet_socket_address_get_port (sock_address);
	GString *host_and_port = g_string_new (NULL);

	g_string_printf (host_and_port, "%s:%u", g_inet_address_to_string (address), port);

	priv->data_connection = g_socket_client_connect_to_host (g_socket_client_new (),
								host_and_port->str,
								21,
								NULL,
								NULL);
	stream = g_io_stream_get_input_stream (G_IO_STREAM (priv->data_connection));

	
	return stream;
}

GInputStream *
ftp_get_data_input_stream_active (GInetSocketAddress *sock_address)
{
}

guint16
string_to_dec (const gchar *str)
{
	guint16 dec = 0;
	
	while (*str != '\0')
		dec = dec * 10 + g_ascii_xdigit_value (str++[0]);

	return dec;
}

GInetSocketAddress *
ftp_pasv_get_inet_socket_address (SoupProtocolFtpReply *reply)
{
	GInetAddress *address;
	GInetSocketAddress *sock_address;
	GError *error = NULL;
	GString *string_address = g_string_new (NULL);
	guint16 port;
	GRegex *regex = g_regex_new ("([0-9]*),([0-9]*),([0-9]*),([0-9]*),([0-9]*),([0-9]*)", 0, 0, &error);
	gchar **split = g_regex_split_full (regex, reply->message->str,
					    -1, 0, 0, 6, &error);

	g_string_printf (string_address, "%s.%s.%s.%s", split[1], split[2], split[3], split[4]);
	address = g_inet_address_new_from_string (string_address->str);

	port = 256 * string_to_dec (split[5]) + string_to_dec (split[6]);

	sock_address = g_inet_socket_address_new (address, port);

	return sock_address;
}

gboolean
ftp_send_request (GSocketConnection *conn, const gchar *str, GCancellable *cancellable, GError **error)
{
	gchar *request;
	GDataOutputStream *output = g_data_output_stream_new (g_io_stream_get_output_stream (G_IO_STREAM (conn)));

	request = g_strconcat (str, "\r\n", NULL);

	g_debug ("---> %s", str);

	return g_data_output_stream_put_string (output, request, cancellable, error);
}

SoupProtocolFtpReply *
ftp_receive_answer (GSocketConnection *conn,
		    GCancellable      *cancellable,
		    GError	     **error)
{
	SoupProtocolFtpReply *reply = g_malloc0 (sizeof (SoupProtocolFtpReply));
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

GQuark
soup_protocol_ftp_error_quark (void)
{
	static GQuark error;
	if (!error)
		error = g_quark_from_static_string ("soup_protocol_ftp_error_quark");
	return error;
}
