#include "soup-protocol-ftp.h"

#define SOUP_PROTOCOL_FTP_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SOUP_TYPE_PROTOCOL_FTP, SoupProtocolFtpPrivate))

struct _SoupProtocolFtpPrivate
{
	SoupURI *uri;
	GSocketConnection *conn;
};

struct _SoupProtocolFtpReply
{
	gchar 		code[3];
	GString        *message;
};

G_DEFINE_TYPE (SoupProtocolFtp, soup_protocol_ftp, SOUP_TYPE_PROTOCOL);

SoupProtocolFtpReply *ftp_receive_answer (GSocketConnection *conn,
					  GCancellable      *cancellable,
					  GError	   **error);

gboolean ftp_send_request (GSocketConnection *conn, const char *str, GCancellable *cancellable, GError **error);

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
	GString *host_and_port;
	SoupProtocolFtpReply *reply;

	g_debug ("soup_protocol_ftp_load_uri called");
	
	g_return_val_if_fail (SOUP_IS_PROTOCOL_FTP (protocol), NULL);

	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (SOUP_PROTOCOL_FTP (protocol));

	host_and_port = g_string_new (NULL);
	g_string_printf (host_and_port, "%s:%u", uri->host, uri->port);

	priv->conn = g_socket_client_connect_to_host (g_socket_client_new (),
						      host_and_port->str,
						      21,
						      cancellable,
						      error);

	if (error)
		g_debug ("error - %s", (*error)->message);
	
	reply = ftp_receive_answer (priv->conn, cancellable, error);

	g_debug ("srv : %s", reply->message->str);
	
	ftp_send_request (priv->conn, "FEAT", cancellable, error);

	reply = ftp_receive_answer (priv->conn, cancellable, error);

	g_debug ("srv : %s", reply->message->str);
 
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

//void
//ftp_auth (GIOStream  *io,
	  //const gchar *login, 
	  //const gchar *password,
	  //GError **error)
//{
	
//}

gboolean
ftp_send_request (GSocketConnection *conn, const char *str, GCancellable *cancellable, GError **error)
{
	GDataOutputStream *output = g_data_output_stream_new (g_io_stream_get_output_stream (G_IO_STREAM (conn)));
	return g_data_output_stream_put_string (output, str, cancellable, error);
}

SoupProtocolFtpReply *
ftp_receive_answer (GSocketConnection *conn,
		    GCancellable      *cancellable,
		    GError	     **error)
{
	SoupProtocolFtpReply *reply = g_malloc0 (sizeof (SoupProtocolFtpReply));
	char *buffer;
	gsize len;
	gboolean end = FALSE;
	
	GDataInputStream *input = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (conn)));
	g_data_input_stream_set_newline_type (input, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);

	buffer = g_data_input_stream_read_line (input, &len, cancellable, error);
	
	reply->code[0] = *buffer;
	reply->code[1] = *(buffer + 1);
	reply->code[2] = *(buffer + 2);
	
	reply->message = g_string_new (NULL);
	
	g_string_append (reply->message, buffer + 4);

	if (*(buffer + 3) != '-')
		end = TRUE;

	while (!end)
	{
		g_string_append_c (reply->message, '\n');

		buffer = g_data_input_stream_read_line (input, &len, cancellable, error);

		if (*buffer != ' ')
		{
			g_string_append (reply->message, buffer + 4);
			end = TRUE;
		}
		else
			g_string_append (reply->message, buffer);
	}

	return reply;
}
