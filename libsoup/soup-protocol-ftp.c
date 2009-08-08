#include "soup-protocol-ftp.h"
#include "soup-misc.h"
#include "soup-input-stream.h"
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
	GSimpleAsyncResult	*_async_result_2;
	GAsyncReadyCallback	 _async_callback;
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

typedef enum {
	SOUP_FTP_NONE,
	/* 0yz used only by SoupProtocolFTP */
	SOUP_FTP_BAD_ANSWER = 0,
	SOUP_FTP_INVALID_PATH = 1,
	SOUP_FTP_ACTIVE_NOT_IMPLEMENTED = 2,
	SOUP_FTP_ERROR = 3,
	SOUP_FTP_LOGIN_ERROR = 4,
	SOUP_FTP_SERVICE_UNAVAILABLE = 5
} SoupProtocolFTPError;

#define SOUP_PARSE_FTP_STATUS(buffer)		(g_ascii_digit_value (buffer[0]) * 100 \
						+ g_ascii_digit_value (buffer[1]) * 10 \
						+ g_ascii_digit_value (buffer[2]))
#define REPLY_IS_POSITIVE_PRELIMINARY(reply)	(reply->code / 100 == 1 ? TRUE : FALSE)
#define REPLY_IS_POSITIVE_COMPLETION(reply)	(reply->code / 100 == 2 ? TRUE : FALSE)
#define REPLY_IS_POSITIVE_INTERMEDIATE(reply)	(reply->code / 100 == 3 ? TRUE : FALSE)
#define REPLY_IS_NEGATIVE_TRANSIENT(reply)	(reply->code / 100 == 4 ? TRUE : FALSE)
#define REPLY_IS_NEGATIVE_PERMANENT(reply)	(reply->code / 100 == 5 ? TRUE : FALSE)
#define REPLY_IS_ABOUT_SYNTAX(reply)		((reply->code % 100) / 10 == 0 ? TRUE : FALSE)
#define REPLY_IS_ABOUT_INFORMATION(reply)	((reply->code % 100) / 10 == 1 ? TRUE : FALSE)
#define REPLY_IS_ABOUT_CONNECTION(reply)	((reply->code % 100) / 10 == 2 ? TRUE : FALSE)
#define REPLY_IS_ABOUT_AUTHENTICATION(reply)	((reply->code % 100) / 10 == 3 ? TRUE : FALSE)
#define REPLY_IS_ABOUT_UNSPECIFIED(reply)	((reply->code % 100) / 10 == 4 ? TRUE : FALSE)
#define REPLY_IS_ABOUT_FILE_SYSTEM(reply)	((reply->code % 100) / 10 == 5 ? TRUE : FALSE)


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
							 GCancellable		 *cancellable,
							 GError			**error);
void		      ftp_receive_reply_async		(SoupProtocolFTP	 *protocol,
							 GCancellable		 *cancellable,
							 GAsyncReadyCallback	  callback);
void		      ftp_receive_reply_multi_async	(GObject		 *source_object,
							 GAsyncResult		 *res,
							 gpointer		  user_data);
SoupProtocolFTPReply *ftp_receive_reply_finish		(SoupProtocolFTP	 *protocol,
							 GAsyncResult		 *result,
							 GError			**error);
gboolean	      ftp_send_command			(SoupProtocolFTP	 *protocol,
							 const gchar		 *str,
							 GCancellable		 *cancellable,
							 GError			**error);
void		      ftp_send_command_async		(SoupProtocolFTP	 *protocol,
							 const gchar		 *str,
							 GCancellable		 *cancellable,
							 GAsyncReadyCallback	  callback);
void		      ftp_send_command_callback		(GObject		 *source_object,
							 GAsyncResult		 *result,
							 gpointer		  user_data);
gboolean	      ftp_send_command_finish		(SoupProtocolFTP	 *protocol,
							 GAsyncResult		 *result,
							 GError			**error);
SoupProtocolFTPReply *ftp_send_and_recv			(SoupProtocolFTP	 *protocol,
							 const gchar		 *str,
							 GCancellable		 *cancellable,
							 GError			**error);
void		      ftp_send_and_recv_async		(SoupProtocolFTP	 *protocol,
							 const gchar		 *str,
							 GCancellable		 *cancellable,
							 GAsyncReadyCallback	  callback);
void		      ftp_send_and_recv_async_cb	(GObject *source_object,
							 GAsyncResult *res,
							 gpointer user_data);
SoupProtocolFTPReply *ftp_send_and_recv_finish		(SoupProtocolFTP	 *protocol,
							 GAsyncResult		 *result,
							 GError			**error);
/* parsing methods */
gboolean	      ftp_parse_feat_reply		(SoupProtocolFTP	 *protocol,
							 SoupProtocolFTPReply	 *reply);
GSocketConnectable   *ftp_parse_pasv_reply		(SoupProtocolFTP	 *protocol,
							 SoupProtocolFTPReply	 *reply);
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
/* misc methods */
GQuark		      soup_protocol_ftp_error_quark	(void);

gboolean	      ftp_uri_hash_equal		(gconstpointer a,
							 gconstpointer b);

/* structs methods */
void		      ftp_reply_free			(SoupProtocolFTPReply *reply);
SoupProtocolFTPReply *ftp_reply_copy			(SoupProtocolFTPReply *reply);

static void
soup_protocol_ftp_finalize (GObject *object)
{
	SoupProtocolFTP *ftp;
	SoupProtocolFTPPrivate *priv;

	ftp = SOUP_PROTOCOL_FTP (object);
	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (ftp);

	if (priv->uri)
		soup_uri_free (priv->uri);
	ftp_send_command (ftp, "QUIT", NULL, NULL);
	if (priv->control)
		g_object_unref (priv->control);
	if (priv->data)
		g_object_unref (priv->data);
	if (priv->async_result)
		g_object_unref (priv->async_result);
	if (priv->async_error)
		g_error_free (priv->async_error);
	if (priv->_async_result)
		g_object_unref (priv->_async_result);

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
}

SoupProtocol *
soup_protocol_ftp_new (void)
{
	SoupProtocolFTP *self;

	self = g_object_new (SOUP_TYPE_PROTOCOL_FTP, NULL);

	return SOUP_PROTOCOL (self);
}

gboolean
ftp_parse_feat_reply (SoupProtocolFTP		*protocol,
		      SoupProtocolFTPReply	*reply)
{
	SoupProtocolFTPPrivate *priv;
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

	g_return_val_if_fail (SOUP_IS_PROTOCOL_FTP (protocol), FALSE);
	g_return_val_if_fail (reply != NULL, FALSE);

	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	if (reply->code != 211)
		return FALSE;
	split = g_strsplit (reply->message->str, "\n", 0);
	for (i = 1; split[i + 1]; ++i)
	{
		if (!g_ascii_isspace (split[i][0])) {
			priv->features = 0;
			g_strfreev (split);
			return FALSE;
		}
		feature = g_strstrip (split[i]);
		for (j = 0; j < G_N_ELEMENTS (features); ++j)
		{
			if (g_ascii_strncasecmp (feature, features[j].name, 4) == 0) {
				g_debug ("[FEAT] %s supported", features[j].name);
				priv->features |= features[j].enable;
			}
		}
	}
	g_strfreev (split);

	return TRUE;
}

GSocketConnectable *
ftp_parse_pasv_reply (SoupProtocolFTP		*protocol,
		      SoupProtocolFTPReply	*reply)
{
	GSocketConnectable *conn;
	GRegex *regex;
	gchar **split;
	gchar *hostname;
	guint16 port;

	g_return_val_if_fail (SOUP_IS_PROTOCOL_FTP (protocol), NULL);
	g_return_val_if_fail (reply != NULL, NULL);

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

gboolean
ftp_check_reply (SoupProtocolFTP	 *protocol,
		 SoupProtocolFTPReply	 *reply,
		 GError			**error)
{
	g_return_val_if_fail (SOUP_IS_PROTOCOL_FTP (protocol), FALSE);
	g_return_val_if_fail (reply != NULL, FALSE);

	if (REPLY_IS_POSITIVE_PRELIMINARY (reply) ||
	    REPLY_IS_POSITIVE_COMPLETION (reply) ||
	    REPLY_IS_POSITIVE_INTERMEDIATE (reply))
		return TRUE;
	else if (REPLY_IS_NEGATIVE_TRANSIENT (reply)) {
		if (REPLY_IS_ABOUT_CONNECTION (reply)) {
			g_set_error_literal (error,
					     SOUP_PROTOCOL_FTP_ERROR,
					     0,
					     "Connection : try again later");
		}
		else if (REPLY_IS_ABOUT_FILE_SYSTEM (reply)) {
			g_set_error_literal (error,
					     SOUP_PROTOCOL_FTP_ERROR,
					     0,
					     "File system : try again later");
		}
		else {
			g_set_error (error,
				     SOUP_PROTOCOL_FTP_ERROR,
				     0,
				     "Unknow : try again later (%u - %s)",
				     reply->code,
				     reply->message->str);
		}
		return FALSE;
	}
	else if (REPLY_IS_NEGATIVE_PERMANENT (reply)) {
		if (REPLY_IS_ABOUT_SYNTAX (reply)) {
			g_set_error_literal (error,
					     SOUP_PROTOCOL_FTP_ERROR,
					     0,
					     "Syntax : command failed");
		}
		if (REPLY_IS_ABOUT_AUTHENTICATION (reply)) {
			g_set_error_literal (error,
					     SOUP_PROTOCOL_FTP_ERROR,
					     0,
					     "Authentication : login failed (incorrect username or password)");
		}
		if (REPLY_IS_ABOUT_FILE_SYSTEM (reply)) {
			g_set_error_literal (error,
					     SOUP_PROTOCOL_FTP_ERROR,
					     0,
					     "File system : file action failed (invalid path or no access allowed)");
		}
		else {
			g_set_error (error,
				     SOUP_PROTOCOL_FTP_ERROR,
				     0,
				     "Unknow : ftp fatal error (%u - %s)",
				     reply->code,
				     reply->message->str);
		}
		return FALSE;
	}
	else {
		g_set_error (error,
			     SOUP_PROTOCOL_FTP_ERROR,
			     0,
			     "Unknown : error detected (%u - %s)",
			     reply->code,
			     reply->message->str);
		return FALSE;
	}
}


SoupProtocolFTPReply *
ftp_receive_reply (SoupProtocolFTP				 *protocol,
		   GCancellable					 *cancellable,
		   GError					**error)
{
	SoupProtocolFTPPrivate *priv;
	SoupProtocolFTPReply *reply = g_malloc0 (sizeof (SoupProtocolFTPReply));
	gchar *buffer;
	gsize len;
	gboolean multi_line = FALSE;

	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	buffer = g_data_input_stream_read_line (priv->control_input, &len, cancellable, error);
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
		buffer = g_data_input_stream_read_line (priv->control_input, &len, cancellable, error);

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
			 GCancellable				 *cancellable,
			 GAsyncReadyCallback			  callback)
{
	SoupProtocolFTPPrivate *priv;
	SoupProtocolFTPReply *reply;

	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	reply = g_malloc0 (sizeof (SoupProtocolFTPReply));
	reply->message = g_string_new (NULL);
	priv->_async_result = g_simple_async_result_new (G_OBJECT (protocol),
							    callback,
							    NULL,
							    ftp_receive_reply_async);
	g_simple_async_result_set_op_res_gpointer (priv->_async_result,
						   reply,
						   (GDestroyNotify) ftp_reply_free);
	g_data_input_stream_read_line_async (priv->control_input,
					     G_PRIORITY_DEFAULT,
					     cancellable,
					     ftp_receive_reply_multi_async,
					     protocol);
}

void
ftp_receive_reply_multi_async (GObject *source_object,
			       GAsyncResult *read_res,
			       gpointer user_data)
{
	SoupProtocolFTP *protocol = user_data;
	SoupProtocolFTPPrivate *priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	SoupProtocolFTPReply *reply;
	GError *error = NULL;
	gsize len;
	gchar *buffer;

	buffer = g_data_input_stream_read_line_finish (priv->control_input,
						       read_res,
						       &len,
						       &error);
	if (buffer) {
		reply = g_simple_async_result_get_op_res_gpointer (priv->_async_result);
		if (!reply->multi_line && (!g_ascii_isdigit (buffer[0])		||
					   g_ascii_digit_value (buffer[0]) > 5	||
					   g_ascii_digit_value (buffer[0]) == 0	||
					   !g_ascii_isdigit (buffer[1])		||
					   g_ascii_digit_value (buffer[1]) > 5	||
					   !g_ascii_isdigit (buffer[2]))) {
			g_simple_async_result_set_error (priv->_async_result,
							 soup_protocol_ftp_error_quark (),
							 SOUP_FTP_BAD_ANSWER,
							 "Server answer code not recognized");
			g_simple_async_result_complete (priv->_async_result);
			g_free (buffer);
			return;
		}
		else if (len < 4) {
			g_simple_async_result_set_error (priv->_async_result,
							 soup_protocol_ftp_error_quark (),
							 SOUP_FTP_BAD_ANSWER,
							 "Server answer too short");
			g_simple_async_result_complete (priv->_async_result);
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
			g_data_input_stream_read_line_async (priv->control_input,
							     G_PRIORITY_DEFAULT,
							     priv->async_cancellable,
							     ftp_receive_reply_multi_async,
							     protocol);
			return;
		}
		else {
			g_debug ("[async] <--- [%u] %s", reply->code, reply->message->str);
			g_simple_async_result_complete (priv->_async_result);
			return;
		}
	}
	else {
		g_simple_async_result_set_from_error (priv->_async_result,
						      error);
		g_simple_async_result_complete (priv->_async_result);
		g_error_free (error);
		return;
	}
}

SoupProtocolFTPReply *
ftp_receive_reply_finish (SoupProtocolFTP			 *protocol,
			  GAsyncResult				 *result,
			  GError				**error)
{
	SoupProtocolFTPPrivate *priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	SoupProtocolFTPReply *reply;

	reply = ftp_reply_copy (g_simple_async_result_get_op_res_gpointer ((GSimpleAsyncResult *) result));
	g_object_unref (priv->_async_result);
	priv->_async_result = NULL;

	return reply;
}

gboolean
ftp_send_command (SoupProtocolFTP	 *protocol,
		  const gchar		 *str,
		  GCancellable		 *cancellable,
		  GError		**error)
{
	SoupProtocolFTPPrivate *priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	gchar *request;
	gssize bytes_written;
	gboolean success;

	request = g_strconcat (str, "\r\n", NULL);
	bytes_written = g_output_stream_write (priv->control_output,
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
			const gchar		*str,
			GCancellable		*cancellable,
			GAsyncReadyCallback	 callback)
{
	SoupProtocolFTPPrivate *priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	gchar *request;

	g_debug ("[async] ---> %s", str);

	priv->_async_result = g_simple_async_result_new (G_OBJECT (protocol),
							    callback,
							    NULL,
							    ftp_send_command_async);
	request = g_strconcat (str, "\r\n", NULL);
	g_simple_async_result_set_op_res_gssize (priv->_async_result, strlen (request));
	g_output_stream_write_async (G_OUTPUT_STREAM (priv->control_output),
				     request,
				     strlen (request),
				     G_PRIORITY_DEFAULT,
				     cancellable,
				     ftp_send_command_callback,
				     protocol);
	g_free (request);
}

void
ftp_send_command_callback (GObject		 *source_object,
			   GAsyncResult		 *result,
			   gpointer		  user_data)
{
	SoupProtocolFTP *protocol = user_data;
	SoupProtocolFTPPrivate *priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	GError *error = NULL;
	gboolean success;
	gssize bytes_to_write, bytes_written;

	bytes_to_write = g_simple_async_result_get_op_res_gssize (priv->_async_result);
	bytes_written = g_output_stream_write_finish (G_OUTPUT_STREAM (priv->control_output),
						      result,
						      &error);
	success = (bytes_to_write == bytes_written);
	if (bytes_written == -1) {
		g_simple_async_result_set_from_error (priv->_async_result,
						      error);
		g_simple_async_result_complete (priv->_async_result);
		g_error_free (error);
	}
	else {
		g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (priv->_async_result), success);
		g_simple_async_result_complete (priv->_async_result);
	}
}

gboolean
ftp_send_command_finish (SoupProtocolFTP	 *protocol,
			 GAsyncResult		 *result,
			 GError			**error)
{
	SoupProtocolFTPPrivate *priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	gboolean success;

	if (!g_simple_async_result_is_valid (result, G_OBJECT (protocol), ftp_send_command_async))
		g_critical ("ftp_send_command_finish FAILED");
	success = g_simple_async_result_get_op_res_gboolean ((GSimpleAsyncResult *) result);
	g_object_unref (priv->_async_result);
	priv->_async_result = NULL;

	return success;
}

SoupProtocolFTPReply *
ftp_send_and_recv (SoupProtocolFTP		 *protocol,
		   const gchar			 *str,
		   GCancellable			 *cancellable,
		   GError			**error)
{
	SoupProtocolFTPPrivate *priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	gboolean success;
	SoupProtocolFTPReply *reply;

	success = ftp_send_command (protocol, str, cancellable, error);
	if (success) {
		reply = ftp_receive_reply (protocol, cancellable, error);
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
			 const gchar		 *str,
			 GCancellable		 *cancellable,
			 GAsyncReadyCallback	  callback)
{
	SoupProtocolFTPPrivate *priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	priv->_async_callback = callback;
	ftp_send_command_async (protocol, str, cancellable, ftp_send_and_recv_async_cb);
}

void
ftp_send_and_recv_async_cb (GObject		*source_object,
			    GAsyncResult	*res,
			    gpointer		 user_data)
{
	SoupProtocolFTP *protocol;
	SoupProtocolFTPPrivate *priv;
	GSimpleAsyncResult *result;
	GError *error = NULL;
	gboolean success;

	g_warn_if_fail (SOUP_IS_PROTOCOL_FTP (source_object));
	g_warn_if_fail (G_IS_ASYNC_RESULT (res));

	protocol = SOUP_PROTOCOL_FTP (source_object);
	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	success = ftp_send_command_finish (protocol, res, &error);
	if (success) {
		ftp_receive_reply_async (protocol,
					 priv->async_cancellable,
					 priv->_async_callback);
	}
	else {
		result = g_simple_async_result_new_from_error (G_OBJECT (protocol), priv->_async_callback, NULL, error);
		g_simple_async_result_complete (result);
		g_object_unref (result);
		g_error_free (error);
	}
}

SoupProtocolFTPReply *
ftp_send_and_recv_finish (SoupProtocolFTP	 *protocol,
			  GAsyncResult		 *result,
			  GError		**error)
{
	return ftp_receive_reply_finish (protocol,
					 result,
					 error);
}

/**
 * gboolean protocol_ftp_auth (SoupProtocolFTP *protocol, GError **error)
 * void protocol_ftp_auth_async (SoupProtocolFTP *protocol, GCancellable *cancellable, GAsyncReadyCallback callback)
 * gboolean protocol_ftp_auth_finish (SoupProtocolFTP *protocol, GAsyncResult *result, GError **error)
 *
 * GInputStream * protocol_ftp_list (SoupProtocolFTP *protocol, gchar *path, GError **error)
 * void protocol_ftp_list_async (SoupProtocolFTP *protocol, gchar *path, GCancellable *cancellable, GAsyncReadyCallback callback)
 * GInputStream * protocol_ftp_list_finish (SoupProtocolFTP *protocol, GAsyncResult *result, GError **error)
 *
 * GInputStream * protocol_ftp_retr (SoupProtocolFTP *protocol, gchar *path, GError **error)
 * void protocol_ftp_retr_async (SoupProtocolFTP *protocol, gchar *path, GCancellable *cancellable, GAsyncReadyCallback callback)
 * GInputStream * protocol_ftp_retr_finish (SoupProtocolFTP *protocol, GAsyncResult *result, GError **error)
 *
 * protocol_ftp_cd (SoupProtocolFTP *protocol, gchar *path)
 * gchar * protocol_ftp_cwd (SoupProtocolFTP *protocol)
 **/

gboolean
protocol_ftp_auth (SoupProtocolFTP	 *protocol,
		   GError		**error)
{
	SoupProtocolFTPPrivate *priv;
	SoupProtocolFTPReply *reply;
	gchar *msg;

	g_return_val_if_fail (SOUP_IS_PROTOCOL_FTP (protocol), FALSE);

	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	msg = g_strdup_printf ("USER %s", priv->uri->user);
	reply = ftp_send_and_recv (protocol,
				   msg,
				   priv->async_cancellable,
				   error);
	g_free (msg);
	if (!reply)
		return FALSE;
	if (!ftp_check_reply (protocol, reply, error)) {
		ftp_reply_free (reply);
		return FALSE;
	}
	else if (reply->code == 230) {
		ftp_reply_free (reply);
		return TRUE;
	}
	else if (reply->code == 332) {
		ftp_reply_free (reply);
		g_set_error_literal (error,
				     SOUP_PROTOCOL_FTP_ERROR,
				     0,
				     "Authentication : ACCT not implemented");
		return FALSE;
	}
	else if (reply->code != 331) {
		ftp_reply_free (reply);
		g_set_error_literal (error,
				     SOUP_PROTOCOL_FTP_ERROR,
				     0,
				     "Authentication : Unexpected reply received");
		return FALSE;
	}
	ftp_reply_free (reply);
	msg = g_strdup_printf ("PASS %s", priv->uri->password);
	reply = ftp_send_and_recv (protocol,
				   msg,
				   priv->async_cancellable,
				   error);
	g_free (msg);
	if (!reply)
		return FALSE;
	if (!ftp_check_reply (protocol, reply, error)) {
		ftp_reply_free (reply);
		return FALSE;
	}
	else if (reply->code == 230) {
		ftp_reply_free (reply);
		return TRUE;
	}
	else if (reply->code == 332) {
		ftp_reply_free (reply);
		g_set_error_literal (error,
				     SOUP_PROTOCOL_FTP_ERROR,
				     0,
				     "Authentication : ACCT not implemented");
		return FALSE;
	}
	else {
		ftp_reply_free (reply);
		g_set_error_literal (error,
				     SOUP_PROTOCOL_FTP_ERROR,
				     0,
				     "Authentication : Unexpected reply received");
		return FALSE;
	}
}

void
protocol_ftp_auth_pass_cb (GObject		 *source,
			   GAsyncResult		 *res,
			   gpointer		  user_data)
{
	SoupProtocolFTP *protocol;
	SoupProtocolFTPPrivate *priv;
	SoupProtocolFTPReply *reply;
	GError *error = NULL;

	g_return_if_fail (SOUP_IS_PROTOCOL_FTP (source));
	g_return_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res));

	protocol = SOUP_PROTOCOL_FTP (source);
	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	reply = ftp_send_and_recv_finish (protocol, res, &error);

	if (!reply) {
		g_simple_async_result_set_from_error (priv->_async_result_2, error);
		g_simple_async_result_set_op_res_gboolean (priv->_async_result_2, FALSE);
		g_simple_async_result_complete (priv->_async_result_2);
		return;
	}
	if (!ftp_check_reply (protocol, reply, &error)) {
		ftp_reply_free (reply);
		g_simple_async_result_set_from_error (priv->_async_result_2, error);
		g_simple_async_result_set_op_res_gboolean (priv->_async_result_2, FALSE);
		g_simple_async_result_complete (priv->_async_result_2);
		return;
	}
	else if (reply->code == 230) {
		ftp_reply_free (reply);
		g_simple_async_result_set_op_res_gboolean (priv->_async_result_2, TRUE);
		g_simple_async_result_complete (priv->_async_result_2);
		return;
	}
	else if (reply->code == 332) {
		ftp_reply_free (reply);
		g_simple_async_result_set_error (priv->_async_result_2,
						 SOUP_PROTOCOL_FTP_ERROR,
						 0,
						 "Authentication : ACCT not implemented");
		g_simple_async_result_set_op_res_gboolean (priv->_async_result_2, FALSE);
		g_simple_async_result_complete (priv->_async_result_2);
		return;
	}
	else {
		ftp_reply_free (reply);
		g_simple_async_result_set_error (priv->_async_result_2,
						 SOUP_PROTOCOL_FTP_ERROR,
						 0,
						 "Authentication : Unexpected reply received");
		g_simple_async_result_set_op_res_gboolean (priv->_async_result_2, FALSE);
		g_simple_async_result_complete (priv->_async_result_2);
		return;
	}
}

void
protocol_ftp_auth_user_cb (GObject		 *source,
			   GAsyncResult		 *res,
			   gpointer		  user_data)
{
	SoupProtocolFTP *protocol;
	SoupProtocolFTPPrivate *priv;
	SoupProtocolFTPReply *reply;
	GError *error = NULL;
	gchar *msg;

	g_return_if_fail (SOUP_IS_PROTOCOL_FTP (source));
	g_return_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res));

	protocol = SOUP_PROTOCOL_FTP (source);
	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	reply = ftp_send_and_recv_finish (protocol, res, &error);

	if (!reply) {
		g_simple_async_result_set_from_error (priv->_async_result_2, error);
		g_simple_async_result_set_op_res_gboolean (priv->_async_result_2, FALSE);
		g_simple_async_result_complete (priv->_async_result_2);
		return;
	}
	if (!ftp_check_reply (protocol, reply, &error)) {
		ftp_reply_free (reply);
		g_simple_async_result_set_from_error (priv->_async_result_2, error);
		g_simple_async_result_set_op_res_gboolean (priv->_async_result_2, FALSE);
		g_simple_async_result_complete (priv->_async_result_2);
		return;
	}
	else if (reply->code == 230) {
		ftp_reply_free (reply);
		g_simple_async_result_set_op_res_gboolean (priv->_async_result_2, TRUE);
		g_simple_async_result_complete (priv->_async_result_2);
		return;
	}
	else if (reply->code == 332) {
		ftp_reply_free (reply);
		g_simple_async_result_set_error (priv->_async_result_2,
						 SOUP_PROTOCOL_FTP_ERROR,
						 0,
						 "Authentication : ACCT not implemented");
		g_simple_async_result_set_op_res_gboolean (priv->_async_result_2, FALSE);
		g_simple_async_result_complete (priv->_async_result_2);
		return;
	}
	else if (reply->code != 331) {
		ftp_reply_free (reply);
		g_simple_async_result_set_error (priv->_async_result_2,
						 SOUP_PROTOCOL_FTP_ERROR,
						 0,
						 "Authentication : Unexpected reply received");
		g_simple_async_result_set_op_res_gboolean (priv->_async_result_2, FALSE);
		g_simple_async_result_complete (priv->_async_result_2);
		return;
	}
	ftp_reply_free (reply);
	msg = g_strdup_printf ("PASS %s", priv->uri->password);
	ftp_send_and_recv_async (protocol,
				 msg,
				 priv->async_cancellable,
				 protocol_ftp_auth_pass_cb);
	g_free (msg);
}

void
protocol_ftp_auth_async (SoupProtocolFTP	 *protocol,
			 GCancellable		 *cancellable,
			 GAsyncReadyCallback	  callback)
{
	SoupProtocolFTPPrivate *priv;
	gchar *msg;

	g_return_if_fail (SOUP_IS_PROTOCOL_FTP (protocol));

	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	priv->_async_result_2 = g_simple_async_result_new (G_OBJECT (protocol),
							   callback,
							   NULL,
							   protocol_ftp_auth_async);
	msg = g_strdup_printf ("USER %s", priv->uri->user);
	ftp_send_and_recv_async (protocol,
				 msg,
				 priv->async_cancellable,
				 protocol_ftp_auth_user_cb);
	g_free (msg);
}

gboolean
protocol_ftp_auth_finish (SoupProtocolFTP	 *protocol,
			  GAsyncResult		 *result,
			  GError		**error)
{
	SoupProtocolFTPPrivate *priv;
	GSimpleAsyncResult *simple;
	gboolean res;

	g_return_val_if_fail (SOUP_IS_PROTOCOL_FTP (protocol), FALSE);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (result), FALSE);

	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	simple = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (simple, error))
		res = FALSE;
	else
		res = g_simple_async_result_get_op_res_gboolean (simple);
	g_object_unref (priv->_async_result);
	priv->_async_result = NULL;

	return res;
}

/**
 * GInputStream * protocol_ftp_list (SoupProtocolFTP *protocol, gchar *path, GError **error)
 * void protocol_ftp_list_async (SoupProtocolFTP *protocol, gchar *path, GCancellable *cancellable, GAsyncReadyCallback callback)
 * GInputStream * protocol_ftp_list_finish (SoupProtocolFTP *protocol, GAsyncResult *result, GError **error)
 **/

static void
protocol_ftp_list_complete (SoupInputStream	 *soup_stream,
			    gpointer		  user_data)
{
	SoupProtocolFTP *protocol_ftp;
	SoupProtocolFTPPrivate *priv;
	SoupProtocolFTPReply *reply;
	GError *error = NULL;

	g_debug ("FIXME : need to clean priv->data");

	g_return_if_fail (SOUP_IS_INPUT_STREAM (soup_stream));
	g_return_if_fail (SOUP_IS_PROTOCOL_FTP (user_data));

	protocol_ftp = SOUP_PROTOCOL_FTP (user_data);
	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol_ftp);
	g_signal_handlers_disconnect_by_func (soup_stream,
					      protocol_ftp_list_complete,
					      protocol_ftp);

	reply = ftp_receive_reply (protocol_ftp, NULL, NULL);
	if (reply)
		ftp_reply_free (reply);
}

gint
protocol_ftp_file_info_list_compare (gpointer	data,
				     gchar	*name)
{
	GFileInfo *info;

	g_return_val_if_fail (G_IS_FILE_INFO (data), -1);
	g_return_val_if_fail (name != NULL,-1);

	info = G_FILE_INFO (data);
	return g_strcmp0 (g_file_info_get_name (info), name);
}

gint
protocol_ftp_file_info_list_sort (gpointer	data1,
				  gpointer	data2)
{
	GFileInfo *info1, *info2;

	g_return_val_if_fail (G_IS_FILE_INFO (data1), -1);
	g_return_val_if_fail (G_IS_FILE_INFO (data2), -1);

	info1 = G_FILE_INFO (data1);
	info2 = G_FILE_INFO (data2);
	return g_strcmp0 (g_file_info_get_name (info1), g_file_info_get_name (info2));
}

GList *
protocol_ftp_list_parse (SoupProtocolFTP	*protocol,
		         GInputStream		*stream)
{
	SoupProtocolFTPPrivate *priv;
	GDataInputStream *dstream;
	struct list_state state = { 0, };
	GList *file_list = NULL;
	GFileInfo *file_info;
	gchar *buffer;
	GError *error = NULL;
	gsize len = 0;
	int type;

	g_return_val_if_fail (SOUP_IS_PROTOCOL_FTP (protocol), NULL);
	g_return_val_if_fail (G_IS_INPUT_STREAM (stream), NULL);

	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	dstream = g_data_input_stream_new (stream);
	g_data_input_stream_set_newline_type (dstream, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);

	while (buffer = g_data_input_stream_read_line (dstream, &len, NULL, &error)) {
		struct list_result result = { 0, };
		type = ParseFTPList (buffer, &state, &result);
		file_info = g_file_info_new();
		if (result.fe_type == 'f')
			g_file_info_set_file_type (file_info, G_FILE_TYPE_REGULAR);
		else if (result.fe_type == 'd')
			g_file_info_set_file_type (file_info, G_FILE_TYPE_DIRECTORY);
		else if (result.fe_type == 'l') {
			g_file_info_set_file_type (file_info, G_FILE_TYPE_SYMBOLIC_LINK);
			g_file_info_set_is_symlink (file_info, TRUE);
		}
		else {
			g_file_info_set_file_type (file_info, G_FILE_TYPE_UNKNOWN);
			continue;
		}
		g_file_info_set_name (file_info, g_strdup (result.fe_fname));
		g_file_info_set_size (file_info, atoi (result.fe_size));
		file_list = g_list_prepend (file_list, file_info);
	}

	g_object_unref (dstream);
	file_list = g_list_sort (file_list, protocol_ftp_file_info_list_sort);

	return file_list;
}

GInputStream *
protocol_ftp_list (SoupProtocolFTP	 *protocol,
		   gchar		 *path,
		   GError		**error)
{
	SoupProtocolFTPPrivate *priv;
	SoupProtocolFTPReply *reply;
	SoupInputStream *sstream;
	GInputStream *istream;
	GSocketConnectable *conn;
	GSocketClient *client;
	GList *file_list = NULL;
	GFileInfo *dir_info;
	gchar *msg;

	g_return_val_if_fail (SOUP_IS_PROTOCOL_FTP (protocol), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);

	reply = ftp_send_and_recv (protocol,
				   "PASV",
				   priv->async_cancellable,
				   error);
	if (!reply)
		return NULL;
	if (!ftp_check_reply (protocol, reply, error)) {
		ftp_reply_free (reply);
		return NULL;
	}
	if (reply->code != 227) {
		ftp_reply_free (reply);
		g_set_error_literal (error,
				     SOUP_PROTOCOL_FTP_ERROR,
				     0,
				     "Directory listing : Unexpected reply received");
		return NULL;
	}
	conn = ftp_parse_pasv_reply (protocol, reply);
	ftp_reply_free (reply);
	client = g_socket_client_new ();
	priv->data = g_socket_client_connect (client,
					      conn,
					      priv->async_cancellable,
					      error);
	g_object_unref (client);
	g_object_unref (conn);
	if (!priv->data)
		return NULL;
	msg = g_strdup_printf ("LIST .%s", path);
	reply = ftp_send_and_recv (protocol,
				   msg,
				   priv->async_cancellable,
				   error);
	g_free (msg);
	if (!reply)
		return NULL;
	if (!ftp_check_reply (protocol, reply, error)) {
		ftp_reply_free (reply);
		return NULL;
	}
	if (reply->code != 125 && reply->code != 150) {
		ftp_reply_free (reply);
		g_set_error_literal (error,
				     SOUP_PROTOCOL_FTP_ERROR,
				     0,
				     "Directory listing : Unexpected reply received");
		return NULL;
	}
	ftp_reply_free (reply);
	istream = g_io_stream_get_input_stream (G_IO_STREAM (priv->data));
	dir_info = g_file_info_new ();
	g_file_info_set_name (dir_info, path);
	g_file_info_set_file_type (dir_info, G_FILE_TYPE_DIRECTORY);
	sstream = soup_input_stream_new (istream, dir_info, NULL);
	g_object_unref (dir_info);
	g_signal_connect (sstream, "end-of-stream",
			  G_CALLBACK (protocol_ftp_list_complete), protocol);
	g_signal_connect (sstream, "stream-closed",
			  G_CALLBACK (protocol_ftp_list_complete), protocol);
	file_list = protocol_ftp_list_parse (protocol, G_OBJECT (sstream));
	g_object_set (sstream,
		      "children", file_list,
		      NULL);

	return G_INPUT_STREAM (sstream);
}

static void
protocol_ftp_retr_complete (SoupInputStream	 *soup_stream,
			    gpointer		  user_data)
{
	SoupProtocolFTP *protocol_ftp;
	SoupProtocolFTPPrivate *priv;
	SoupProtocolFTPReply *reply;
	GError *error = NULL;

	g_return_if_fail (SOUP_IS_INPUT_STREAM (soup_stream));
	g_return_if_fail (SOUP_IS_PROTOCOL_FTP (user_data));

	g_debug ("FIXME, load_uri_complete, clean up protocol_ftp");

	protocol_ftp = user_data;
	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol_ftp);
	g_signal_handlers_disconnect_by_func (soup_stream, protocol_ftp_retr_complete, protocol_ftp);

	soup_uri_free (priv->uri);
	priv->uri = NULL;
	g_object_unref (priv->data);
	priv->data = NULL;
	reply = ftp_receive_reply (protocol_ftp, NULL, NULL);
	if (reply)
		ftp_reply_free (reply);
}

GInputStream *
soup_protocol_ftp_load_uri (SoupProtocol		*protocol,
			    SoupURI			*uri,
			    GCancellable		*cancellable,
			    GError		       **error)
{
	SoupProtocolFTP *protocol_ftp;
	SoupProtocolFTPPrivate *priv;
	SoupProtocolFTPReply *reply;
	GInputStream *input_stream;
	GInputStream *istream;
	SoupInputStream *soup_stream;
	GSocketConnectable *conn;
	gchar *msg, *uri_decode;
	gboolean is_dir = FALSE;
	GList *listing = NULL;
	GFileInfo *info;

	g_debug ("soup_protocol_ftp_load_uri called");
	g_return_val_if_fail (SOUP_IS_PROTOCOL_FTP (protocol), NULL);
	g_return_val_if_fail (uri != NULL, NULL);

	protocol_ftp = SOUP_PROTOCOL_FTP (protocol);
	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol_ftp);
	// TODO : free private before start new job
	priv->uri = soup_uri_copy (uri);
	priv->async_cancellable = cancellable;
	if (!priv->control) {
		priv->control = g_socket_client_connect_to_host (g_socket_client_new (),
								 uri->host,
								 uri->port,
								 priv->async_cancellable,
								 error);
		if (!priv->control)
			return NULL;
	}
	if (!priv->control_input && !priv->control_output) {
		priv->control_input = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (priv->control)));
		g_data_input_stream_set_newline_type (priv->control_input, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);
		priv->control_output = g_io_stream_get_output_stream (G_IO_STREAM (priv->control));
		reply = ftp_receive_reply (protocol_ftp, priv->async_cancellable, error);
		// check welcome code
		ftp_reply_free (reply);
		if (!protocol_ftp_auth (protocol_ftp, error))
			return NULL;
	}
	if (g_str_has_suffix (uri->path, "/")) {
		g_debug ("directory detected");
		istream = protocol_ftp_list (protocol_ftp, uri->path, NULL);
		return istream;
	}
	else {
		gchar *parent_dir_path = g_strndup (uri->path, g_strrstr (uri->path, "/") - uri->path + 1);
		istream = protocol_ftp_list (protocol_ftp, parent_dir_path, NULL);
		g_free (parent_dir_path);
		g_object_get (istream,
			      "file-info", &info,
			      "children", &listing,
			      NULL);
		GList *l = g_list_find_custom (listing,
					    g_strrstr (uri->path, "/") + 1,
					    protocol_ftp_file_info_list_compare);
		if (g_file_info_get_file_type (l->data) == G_FILE_TYPE_DIRECTORY) {
			g_debug ("directory detected");
			istream = protocol_ftp_list (protocol_ftp, uri->path, NULL);
			return istream;
		}
		else if (g_file_info_get_file_type (l->data) == G_FILE_TYPE_REGULAR)
			g_debug ("file detected");
		g_object_unref (istream);
	}
	reply = ftp_send_and_recv (protocol_ftp, "FEAT", priv->async_cancellable, error);
	if (reply) {
		switch (reply->code) {
			case 211:
				ftp_parse_feat_reply (protocol_ftp, reply);
				ftp_reply_free (reply);
				reply = ftp_send_and_recv (protocol_ftp,
							   "PASV",
							   priv->async_cancellable,
							   error);
				break;
		}
	}
	if (reply) {
		switch (reply->code) {
			case 227:
				conn = ftp_parse_pasv_reply (protocol_ftp, reply);
				ftp_reply_free (reply);
				priv->data = g_socket_client_connect (g_socket_client_new (),
								      conn,
								      priv->async_cancellable,
								      error);
				break;
		}
	}
	if (G_IS_SOCKET_CONNECTION (priv->data)) {
		uri_decode = soup_uri_decode (priv->uri->path);
		if (uri_decode) {
			msg = g_strdup_printf ("RETR %s", uri_decode);
			reply = ftp_send_and_recv (protocol_ftp,
						   msg,
						   priv->async_cancellable,
						   error);
			g_free (uri_decode);
			g_free (msg);
		}
		else {
			g_set_error (error,
				     SOUP_PROTOCOL_FTP_ERROR,
				     SOUP_FTP_INVALID_PATH,
				     "Path decode failed");
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
		return NULL;
	}

	input_stream = g_io_stream_get_input_stream (G_IO_STREAM (priv->data));
	//g_object_ref (input_stream);
	//g_object_set_data_full (G_OBJECT (input_stream), "socket-connection", data, g_object_unref);

	soup_stream = soup_input_stream_new (input_stream, NULL, NULL);
	g_signal_connect (soup_stream, "end-of-stream",
			  G_CALLBACK (protocol_ftp_retr_complete), protocol_ftp);
	g_signal_connect (soup_stream, "stream-closed",
			  G_CALLBACK (protocol_ftp_retr_complete), protocol_ftp);

	return G_INPUT_STREAM (soup_stream);
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

	g_debug ("soup_protocol_ftp_load_uri_async called");
	g_return_if_fail (SOUP_IS_PROTOCOL_FTP (protocol));

	protocol_ftp = SOUP_PROTOCOL_FTP (protocol);
	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol_ftp);
	// TODO : free private before start new job
	priv->uri = soup_uri_copy (uri);
	priv->async_cancellable = cancellable;
	priv->async_result = g_simple_async_result_new (G_OBJECT (protocol),
							callback,
							user_data,
							soup_protocol_ftp_load_uri_async);
	if (priv->control && g_socket_is_connected (g_socket_connection_get_socket (priv->control))) {
		ftp_send_and_recv_async (protocol_ftp, "PASV", priv->async_cancellable, ftp_callback_pasv);
	}
	else {
		g_socket_client_connect_to_host_async (g_socket_client_new (),
						       priv->uri->host,
						       priv->uri->port,
						       priv->async_cancellable,
						       ftp_callback_conn,
						       protocol);
	}
}

GInputStream *
soup_protocol_ftp_load_uri_finish (SoupProtocol	 *protocol,
				   GAsyncResult	 *result,
				   GError	**error)
{
	SoupProtocolFTP *protocol_ftp;
	SoupProtocolFTPPrivate *priv;
	GInputStream *input_stream;
	SoupInputStream *soup_stream;

	g_debug ("soup_protocol_ftp_load_uri_finish called");

	g_return_val_if_fail (SOUP_IS_PROTOCOL_FTP (protocol), NULL);

	protocol_ftp = SOUP_PROTOCOL_FTP (protocol);
	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol_ftp);

	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return NULL;
	input_stream = g_io_stream_get_input_stream (G_IO_STREAM (priv->data));
	soup_stream = soup_input_stream_new (input_stream, NULL, NULL);
	g_signal_connect (soup_stream, "end-of-stream",
			  G_CALLBACK (protocol_ftp_retr_complete), protocol_ftp);
	g_signal_connect (soup_stream, "stream-closed",
			  G_CALLBACK (protocol_ftp_retr_complete), protocol_ftp);
	g_object_unref (priv->async_result);
	priv->async_result = NULL;

	return G_INPUT_STREAM (soup_stream);
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

/**
 * async callbacks
 **/
void
ftp_callback_conn (GObject *source_object,
		   GAsyncResult *res,
		   gpointer user_data)
{
	SoupProtocolFTP *protocol;
	SoupProtocolFTPPrivate *priv;
	GSocketClient *client;
	GError *error = NULL;

	g_warn_if_fail (G_IS_SOCKET_CLIENT (source_object));
	g_warn_if_fail (G_IS_ASYNC_RESULT (res));
	g_warn_if_fail (user_data != NULL);

	client = G_SOCKET_CLIENT (source_object);
	protocol = SOUP_PROTOCOL_FTP (user_data);
	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	priv->control = g_socket_client_connect_to_host_finish (client,
								   res,
								   &error);
	if (priv->control) {
		priv->control_input = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (priv->control)));
		g_data_input_stream_set_newline_type (priv->control_input, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);
		priv->control_output = g_io_stream_get_output_stream (G_IO_STREAM (priv->control));
		ftp_receive_reply_async (protocol,
					 priv->async_cancellable,
					 ftp_callback_welcome);
	}
	else {
		g_simple_async_result_set_from_error (priv->async_result,
						      error);
		g_simple_async_result_complete (priv->async_result);
		g_error_free (error);
	}
}

void
ftp_callback_welcome (GObject *source_object,
		      GAsyncResult *res,
		      gpointer user_data)
{
	SoupProtocolFTP *protocol;
	SoupProtocolFTPPrivate *priv;
	SoupProtocolFTPReply *reply;
	GError *error = NULL;

	g_warn_if_fail (SOUP_IS_PROTOCOL_FTP (source_object));
	g_warn_if_fail (G_IS_ASYNC_RESULT (res));

	protocol = SOUP_PROTOCOL_FTP (source_object);
	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	reply = ftp_receive_reply_finish (protocol,
					  res,
					  &error);
	if (reply) {
		if (ftp_check_reply (protocol, reply, &error)) {
			if (REPLY_IS_POSITIVE_PRELIMINARY (reply)) {
				g_simple_async_result_set_error (priv->async_result,
								 SOUP_PROTOCOL_FTP_ERROR,
								 SOUP_FTP_SERVICE_UNAVAILABLE,
								 "Service unavailable");
				g_simple_async_result_complete (priv->async_result);
			}
			else
				protocol_ftp_auth_async (protocol, priv->async_cancellable, ftp_callback_pass);
		}
		else {
			g_simple_async_result_set_from_error (priv->async_result,
							      error);
			g_simple_async_result_complete (priv->async_result);
			g_error_free (error);
		}
		ftp_reply_free (reply);
	}
	else {
		g_simple_async_result_set_from_error (priv->async_result,
						      error);
		g_simple_async_result_complete (priv->async_result);
		g_error_free (error);
	}
}

void
ftp_callback_user (GObject *source_object,
		   GAsyncResult *res,
		   gpointer user_data)
{
	SoupProtocolFTP *protocol;
	SoupProtocolFTPPrivate *priv;
	SoupProtocolFTPReply *reply;
	GError *error = NULL;
	gchar *msg;

	g_warn_if_fail (SOUP_IS_PROTOCOL_FTP (source_object));
	g_warn_if_fail (G_IS_ASYNC_RESULT (res));

	protocol = SOUP_PROTOCOL_FTP (source_object);
	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	reply = ftp_send_and_recv_finish (protocol, res, &error);
	if (reply) {
		if (ftp_check_reply (protocol, reply, &error)) {
			if (REPLY_IS_POSITIVE_COMPLETION (reply)) {
				ftp_send_and_recv_async (protocol,
							 "FEAT",
							 priv->async_cancellable,
							 ftp_callback_pass);
			}
			if (REPLY_IS_POSITIVE_INTERMEDIATE (reply)) {
				msg = g_strdup_printf ("PASS %s", priv->uri->password);
				ftp_send_and_recv_async (protocol,
							 msg,
							 priv->async_cancellable,
							 ftp_callback_pass);
				g_free (msg);
			}
		}
		else {
			g_simple_async_result_set_from_error (priv->async_result,
							      error);
			g_simple_async_result_complete (priv->async_result);
			g_error_free (error);
		}
		ftp_reply_free (reply);
	}
	else {
		g_simple_async_result_set_from_error (priv->async_result,
						      error);
		g_simple_async_result_complete (priv->async_result);
		g_error_free (error);
	}
}

void
ftp_callback_pass (GObject *source_object,
		   GAsyncResult *res,
		   gpointer user_data)
{
	SoupProtocolFTP *protocol;
	SoupProtocolFTPPrivate *priv;
	SoupProtocolFTPReply *reply;
	GError *error = NULL;

	g_warn_if_fail (SOUP_IS_PROTOCOL_FTP (source_object));
	g_warn_if_fail (G_IS_ASYNC_RESULT (res));

	protocol = SOUP_PROTOCOL_FTP (source_object);
	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);

	if (protocol_ftp_auth_finish (protocol, res, &error))
		ftp_send_and_recv_async (protocol,
					 "FEAT",
					 priv->async_cancellable,
					 ftp_callback_feat);
}

void
ftp_callback_feat (GObject *source_object,
		   GAsyncResult *res,
		   gpointer user_data)
{
	SoupProtocolFTP *protocol;
	SoupProtocolFTPPrivate *priv;
	SoupProtocolFTPReply *reply;
	GError *error = NULL;

	g_warn_if_fail (SOUP_IS_PROTOCOL_FTP (source_object));
	g_warn_if_fail (G_IS_ASYNC_RESULT (res));

	protocol = SOUP_PROTOCOL_FTP (source_object);
	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	reply = ftp_receive_reply_finish (protocol, res, &error);
	if (reply) {
		if (ftp_check_reply (protocol, reply, &error)) {
			if (REPLY_IS_POSITIVE_COMPLETION (reply)) {
				ftp_parse_feat_reply (protocol, reply);
				ftp_send_and_recv_async (protocol,
							 "PASV",
							 priv->async_cancellable,
							 ftp_callback_pasv);
			}
		}
		else {
			g_simple_async_result_set_from_error (priv->async_result,
							      error);
			g_simple_async_result_complete (priv->async_result);
			g_error_free (error);
		}
		ftp_reply_free (reply);
	}
	else {
		g_simple_async_result_set_from_error (priv->async_result,
						      error);
		g_simple_async_result_complete (priv->async_result);
		g_error_free (error);
	}
}

void
ftp_callback_pasv (GObject *source_object,
		   GAsyncResult *res,
		   gpointer user_data)
{
	SoupProtocolFTP *protocol;
	SoupProtocolFTPPrivate *priv;
	SoupProtocolFTPReply *reply;
	GSocketConnectable *conn;
	GError *error = NULL;

	g_warn_if_fail (SOUP_IS_PROTOCOL_FTP (source_object));
	g_warn_if_fail (G_IS_ASYNC_RESULT (res));

	protocol = SOUP_PROTOCOL_FTP (source_object);
	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	reply = ftp_receive_reply_finish (protocol, res, &error);
	if (reply) {
		if (ftp_check_reply (protocol, reply, &error)) {
			if (REPLY_IS_POSITIVE_COMPLETION (reply)) {
				conn = ftp_parse_pasv_reply (protocol, reply);
				if (conn) {
					g_socket_client_connect_async (g_socket_client_new (),
								       conn,
								       priv->async_cancellable,
								       ftp_callback_data,
								       protocol);
				}
				else {
					g_simple_async_result_set_error (priv->async_result,
									 SOUP_PROTOCOL_FTP_ERROR,
									 0,
									 "Passive failed");
					g_simple_async_result_complete (priv->async_result);
				}
			}
		}
		else {
			g_simple_async_result_set_from_error (priv->async_result,
							      error);
			g_simple_async_result_complete (priv->async_result);
			g_error_free (error);
		}
		ftp_reply_free (reply);
	}
	else {
		g_simple_async_result_set_from_error (priv->async_result,
						      error);
		g_simple_async_result_complete (priv->async_result);
		g_error_free (error);
	}
}

void
ftp_callback_data (GObject *source_object,
		   GAsyncResult *res,
		   gpointer user_data)
{
	GSocketClient *client;
	SoupProtocolFTP *protocol;
	SoupProtocolFTPPrivate *priv;
	GError *error = NULL;
	gchar *uri_decode, *msg;

	g_warn_if_fail (G_IS_SOCKET_CLIENT (source_object));
	g_warn_if_fail (G_IS_ASYNC_RESULT (res));
	g_warn_if_fail (user_data != NULL);

	client = G_SOCKET_CLIENT (source_object);
	protocol = SOUP_PROTOCOL_FTP (user_data);
	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	priv->data = g_socket_client_connect_finish (client,
							res,
							&error);
	if (priv->data) {
		uri_decode = soup_uri_decode (priv->uri->path);
		if (uri_decode) {
			msg = g_strdup_printf ("RETR %s", uri_decode);
			ftp_send_and_recv_async (protocol, msg, priv->async_cancellable, ftp_callback_retr);
			g_free (uri_decode);
			g_free (msg);
		}
		else {
			g_simple_async_result_set_error (priv->async_result,
							 SOUP_PROTOCOL_FTP_ERROR,
							 SOUP_FTP_INVALID_PATH,
							 "Path decode failed");
			g_simple_async_result_complete (priv->async_result);
		}
	}
	else {
		g_simple_async_result_set_from_error (priv->async_result,
						      error);
		g_simple_async_result_complete (priv->async_result);
		g_error_free (error);
	}
}

void
ftp_callback_retr (GObject *source_object,
		   GAsyncResult *res,
		   gpointer user_data)
{
	SoupProtocolFTP *protocol;
	SoupProtocolFTPPrivate *priv;
	SoupProtocolFTPReply *reply;
	GError *error = NULL;

	g_warn_if_fail (SOUP_IS_PROTOCOL_FTP (source_object));
	g_warn_if_fail (G_IS_ASYNC_RESULT (res));

	protocol = SOUP_PROTOCOL_FTP (source_object);
	priv = SOUP_PROTOCOL_FTP_GET_PRIVATE (protocol);
	reply = ftp_receive_reply_finish (protocol, res, &error);
	if (reply) {
		if (ftp_check_reply (protocol, reply, &error)) {
			if (REPLY_IS_POSITIVE_PRELIMINARY (reply)) {
				g_simple_async_result_complete (priv->async_result);
			}
		}
		else {
			g_simple_async_result_set_from_error (priv->async_result,
							      error);
			g_simple_async_result_complete (priv->async_result);
			g_error_free (error);
		}
		ftp_reply_free (reply);
	}
	else {
		g_simple_async_result_set_from_error (priv->async_result,
						      error);
		g_simple_async_result_complete (priv->async_result);
		g_error_free (error);
	}

}
