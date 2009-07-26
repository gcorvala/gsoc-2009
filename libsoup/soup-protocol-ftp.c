#include "soup-protocol-ftp.h"
#include "soup-misc.h"
#include "ParseFTPList.h"
#include <string.h>
#include <stdlib.h>

/**
 * TODO:
 * Remove GCancellable parameter from internal methods and use context->async_cancellable
 **/

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
	gboolean	multi_line;	// must be TRUE if reply is multi_line, actually, alway FALSE ( see ftp_receive_reply )
} SoupProtocolFTPReply;

typedef struct
{
	SoupProtocolFTP		*protocol;
	SoupURI			*uri;
	guint16			 features;
	GSocketConnection	*control;	// not needed but, if unref, it close control_input & control_output streams
	GDataInputStream	*control_input;
	GOutputStream		*control_output;
	GSocketConnection	*data;		// not needed but, if unref, it close input returned to caller

	/* main async call (load_uri_async) */
	GCancellable		*async_cancellable;
	GSimpleAsyncResult	*async_result;
	GError			*async_error;
	/* internal async call vars*/
	GSimpleAsyncResult	*_async_result;
	GAsyncReadyCallback	 _async_callback;
} SoupProtocolFTPContext;

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

#define SOUP_PARSE_FTP_STATUS(buffer) (g_ascii_digit_value (buffer[0]) * 100 \
				      + g_ascii_digit_value (buffer[1]) * 10 \
				      + g_ascii_digit_value (buffer[2]))

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

/* communication methods */
SoupProtocolFTPReply *ftp_receive_reply			(SoupProtocolFTP	 *protocol,
							 SoupProtocolFTPContext *context,
							 GCancellable		 *cancellable,
							 GError			**error);
void		      ftp_receive_reply_async		(SoupProtocolFTP	 *protocol,
							 SoupProtocolFTPContext	 *context,
							 GCancellable		 *cancellable,
							 GAsyncReadyCallback	  callback);
void		      ftp_receive_reply_multi_async	(GObject *source_object,
							 GAsyncResult *res,
							 gpointer user_data);
SoupProtocolFTPReply *ftp_receive_reply_finish		(SoupProtocolFTP	 *protocol,
							 SoupProtocolFTPContext	 *context,
							 GAsyncResult		 *result,
							 GError			**error);

gboolean	      ftp_send_command			(SoupProtocolFTP	 *protocol,
							 SoupProtocolFTPContext	 *context,
							 const gchar		 *str,
							 GCancellable		 *cancellable,
							 GError			**error);
void		      ftp_send_command_async		(SoupProtocolFTP	 *protocol,
							 SoupProtocolFTPContext	 *context,
							 const gchar		 *str,
							 GCancellable		 *cancellable,
							 GAsyncReadyCallback	  callback);
void		      ftp_send_command_callback		(GObject		 *source_object,
							 GAsyncResult		 *result,
							 gpointer		  user_data);
gboolean	      ftp_send_command_finish		(SoupProtocolFTP	 *protocol,
							 SoupProtocolFTPContext	 *context,
							 GAsyncResult		 *result,
							 GError			**error);
SoupProtocolFTPReply *ftp_send_and_recv			(SoupProtocolFTP	 *protocol,
							 SoupProtocolFTPContext	 *context,
							 const gchar		 *str,
							 GCancellable		 *cancellable,
							 GError			**error);
void		      ftp_send_and_recv_async		(SoupProtocolFTP	 *protocol,
							 SoupProtocolFTPContext	 *context,
							 const gchar		 *str,
							 GCancellable		 *cancellable,
							 GAsyncReadyCallback	  callback);
void		      ftp_send_and_recv_async_cb	(GObject *source_object,
							 GAsyncResult *res,
							 gpointer user_data);
SoupProtocolFTPReply *ftp_send_and_recv_finish		(SoupProtocolFTP	 *protocol,
							 SoupProtocolFTPContext	 *context,
							 GAsyncResult		 *result,
							 GError			**error);
/* parsing methods */
gboolean	      ftp_parse_feat_reply		(SoupProtocolFTPContext	 *data,
							 SoupProtocolFTPReply	 *reply);
GSocketConnectable   *ftp_parse_pasv_reply		(SoupProtocolFTPContext	 *data,
							 SoupProtocolFTPReply	 *reply);
void		      ftp_parse_list_reply		(const gchar		 *str);
/* async callbacks */
void		      ftp_callback_conn			(GObject *source_object,
							 GAsyncResult *res,
							 gpointer user_data);
void		      ftp_callback_welcome		(GObject *source_object,
							 GAsyncResult *res,
							 gpointer user_data);
void		      ftp_callback_user			(GObject *source_object,
							 GAsyncResult *res,
							 gpointer user_data);
void		      ftp_callback_pass			(GObject *source_object,
							 GAsyncResult *res,
							 gpointer user_data);
void		      ftp_callback_feat			(GObject *source_object,
							 GAsyncResult *res,
							 gpointer user_data);
void		      ftp_callback_pasv			(GObject *source_object,
							 GAsyncResult *res,
							 gpointer user_data);
void		      ftp_callback_data			(GObject *source_object,
							 GAsyncResult *res,
							 gpointer user_data);
void		      ftp_callback_retr			(GObject *source_object,
							 GAsyncResult *res,
							 gpointer user_data);
void		      ftp_callback_end			(GObject *source_object,
							 GAsyncResult *res,
							 gpointer user_data);

/* misc methods */
GQuark		      soup_protocol_ftp_error_quark	(void);

gboolean	      ftp_uri_hash_equal		(gconstpointer a,
							 gconstpointer b);

/* structs methods */
void		      ftp_reply_free			(SoupProtocolFTPReply *reply);
SoupProtocolFTPReply *ftp_reply_copy			(SoupProtocolFTPReply *reply);
void		      ftp_context_free			(SoupProtocolFTPContext *data);

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
						   ftp_context_free);
}

SoupProtocol *
soup_protocol_ftp_new (void)
{
	SoupProtocolFTP *self;

	self = g_object_new (SOUP_TYPE_PROTOCOL_FTP, NULL);

	return SOUP_PROTOCOL (self);
}

gboolean
ftp_parse_feat_reply (SoupProtocolFTPContext *data,
		      SoupProtocolFTPReply *reply)
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

GSocketConnectable *
ftp_parse_pasv_reply (SoupProtocolFTPContext *data,
		      SoupProtocolFTPReply *reply)
{
	GSocketConnectable *conn;
	GRegex *regex;
	gchar **split;
	gchar *hostname;
	guint16 port;

	if (reply->code != 227)
		return NULL;
	regex = g_regex_new ("([0-9]*),([0-9]*),([0-9]*),([0-9]*),([0-9]*),([0-9]*)", 0, 0, NULL);
	split = g_regex_split_full (regex, reply->message->str, -1, 0, 0, 6, NULL);
	hostname = g_strdup_printf ("%s.%s.%s.%s", split[1], split[2], split[3], split[4]);
	port = 256 * atoi (split[5]) + atoi (split[6]);
	conn = g_network_address_new (hostname, port);
	g_regex_unref (regex);
	g_strfreev (split);
	g_free (hostname);

	return conn;
}

GInputStream *
soup_protocol_ftp_load_uri (SoupProtocol		*protocol,
			    SoupURI			*uri,
			    GCancellable		*cancellable,
			    GError		       **error)
{
	SoupProtocolFTP *protocol_ftp;
	SoupProtocolFTPPrivate *priv;
	GInputStream *input_stream;
	GSocketConnection *data;
	SoupProtocolFTPContext *context;
	SoupProtocolFTPReply *reply;
	GSocketConnectable *conn;
	gchar *msg, *uri_decode;

	g_debug ("soup_protocol_ftp_load_uri called");
	g_return_val_if_fail (SOUP_IS_PROTOCOL_FTP (protocol), NULL);

	protocol_ftp = SOUP_PROTOCOL_FTP (protocol);
	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol_ftp);

	context = g_hash_table_lookup (priv->connections, uri);
	if (!context) {
		context = g_malloc0 (sizeof (SoupProtocolFTPContext));
		context->protocol = g_object_ref (protocol_ftp);
		g_hash_table_insert (priv->connections, soup_uri_copy (uri), context);
	}
	context->uri = soup_uri_copy (uri);
	context->async_cancellable = cancellable;
	if (!G_IS_SOCKET_CONNECTION (context->control)) {
		context->control = g_socket_client_connect_to_host (g_socket_client_new (),
								    uri->host,
								    uri->port,
								    context->async_cancellable,
								    error);
		if (!context->control)
			return NULL;
	}
	if (!G_IS_DATA_INPUT_STREAM (context->control_input)) {
		context->control_input = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (context->control)));
		g_data_input_stream_set_newline_type (context->control_input, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);
	}
	if (!G_IS_OUTPUT_STREAM (context->control_output))
		context->control_output = g_io_stream_get_output_stream (G_IO_STREAM (context->control));
	reply = ftp_receive_reply (protocol_ftp, context, context->async_cancellable, error);
	if (reply) {
		switch (reply->code) {
			case 120:
				ftp_reply_free (reply);
				g_set_error (error,
					     SOUP_PROTOCOL_FTP_ERROR,
					     SOUP_FTP_SERVICE_UNAVAILABLE,
					     "Service unavailable");
				g_hash_table_remove (priv->connections, context->uri);
				return NULL;
			case 220:
				ftp_reply_free (reply);
				msg = g_strdup_printf ("USER %s", context->uri->user);
				reply = ftp_send_and_recv (protocol,
							   context,
							   msg,
							   context->async_cancellable,
							   error);
				g_free (msg);
				break;
			case 421:
				g_debug ("welcome failed > error");
				break;
			default:
				g_debug ("welcome receive unknow answer > error");
		}
	}
	if (reply) {
		switch (reply->code) {
			//case 230:
				//ok send FEAT
			case 331:
				ftp_reply_free (reply);
				msg = g_strdup_printf ("PASS %s", context->uri->password);
				reply = ftp_send_and_recv (protocol,
							   context,
							   msg,
							   context->async_cancellable,
							   error);
				g_free (msg);
				break;
			//case 332:
				//ok send ACCT
			//case 530:
			//case 500:
			//case 501:
				//error
			//case 421:
				//not available
		}
	}
	if (reply) {
		switch (reply->code) {
			//case 202:
			case 230:
				ftp_reply_free (reply);
				reply = ftp_send_and_recv (protocol,
							   context,
							   "FEAT",
							   context->async_cancellable,
							   error);
				break;
				//ok send PASS
			//case 332:
				//ok send ACCT
			//case 530:
			//case 500:
			//case 501:
			//case 503:
				//error
			//case 421:
				//not available
		}
	}
	if (reply) {
		switch (reply->code) {
			case 211:
				ftp_parse_feat_reply (context, reply);
				ftp_reply_free (reply);
				reply = ftp_send_and_recv (protocol,
							   context,
							   "PASV",
							   context->async_cancellable,
							   error);
				break;
		}
	}
	if (reply) {
		switch (reply->code) {
			case 227:
				conn = ftp_parse_pasv_reply (context, reply);
				ftp_reply_free (reply);
				context->data = g_socket_client_connect (g_socket_client_new (),
									 conn,
									 context->async_cancellable,
									 error);
				break;
		}
	}
	if (G_IS_SOCKET_CONNECTION (context->data)) {
		uri_decode = soup_uri_decode (context->uri->path);
		if (uri_decode) {
			msg = g_strdup_printf ("RETR %s", uri_decode);
			reply = ftp_send_and_recv (protocol,
						   context,
						   msg,
						   context->async_cancellable,
						   error);
			g_free (uri_decode);
			g_free (msg);
		}
		else {
			g_set_error (error,
				     SOUP_PROTOCOL_FTP_ERROR,
				     SOUP_FTP_INVALID_PATH,
				     "Path decode failed");
			g_hash_table_remove (priv->connections, context->uri);
			return NULL;
		}
	}
	if (reply) {
		switch (reply->code) {
			//case 110:
			case 125:
			case 150:
				ftp_reply_free (reply);
				//ftp_receive_reply_async (protocol, context, context->async_cancellable, ftp_callback_end);
				//g_simple_async_result_complete (context->async_result);
				break;
			//case 226:
			//case 250:
			//case 421:
			//case 425:
			//case 426:
			//case 450:
			//case 451:
			//case 501:
			//case 530:
			//case 550:
			default:
				g_debug ("action not defined : %u", reply->code);
		}
	}
	else {
		g_set_error (error,
			     SOUP_PROTOCOL_FTP_ERROR,
			     SOUP_FTP_INVALID_PATH,
			     "An error occurred");
		g_hash_table_remove (priv->connections, context->uri);
		return NULL;
	}

	input_stream = g_io_stream_get_input_stream (G_IO_STREAM (context->data));
	//g_object_ref (input_stream);
	//g_object_set_data_full (G_OBJECT (input_stream), "socket-connection", data, g_object_unref);

	return input_stream;
}

void
soup_protocol_ftp_load_uri_async (SoupProtocol		*protocol,
				  SoupURI		*uri,
				  GCancellable		*cancellable,
				  GAsyncReadyCallback	 callback,
				  gpointer		 user_data)
{
	SoupProtocolFTP *protocol_ftp;
	SoupProtocolFTPPrivate *priv;
	SoupProtocolFTPContext *context;

	g_debug ("soup_protocol_ftp_load_uri_async called");
	g_return_if_fail (SOUP_IS_PROTOCOL_FTP (protocol));

	protocol_ftp = SOUP_PROTOCOL_FTP (protocol);
	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol_ftp);

	context = g_hash_table_lookup (priv->connections, uri);
	if (context) {
		// TODO : get the last answer sent by sync method (change that)
		ftp_receive_reply (protocol_ftp, context, context->async_cancellable, NULL);
		if (context->uri)
			soup_uri_free (context->uri);
		context->uri = soup_uri_copy (uri);
		context->async_cancellable = cancellable;
		context->async_result = g_simple_async_result_new (G_OBJECT (protocol),
								   callback,
								   user_data,
								   soup_protocol_ftp_load_uri_async);
		ftp_send_and_recv_async (protocol_ftp, context, "PASV", context->async_cancellable, ftp_callback_pasv);
	}
	else {
		context = g_malloc0 (sizeof (SoupProtocolFTPContext));
		g_hash_table_insert (priv->connections, soup_uri_copy (uri), context);
		context->protocol = g_object_ref (protocol);
		context->uri = soup_uri_copy (uri);
		context->async_cancellable = cancellable;
		context->async_result = g_simple_async_result_new (G_OBJECT (protocol),
								   callback,
								   user_data,
								   soup_protocol_ftp_load_uri_async);
		g_socket_client_connect_to_host_async (g_socket_client_new (),
						       context->uri->host,
						       context->uri->port,
						       context->async_cancellable,
						       ftp_callback_conn,
						       context);
	}
}

GInputStream *
soup_protocol_ftp_load_uri_finish (SoupProtocol	 *protocol,
				   GAsyncResult	 *result,
				   GError	**error)
{
	GSocketConnection *data;
	GInputStream *input_stream;

	g_debug ("soup_protocol_ftp_load_uri_finish called");
	
	g_return_val_if_fail (SOUP_IS_PROTOCOL_FTP (protocol), NULL);

	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return NULL;
	data = g_simple_async_result_get_op_res_gpointer ((GSimpleAsyncResult *) result);
	input_stream =  g_io_stream_get_input_stream (G_IO_STREAM (data));

	return input_stream;
}

SoupProtocolFTPReply *
ftp_receive_reply (SoupProtocolFTP				 *protocol,
		   SoupProtocolFTPContext			 *context,
		   GCancellable					 *cancellable,
		   GError					**error)
{
	SoupProtocolFTPReply *reply = g_malloc0 (sizeof (SoupProtocolFTPReply));
	gchar *buffer;
	gsize len;
	gboolean multi_line = FALSE;

	buffer = g_data_input_stream_read_line (context->control_input, &len, cancellable, error);
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
		buffer = g_data_input_stream_read_line (context->control_input, &len, cancellable, error);

		if (SOUP_PARSE_FTP_STATUS (buffer) == reply->code &&
		    g_ascii_isspace (buffer[3]))
			multi_line = FALSE;
		else
			g_string_append (reply->message, buffer);
	}
	g_free (buffer);

	g_debug (" [sync] <--- [%u] %s", reply->code, reply->message->str);

	return reply;
}

void
ftp_receive_reply_async (SoupProtocolFTP			 *protocol,
			 SoupProtocolFTPContext			 *context,
			 GCancellable				 *cancellable,
			 GAsyncReadyCallback			  callback)
{
	SoupProtocolFTPReply *reply;

	reply = g_malloc0 (sizeof (SoupProtocolFTPReply));
	reply->message = g_string_new (NULL);
	context->_async_result = g_simple_async_result_new (G_OBJECT (protocol),
							    callback,
							    context,
							    ftp_receive_reply_async);
	g_simple_async_result_set_op_res_gpointer (context->_async_result,
						   reply,
						   ftp_reply_free);
	g_data_input_stream_read_line_async (context->control_input,
					     G_PRIORITY_DEFAULT,
					     cancellable,
					     ftp_receive_reply_multi_async,
					     context);
}

void
ftp_receive_reply_multi_async (GObject *source_object,
			       GAsyncResult *read_res,
			       gpointer user_data)
{
	SoupProtocolFTPContext *context = user_data;
	SoupProtocolFTPReply *reply;
	GError *error = NULL;
	gsize len;
	gchar *buffer;

	buffer = g_data_input_stream_read_line_finish (context->control_input,
						       read_res,
						       &len,
						       &error);
	if (buffer) {
		reply = g_simple_async_result_get_op_res_gpointer (context->_async_result);
		if (!reply->multi_line && (!g_ascii_isdigit (buffer[0])		||
					   g_ascii_digit_value (buffer[0]) > 5	||
					   g_ascii_digit_value (buffer[0]) == 0	||
					   !g_ascii_isdigit (buffer[1])		||
					   g_ascii_digit_value (buffer[1]) > 5	||
					   !g_ascii_isdigit (buffer[2]))) {
			g_simple_async_result_set_error (context->_async_result,
							 soup_protocol_ftp_error_quark (),
							 SOUP_FTP_BAD_ANSWER,
							 "Server answer code not recognized");
			g_simple_async_result_complete (context->_async_result);
			g_free (buffer);
			return;
		}
		else if (len < 4) {
			g_simple_async_result_set_error (context->_async_result,
							 soup_protocol_ftp_error_quark (),
							 SOUP_FTP_BAD_ANSWER,
							 "Server answer too short");
			g_simple_async_result_complete (context->_async_result);
			g_free (buffer);
			return;
		}
		if (reply->message->len == 0) {
			reply->code = 100 * g_ascii_digit_value (buffer[0])
				    + 10 * g_ascii_digit_value (buffer[1])
				    + g_ascii_digit_value (buffer[2]);
			g_string_append (reply->message, buffer + 4);
			if (buffer[3] == '-')
				reply->multi_line = TRUE;
			g_free (buffer);
		}
		else if (reply->multi_line) {
			g_string_append_c (reply->message, '\n');
			if (SOUP_PARSE_FTP_STATUS (buffer) == reply->code) {
				if (g_ascii_isspace (buffer[3])) {
					reply->multi_line = FALSE;
					g_string_append (reply->message, buffer + 4);
				}
				else if (buffer[3] == '-')
					g_string_append (reply->message, buffer + 4);
			}
			else
				g_string_append (reply->message, buffer);
			g_free (buffer);
		}
		if (reply->multi_line) {
			g_data_input_stream_read_line_async (context->control_input,
							     G_PRIORITY_DEFAULT,
							     context->async_cancellable,
							     ftp_receive_reply_multi_async,
							     context);
			return;
		}
		else {
			g_debug ("[async] <--- [%u] %s", reply->code, reply->message->str);
			g_simple_async_result_complete (context->_async_result);
			return;
		}
	}
	else {
		g_simple_async_result_set_from_error (context->_async_result,
						      error);
		g_simple_async_result_complete (context->_async_result);
		g_error_free (error);
		return;
	}
}

SoupProtocolFTPReply *
ftp_receive_reply_finish (SoupProtocolFTP			 *protocol,
			  SoupProtocolFTPContext		 *context,
			  GAsyncResult				 *result,
			  GError				**error)
{
	SoupProtocolFTPReply *reply;

	reply = ftp_reply_copy (g_simple_async_result_get_op_res_gpointer ((GSimpleAsyncResult *) result));
	g_object_unref (context->_async_result);
	context->_async_result = NULL;

	return reply;
}

gboolean
ftp_send_command (SoupProtocolFTP	 *protocol,
		  SoupProtocolFTPContext *context,
		  const gchar		 *str,
		  GCancellable		 *cancellable,
		  GError		**error)
{
	gchar *request;
	gssize bytes_written;
	gboolean success;

	request = g_strconcat (str, "\r\n", NULL);
	bytes_written = g_output_stream_write (context->control_output,
					       request,
					       strlen (request),
					       cancellable,
					       error);
	success = bytes_written == strlen (request);
	g_free (request);
	g_debug (" [sync] ---> %s", str);

	return success;
}

void
ftp_send_command_async (SoupProtocolFTP	 *protocol,
			SoupProtocolFTPContext	*context,
			const gchar		*str,
			GCancellable		*cancellable,
			GAsyncReadyCallback	 callback)
{
	gchar *request;

	g_debug ("[async] ---> %s", str);

	context->_async_result = g_simple_async_result_new (G_OBJECT (protocol),
							    callback,
							    context,
							    ftp_send_command_async);
	request = g_strconcat (str, "\r\n", NULL);
	g_simple_async_result_set_op_res_gssize (context->_async_result, strlen (request));
	g_output_stream_write_async (G_OUTPUT_STREAM (context->control_output),
				     request,
				     strlen (request),
				     G_PRIORITY_DEFAULT,
				     cancellable,
				     ftp_send_command_callback,
				     context);
	g_free (request);
}

void
ftp_send_command_callback (GObject		 *source_object,
			   GAsyncResult		 *result,
			   gpointer		  user_data)
{
	SoupProtocolFTPContext *context = user_data;
	GError *error = NULL;
	gboolean success;
	gssize size;
	gssize bytes_to_write, bytes_written;

	bytes_to_write = g_simple_async_result_get_op_res_gssize (G_SIMPLE_ASYNC_RESULT (context->_async_result));
	bytes_written = g_output_stream_write_finish (G_OUTPUT_STREAM (context->control_output),
						      result,
						      &error);
	success = (bytes_to_write == bytes_written);
	g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (context->_async_result), success);
	if (size == -1) {
		g_simple_async_result_set_from_error (context->_async_result,
						      error);
		g_simple_async_result_complete (context->_async_result);
		g_error_free (error);
	}
	else {
		g_simple_async_result_complete (context->_async_result);
	}
}

gboolean
ftp_send_command_finish (SoupProtocolFTP	 *protocol,
			 SoupProtocolFTPContext	 *context,
			 GAsyncResult		 *result,
			 GError			**error)
{
	gboolean success;

	if (!g_simple_async_result_is_valid (result, G_OBJECT (protocol), ftp_send_command_async))
		g_critical ("ftp_send_command_finish FAILED");
	success = g_simple_async_result_get_op_res_gboolean ((GSimpleAsyncResult *) result);
	g_object_unref (context->_async_result);
	context->_async_result = NULL;

	return success;
}

SoupProtocolFTPReply *
ftp_send_and_recv (SoupProtocolFTP		 *protocol,
		   SoupProtocolFTPContext	 *context,
		   const gchar			 *str,
		   GCancellable			 *cancellable,
		   GError			**error)
{
	gboolean success;
	SoupProtocolFTPReply *reply;

	success = ftp_send_command (protocol, context, str, cancellable, error);
	if (success) {
		reply = ftp_receive_reply (protocol, context, cancellable, error);
		if (reply)
			return reply;
		else
			return NULL;
	}
	else
		return NULL;
}

void
ftp_send_and_recv_async (SoupProtocolFTP	 *protocol,
			 SoupProtocolFTPContext	 *context,
			 const gchar		 *str,
			 GCancellable		 *cancellable,
			 GAsyncReadyCallback	  callback)
{
	context->_async_callback = callback;
	ftp_send_command_async (protocol, context, str, cancellable, ftp_send_and_recv_async_cb);
}

void
ftp_send_and_recv_async_cb (GObject		*source_object,
			    GAsyncResult	*res,
			    gpointer		 user_data)
{
	SoupProtocolFTP *protocol = source_object;
	SoupProtocolFTPContext *context = user_data;
	GSimpleAsyncResult *result;
	GError *error = NULL;
	gboolean success;

	success = ftp_send_command_finish (protocol, context, res, &error);
	if (success) {
		ftp_receive_reply_async (context->protocol,
					 context,
					 context->async_cancellable,
					 context->_async_callback);
	}
	else {
		result = g_simple_async_result_new_from_error (G_OBJECT (protocol), context->_async_callback, context, error);
		g_simple_async_result_complete (result);
		g_object_unref (result);
		g_error_free (error);
	}
}

SoupProtocolFTPReply *
ftp_send_and_recv_finish (SoupProtocolFTP	 *protocol,
			  SoupProtocolFTPContext *context,
			  GAsyncResult		 *result,
			  GError		**error)
{
	return ftp_receive_reply_finish (protocol,
					 context,
					 result,
					 error);
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

SoupProtocolFTPReply *
ftp_reply_copy (SoupProtocolFTPReply *reply)
{
	SoupProtocolFTPReply *dup;

	dup = g_malloc0 (sizeof (SoupProtocolFTPReply));
	dup->message = g_string_new (reply->message->str);
	dup->code = reply->code;
	dup->multi_line = reply->multi_line;

	return dup;
}

void
ftp_context_free (SoupProtocolFTPContext *context)
{
	g_object_unref (context->protocol);
	soup_uri_free (context->uri);

}

/**
 * async callbacks
 **/
void
ftp_callback_conn (GObject *source_object,
		   GAsyncResult *res,
		   gpointer user_data)
{
	SoupProtocolFTPContext *context = user_data;
	SoupProtocolFTPPrivate *priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (context->protocol);
	GError *error = NULL;

	//g_debug ("ftp_callback_conn called");

	context->control = g_socket_client_connect_to_host_finish (g_socket_client_new (),
								   res,
								   &error);
	if (context->control) {
		context->control_input = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (context->control)));
		g_data_input_stream_set_newline_type (context->control_input, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);
		context->control_output = g_io_stream_get_output_stream (G_IO_STREAM (context->control));
		ftp_receive_reply_async (context->protocol,
					 context,
					 context->async_cancellable,
					 ftp_callback_welcome);
	}
	else {
		g_simple_async_result_set_from_error (context->async_result,
						      error);
		g_simple_async_result_complete (context->async_result);
		g_error_free (error);
		g_hash_table_remove (priv->connections, context->uri);
	}
}

void
ftp_callback_welcome (GObject *source_object,
		      GAsyncResult *res,
		      gpointer user_data)
{
	SoupProtocolFTP *protocol = source_object;
	SoupProtocolFTPPrivate *priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	SoupProtocolFTPContext *context = user_data;
	SoupProtocolFTPReply *reply;
	GError *error = NULL;
	gchar *msg;

	//g_debug ("ftp_callback_welcome called");

	reply = ftp_receive_reply_finish (protocol,
					  context,
					  res,
					  &error);
	if (reply) {
		switch (reply->code) {
			case 120:
				g_simple_async_result_set_error (context->async_result,
								 SOUP_PROTOCOL_FTP_ERROR,
								 SOUP_FTP_SERVICE_UNAVAILABLE,
								 "Service unavailable");
				g_simple_async_result_complete (context->async_result);
				g_hash_table_remove (priv->connections, context->uri);
				break;
			case 220:
				msg = g_strdup_printf ("USER %s", context->uri->user);
				ftp_send_and_recv_async (protocol,
							 context,
							 msg,
							 context->async_cancellable,
							 ftp_callback_user);
				g_free (msg);
				break;
			case 421:
				g_debug ("welcome failed > error");
				break;
			default:
				g_debug ("welcome receive unknow answer > error");
		}
		ftp_reply_free (reply);
	}
	else {
		g_simple_async_result_set_from_error (context->async_result,
						      error);
		g_simple_async_result_complete (context->async_result);
		g_error_free (error);
		g_hash_table_remove (priv->connections, context->uri);
	}
}

void
ftp_callback_user (GObject *source_object,
		   GAsyncResult *res,
		   gpointer user_data)
{
	SoupProtocolFTP *protocol = source_object;
	SoupProtocolFTPPrivate *priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	SoupProtocolFTPContext *context = user_data;
	SoupProtocolFTPReply *reply;
	GError *error = NULL;
	gchar *msg;

	//g_debug ("ftp_callback_user called");

	reply = ftp_send_and_recv_finish (protocol, context, res, &error);
	if (reply) {
		switch (reply->code) {
			//case 230:
				//ok send FEAT
			case 331:
				msg = g_strdup_printf ("PASS %s", context->uri->password);
				ftp_send_and_recv_async (protocol,
							 context,
							 msg,
							 context->async_cancellable,
							 ftp_callback_pass);
				g_free (msg);
				break;
			//case 332:
				//ok send ACCT
			//case 530:
			//case 500:
			//case 501:
				//error
			//case 421:
				//not available
		}
		ftp_reply_free (reply);
	}
	else {
		g_simple_async_result_set_from_error (context->async_result,
						      error);
		g_simple_async_result_complete (context->async_result);
		g_error_free (error);
		g_hash_table_remove (priv->connections, context->uri);
	}
}

void
ftp_callback_pass (GObject *source_object,
		   GAsyncResult *res,
		   gpointer user_data)
{
	SoupProtocolFTP *protocol = source_object;
	SoupProtocolFTPPrivate *priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	SoupProtocolFTPContext *context = user_data;
	SoupProtocolFTPReply *reply;
	GError *error = NULL;

	//g_debug ("ftp_callback_pass called");

	reply = ftp_receive_reply_finish (protocol, context, res, &error);
	if (reply) {
		switch (reply->code) {
			//case 202:
			case 230:
				ftp_send_and_recv_async (protocol,
							 context,
							 "FEAT",
							 context->async_cancellable,
							 ftp_callback_feat);
				break;
				//ok send PASS
			//case 332:
				//ok send ACCT
			//case 530:
			//case 500:
			//case 501:
			//case 503:
				//error
			//case 421:
				//not available
		}
		ftp_reply_free (reply);
	}
	else {
		g_simple_async_result_set_from_error (context->async_result,
						      error);
		g_simple_async_result_complete (context->async_result);
		g_error_free (error);
		g_hash_table_remove (priv->connections, context->uri);
	}
}

void
ftp_callback_feat (GObject *source_object,
		   GAsyncResult *res,
		   gpointer user_data)
{
	SoupProtocolFTP *protocol = source_object;
	SoupProtocolFTPPrivate *priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	SoupProtocolFTPContext *context = user_data;
	SoupProtocolFTPReply *reply;
	GError *error = NULL;

	//g_debug ("ftp_callback_feat called");

	reply = ftp_receive_reply_finish (protocol, context, res, &error);
	if (reply) {
		switch (reply->code) {
			case 211:
				ftp_parse_feat_reply (context, reply);
				ftp_send_and_recv_async (protocol, context, "PASV", context->async_cancellable, ftp_callback_pasv);
				break;
		}
		ftp_reply_free (reply);
	}
	else {
		g_simple_async_result_set_from_error (context->async_result,
						      error);
		g_simple_async_result_complete (context->async_result);
		g_error_free (error);
		g_hash_table_remove (priv->connections, context->uri);
	}
}

void
ftp_callback_pasv (GObject *source_object,
		   GAsyncResult *res,
		   gpointer user_data)
{
	SoupProtocolFTP *protocol = source_object;
	SoupProtocolFTPPrivate *priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	SoupProtocolFTPContext *context = user_data;
	SoupProtocolFTPReply *reply;
	GSocketConnectable *conn;
	GError *error = NULL;

	//g_debug ("ftp_callback_pasv called");

	reply = ftp_receive_reply_finish (protocol, context, res, &error);
	if (reply) {
		switch (reply->code) {
			case 227:
				conn = ftp_parse_pasv_reply (context, reply);
				g_socket_client_connect_async (g_socket_client_new (),
							       conn,
							       context->async_cancellable,
							       ftp_callback_data,
							       context);
				break;
		}
		ftp_reply_free (reply);
	}
	else {
		g_simple_async_result_set_from_error (context->async_result,
						      error);
		g_simple_async_result_complete (context->async_result);
		g_error_free (error);
		g_hash_table_remove (priv->connections, context->uri);
	}
}

void
ftp_callback_data (GObject *source_object,
		   GAsyncResult *res,
		   gpointer user_data)
{
	GSocketClient *client = source_object;
	SoupProtocolFTPContext *context = user_data;
	SoupProtocolFTPPrivate *priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (context->protocol);
	GError *error = NULL;
	gchar *uri_decode, *msg;

	//g_debug ("ftp_callback_data called");

	context->data = g_socket_client_connect_finish (client,
							res,
							&error);
	if (context->data) {
		g_simple_async_result_set_op_res_gpointer (context->async_result, context->data, g_object_unref);
		uri_decode = soup_uri_decode (context->uri->path);
		if (uri_decode) {
			msg = g_strdup_printf ("RETR %s", uri_decode);
			ftp_send_and_recv_async (context->protocol, context, msg, context->async_cancellable, ftp_callback_retr);
			g_free (uri_decode);
			g_free (msg);
		}
		else {
			g_simple_async_result_set_error (context->async_result,
							 SOUP_PROTOCOL_FTP_ERROR,
							 SOUP_FTP_INVALID_PATH,
							 "Path decode failed");
			g_simple_async_result_complete (context->async_result);
			g_hash_table_remove (priv->connections, context->uri);
		}
	}
	else {
		g_simple_async_result_set_from_error (context->async_result,
						      error);
		g_simple_async_result_complete (context->async_result);
		g_error_free (error);
		g_hash_table_remove (priv->connections, context->uri);
	}
}

void
ftp_callback_retr (GObject *source_object,
		   GAsyncResult *res,
		   gpointer user_data)
{
	SoupProtocolFTP *protocol = source_object;
	SoupProtocolFTPPrivate *priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	SoupProtocolFTPContext *context = user_data;
	SoupProtocolFTPReply *reply;
	GError *error = NULL;

	//g_debug ("ftp_callback_retr called");

	reply = ftp_receive_reply_finish (protocol, context, res, &error);
	if (reply) {
		switch (reply->code) {
			//case 110:
			case 125:
			case 150:
				ftp_receive_reply_async (protocol, context, context->async_cancellable, ftp_callback_end);
				g_simple_async_result_complete (context->async_result);
				break;
			//case 226:
			//case 250:
			//case 421:
			//case 425:
			//case 426:
			//case 450:
			//case 451:
			//case 501:
			//case 530:
			//case 550:
			default:
				g_debug ("action not defined : %u", reply->code);
		}
		ftp_reply_free (reply);
	}
	else {
		g_simple_async_result_set_from_error (context->async_result,
						      error);
		g_simple_async_result_complete (context->async_result);
		g_error_free (error);
		g_hash_table_remove (priv->connections, context->uri);
	}

}

void
ftp_callback_end (GObject *source_object,
		  GAsyncResult *res,
		  gpointer user_data)
{
	SoupProtocolFTP *protocol = source_object;
	SoupProtocolFTPContext *context = user_data;
	SoupProtocolFTPReply *reply;
	GError *error = NULL;

	g_debug ("ftp_callback_end called");

	reply = ftp_receive_reply_finish (protocol,
					  context,
					  res,
					  &error);
	if (reply) {
		g_debug ("end success");
		g_object_unref (context->data);
		ftp_reply_free (reply);
	}
	else {
		g_debug ("end failed");
	}

}
