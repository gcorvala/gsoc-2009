/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-private.h: Asyncronous Callback-based SOAP Request Queue.
 *
 * Authors:
 *      Alex Graveley (alex@helixcode.com)
 *
 * Copyright (C) 2000, Helix Code, Inc.
 */

/* 
 * All the things SOUP users shouldn't need to know about except under
 * extraneous circumstances.
 */

#ifndef SOAP_PRIVATE_H
#define SOAP_PRIVATE_H 1

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef SOUP_WIN32
#  include <malloc.h>
#  define alloca _alloca
#else
#  ifdef HAVE_ALLOCA_H
#    include <alloca.h>
#  else
#    ifdef _AIX
#      pragma alloca
#    else
#      ifndef alloca /* predefined by HP cc +Olibcalls */
         char *alloca ();
#      endif
#    endif
#  endif
#endif

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef SOUP_WIN32
#define VERSION "Win/0.5.99"
#include <windows.h>
#include <winbase.h>
#include <winuser.h>
#endif

#include <libsoup/soup-context.h>
#include <libsoup/soup-message.h>
#include <libsoup/soup-server.h>
#include <libsoup/soup-socket.h>
#include <libsoup/soup-uri.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RESPONSE_BLOCK_SIZE 8192

extern gboolean    soup_initialized;
extern GSList     *soup_active_requests; /* CONTAINS: SoupMessage */
extern GHashTable *soup_hosts;           /* KEY: uri->host, VALUE: SoupHost */

typedef struct {
	gchar      *host;
	GSList     *connections;        /* CONTAINS: SoupConnection */
	GHashTable *contexts;           /* KEY: uri->path, VALUE: SoupContext */
} SoupHost;

struct _SoupAddress {
	gchar*          name;
	struct sockaddr sa;
	gint            ref_count;
};

struct _SoupSocket {
	gint            sockfd;
	SoupAddress    *addr;
	guint           ref_count;
	GIOChannel     *iochannel;
};

typedef struct _SoupAuth SoupAuth;

struct _SoupContext {
	SoupUri      *uri;
	SoupHost     *server;
	SoupAuth     *auth;
	guint         refcnt;
};

struct _SoupConnection {
	SoupHost     *server;
	SoupContext  *context;
	GIOChannel   *channel;
	SoupSocket   *socket;
	guint         port;
	gboolean      in_use;
	guint         last_used_id;
	gboolean      keep_alive;
};

struct _SoupMessagePrivate {
	SoupConnectId   connect_tag;
	guint           read_tag;
	guint           write_tag;
	guint           timeout_tag;

	GString        *req_header;

	SoupCallbackFn  callback;
	gpointer        user_data;

	guint           msg_flags;

	GSList         *content_handlers;

	SoupHttpVersion http_version;

	SoupServer     *server;
	SoupSocket     *server_sock;
};

struct _SoupServer {
	SoupProtocol       proto;
	gint               port;

	GMainLoop         *loop;

	guint              accept_tag;
	SoupSocket        *sock;

	GHashTable        *handlers;
	GSList            *static_handlers;
	SoupServerHandler  default_handler;
};

/* from soup-message.c */

void     soup_message_issue_callback (SoupMessage      *req);

gboolean soup_message_run_handlers   (SoupMessage      *msg,
				      SoupHandlerType   invoke_type);

void     soup_message_cleanup        (SoupMessage      *req);

/* from soup-misc.c */

guint     soup_str_case_hash   (gconstpointer  key);
gboolean  soup_str_case_equal  (gconstpointer  v1,
				gconstpointer  v2);

gint      soup_substring_index (gchar         *str,
				gint           len,
				gchar         *substr);

gchar    *soup_base64_encode   (const gchar   *text,
				gint           len);

/* from soup-socket.c */

gboolean  soup_gethostbyname (const gchar         *hostname,
			      struct sockaddr_in  *sa,
			      gchar              **nicename);

gchar    *soup_gethostbyaddr (const gchar         *addr, 
			      size_t               length, 
			      int                  type);

#ifdef __cplusplus
}
#endif

#endif /*SOUP_PRIVATE_H*/
