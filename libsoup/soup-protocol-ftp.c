#include "soup-protocol-ftp.h"
#include "soup-misc.h"
#include <string.h>
#include <stdlib.h>

#define SOUP_PROTOCOL_FTP_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SOUP_TYPE_PROTOCOL_FTP, SoupProtocolFTPPrivate))

struct _SoupProtocolFTPPrivate
{
	GHashTable *connections;
};

typedef enum {
	SOUP_PROTOCOL_FTP_FEATURE_MDTM = 1 << 0,
	SOUP_PROTOCOL_FTP_FEATURE_SIZE = 1 << 1,
	SOUP_PROTOCOL_FTP_FEATURE_REST = 1 << 2,
	SOUP_PROTOCOL_FTP_FEATURE_TVFS = 1 << 3,
	SOUP_PROTOCOL_FTP_FEATURE_MLST = 1 << 4,
	SOUP_PROTOCOL_FTP_FEATURE_MLSD = 1 << 5,
	SOUP_PROTOCOL_FTP_FEATURE_EPRT = 1 << 6,
	SOUP_PROTOCOL_FTP_FEATURE_EPSV = 1 << 7,
	SOUP_PROTOCOL_FTP_FEATURE_UTF8 = 1 << 8
} SoupProtocolFTPFeature;

typedef struct
{
	guint16		code;
	GString        *message;
} SoupProtocolFTPReply;

typedef struct
{
	guint16		features;
} SoupProtocolFTPData;

typedef enum {
	SOUP_FTP_NONE,
	/* 0yz used only by SoupProtocolFTP */
	SOUP_FTP_BAD_ANSWER = 0,
	SOUP_FTP_INVALID_PATH = 1,
	SOUP_FTP_ACTIVE_NOT_IMPLEMENTED = 2,

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

#define SOUP_PARSE_FTP_STATUS(buffer) (g_ascii_digit_value (buffer[0]) * 100 + g_ascii_digit_value (buffer[1]) * 10 + g_ascii_digit_value (buffer[2]))

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

GSocketConnection    *ftp_get_data			(SoupProtocolFTP       *protocol,
							 GSocketConnection     *control,
							 SoupURI	       *uri,
							 GCancellable	       *cancellable,
							 GError		      **error);

GQuark		      soup_protocol_ftp_error_quark	(void);

gboolean	      ftp_uri_hash_equal		(gconstpointer a,
							 gconstpointer b);

void		      ftp_reply_free			(SoupProtocolFTPReply *reply);

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
	
	priv->connections = g_hash_table_new_full (soup_uri_host_hash,
						   ftp_uri_hash_equal,
						   soup_uri_free,
						   g_object_unref);
}

SoupProtocol *
soup_protocol_ftp_new (void)
{
	SoupProtocolFTP *self;

	self = g_object_new (SOUP_TYPE_PROTOCOL_FTP, NULL);

	return SOUP_PROTOCOL (self);
}

gboolean
ftp_determine_features (SoupProtocolFTPData *data, SoupProtocolFTP *protocol, SoupProtocolFTPReply *reply)
{
	gchar **split, *feature;
	gint i, j;
	const struct {
		const gchar		*name;
		SoupProtocolFTPFeature	 enable;
	} features [] = {
		{ "MDTM", SOUP_PROTOCOL_FTP_FEATURE_MDTM },
		{ "SIZE", SOUP_PROTOCOL_FTP_FEATURE_SIZE },
		{ "REST", SOUP_PROTOCOL_FTP_FEATURE_REST },
		{ "TVFS", SOUP_PROTOCOL_FTP_FEATURE_TVFS },
		{ "MLST", SOUP_PROTOCOL_FTP_FEATURE_MLST },
		{ "MLSD", SOUP_PROTOCOL_FTP_FEATURE_MLSD },
		{ "EPRT", SOUP_PROTOCOL_FTP_FEATURE_EPRT },
		{ "EPSV", SOUP_PROTOCOL_FTP_FEATURE_EPSV },
		{ "UTF8", SOUP_PROTOCOL_FTP_FEATURE_UTF8 },
	};

	if (reply->code != 211)
		return FALSE;

	split = g_strsplit (reply->message->str, "\n", 0);

	for (i = 1; split[i + 1]; ++i)
	{
		if (!g_ascii_isspace (split[i][0])) {
			data->features = 0;
			g_strfreev (split);
			return FALSE;
		}
		feature = g_strstrip (split[i]);

		for (j = 0; j < G_N_ELEMENTS (features); ++j)
		{
			if (g_ascii_strncasecmp (feature, features[j].name, 4) == 0) {
				g_debug ("[FEAT] %s supported", features[j].name);
				data->features |= features[j].enable;
			}
		}
	}

	g_strfreev (split);
	return TRUE;
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
	GSocketConnection *data;
	SoupProtocolFTPData *ftp_data;
	SoupProtocolFTPReply *reply;

	g_debug ("soup_protocol_ftp_load_uri called");
	
	g_return_val_if_fail (SOUP_IS_PROTOCOL_FTP (protocol), NULL);

	/**
	 * Check in the HashTable if this connection already exists
	 * if not, create a new one and push it into the HashTable
	 **/
	control = g_hash_table_lookup (priv->connections, uri);
	
	if (control == NULL)
	{
		control = ftp_connection (uri, cancellable, error);

		if (control == NULL)
			return NULL;
		g_hash_table_insert (priv->connections, soup_uri_copy (uri), control);
	}

	/**
	 * test FEATURES
	 **/
	ftp_data = g_malloc0 (sizeof (SoupProtocolFTPData));
	ftp_send_command (control, "FEAT", NULL, NULL);
	reply = ftp_receive_reply (control, NULL, NULL);
	ftp_determine_features (ftp_data, NULL, reply);

	/**
	 * Get the data input stream containing the file
	 **/
	data = ftp_get_data (protocol_ftp, control, uri, cancellable, error);

	input_stream = g_io_stream_get_input_stream (G_IO_STREAM (data));
	g_object_ref (input_stream);
	g_object_set_data_full (G_OBJECT (input_stream), "socket-connection", data, g_object_unref);

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

	success = g_output_stream_write_all (output,
					     request,
					     strlen (request),
					     NULL,
					     cancellable,
					     error);

	g_free (request);

	g_debug ("---> %s", str);

	return success;
}

SoupProtocolFTPReply *
ftp_receive_reply (GSocketConnection *conn,
		   GCancellable      *cancellable,
		   GError	     **error)
{
	/* Leaked @input. It's also not very efficient to create a new
	 * GDataInputStream every time you call ftp_receive_reply. It
	 * would be better to create it once, when you first make the
	 * GSocketConnection, and store it somewhere (and make sure to
	 * unref it when you're done with the connection).
	 */

	SoupProtocolFTPReply *reply = g_malloc0 (sizeof (SoupProtocolFTPReply));
	GDataInputStream *input;
	char *buffer;
	gsize len;
	gboolean multi_line = FALSE;

	input = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (conn)));
	g_data_input_stream_set_newline_type (input, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);

	buffer = g_data_input_stream_read_line (input, &len, cancellable, error);

	if (buffer == NULL)
		return NULL;

	if (len < 4) {
		g_set_error_literal (error,
				     SOUP_PROTOCOL_FTP_ERROR,
				     SOUP_FTP_BAD_ANSWER,
				     "Bad FTP answer (less than 4 character)");
		return NULL;
	}

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

		if (SOUP_PARSE_FTP_STATUS (buffer) == reply->code &&
		    g_ascii_isspace (buffer[3]))
			multi_line = FALSE;
		else
			g_string_append (reply->message, buffer);
	}
	
	g_free (buffer);
	
	g_debug ("<--- [%u] %s", reply->code, reply->message->str);

	return reply;
}

GSocketConnection *
ftp_connection (SoupURI	     *uri,
		GCancellable *cancellable,
		GError	    **error)
{
	GSocketConnection *control;
	gchar *msg;
	SoupProtocolFTPReply *reply;

	control = g_socket_client_connect_to_host (g_socket_client_new (),
						   uri->host,
						   uri->port,
						   cancellable,
						   error);

	if (control == NULL)
		return NULL;
	
	reply = ftp_receive_reply (control, cancellable, error);
	
	if (reply == NULL) {
		g_object_unref (control);
		return NULL;
	}

	/**
	 * Authentication USER + PASS
	 **/
	msg = g_strdup_printf ("USER %s", uri->user);

	if (ftp_send_command (control, msg, cancellable, error) == FALSE) {
		g_object_unref (control);
		g_free (msg);
		return NULL;
	}

	reply = ftp_receive_reply (control, cancellable, error);

	if (reply == NULL) {
		g_object_unref (control);
		g_free (msg);
		return NULL;
	}

	if (reply->code == SOUP_FTP_NOT_LOGGED ||
	    reply->code == SOUP_FTP_CMD_SYNTAX_ERROR ||
	    reply->code == SOUP_FTP_PARAM_SYNTAX_ERROR ||
	    reply->code == SOUP_FTP_SERVICE_UNAVAILABLE ||
	    reply->code == SOUP_FTP_LOGGIN_NEED_ACCOUNT)
	{
		g_set_error_literal (error,
	    			     SOUP_PROTOCOL_FTP_ERROR,
				     reply->code,
				     g_strdup (reply->message->str));
		g_object_unref (control);
		g_free (msg);
		ftp_reply_free (reply);
		return NULL;
	}

	else if (reply->code == SOUP_FTP_USER_LOGGED) {
		g_free (msg);
		ftp_reply_free (reply);
		return control;
	}

	/* else - SOUP_FTP_USER_OK_NEED_PASS */

	g_free (msg);
	msg = g_strdup_printf ("PASS %s", uri->password);

	if (ftp_send_command (control, msg, cancellable, error) == FALSE) {
		g_object_unref (control);
		g_free (msg);
		ftp_reply_free (reply);
		return NULL;
	}

	reply = ftp_receive_reply (control, cancellable, error);

	if (reply == NULL) {
		g_object_unref (control);
		g_free (msg);
		ftp_reply_free (reply);
		return NULL;
	}
	
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
				     g_strdup (reply->message->str));
		g_object_unref (control);
		g_free (msg);
		ftp_reply_free (reply);
		return NULL;
	}

	/* else - SOUP_FTP_USER_LOGGED) */
	/**
	 * Authentication success
	 **/

	g_free (msg);
	ftp_reply_free (reply);

	return control;
}

GSocketConnection *
ftp_get_data (SoupProtocolFTP	*protocol,
	      GSocketConnection *control,
	      SoupURI		*uri,
	      GCancellable	*cancellable,
	      GError	       **error)
{
	GSocketConnection *data_connection;
	SoupProtocolFTPReply *reply;
	GRegex *regex;
	gchar **split;
	guint16 port;
	gchar *buffer, *uri_decode;
	/**
	 * Detect the PASSIVE/ACTIVE MODE
	 * By default : PASSIVE
	 * TODO : ACTIVE
	 **/

	/**
	 * Passive connection
	 * TODO: check ipv6 answer for PASV and maybe use the EPSV
	 **/
	if (ftp_send_command (control, "PASV", cancellable, error) == FALSE)
		return NULL;

	reply = ftp_receive_reply (control, cancellable, error);

	if (reply == NULL)
		return NULL;

	if (reply->code == SOUP_FTP_CMD_SYNTAX_ERROR ||
	    reply->code == SOUP_FTP_PARAM_SYNTAX_ERROR ||
	    reply->code == SOUP_FTP_SERVICE_UNAVAILABLE ||
	    reply->code == SOUP_FTP_NOT_LOGGED)
	{
		g_set_error_literal (error,
				     SOUP_PROTOCOL_FTP_ERROR,
				     reply->code,
				     g_strdup (reply->message->str));
		ftp_reply_free (reply);
		return NULL;
	}

	else if (reply->code == SOUP_FTP_CMD_NOT_IMPLEMENTED)
	{
		/**
		 * Active connection
		 **/
		g_set_error_literal (error,
				     SOUP_PROTOCOL_FTP_ERROR,
				     SOUP_FTP_ACTIVE_NOT_IMPLEMENTED,
				     "Active connection not yet implemented");
		ftp_reply_free (reply);
		return NULL;
	}

	else
	{
		/**
		 * Passive connection (h1,h2,h3,h4,p1,p2)
		 **/
		regex = g_regex_new ("([0-9]*),([0-9]*),([0-9]*),([0-9]*),([0-9]*),([0-9]*)", 0, 0, NULL);
		split = g_regex_split_full (regex, reply->message->str, -1, 0, 0, 6, NULL);
		g_regex_unref (regex);
		ftp_reply_free (reply);
		/**
		 * if IPV4
		 **/
		buffer = g_strdup_printf ("%s.%s.%s.%s", split[1], split[2], split[3], split[4]);
		/**
		 * if IPV6
		 * USE PASV
		 * do not use the h1,h2,h3,h4
		 * use the same address as cmd_connection
		 * USE EPSV
		 **/

		port = 256 * atoi (split[5]) + atoi (split[6]);

		g_strfreev (split);

		data_connection = g_socket_client_connect_to_host (g_socket_client_new (),
								   buffer,
								   port,
								   cancellable,
								   error);
		g_free (buffer);

		if (data_connection == NULL) {
			return NULL;
		}
	}

	uri_decode = soup_uri_decode (uri->path);

	if (uri_decode == NULL) {
		g_set_error_literal (error,
				     SOUP_PROTOCOL_FTP_ERROR,
				     SOUP_FTP_INVALID_PATH,
				     "Path decode failed");
		g_free (uri_decode);
		return NULL;
	}

	buffer = g_strdup_printf ("RETR %s", uri->path);

	if (ftp_send_command (control, buffer, cancellable, error) == FALSE) {
		g_free (buffer);
		return NULL;
	}

	g_free (buffer);

	reply = ftp_receive_reply (control, cancellable, error);

	if (reply == NULL) {
		return NULL;
	}

	ftp_reply_free (reply);

	reply = ftp_receive_reply (control, cancellable, error);

	if (reply == NULL) {
		return NULL;
	}

	ftp_reply_free (reply);

	return data_connection;
}

GQuark
soup_protocol_ftp_error_quark (void)
{
	static GQuark error;
	if (!error)
		error = g_quark_from_static_string ("soup_protocol_ftp_error_quark");
	return error;
}

gboolean
ftp_uri_hash_equal (gconstpointer a,
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

void
ftp_reply_free (SoupProtocolFTPReply *reply)
{
	g_string_free (reply->message, TRUE);
}
