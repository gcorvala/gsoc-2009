/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-auth.c: HTTP Authentication schemes (basic and digest)
 *
 * Authors:
 *      Joe Shaw (joe@ximian.com)
 *      Jeffrey Steadfast (fejj@ximian.com)
 *      Alex Graveley (alex@ximian.com)
 *
 * Copyright (C) 2001-2002, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>

#include <stdio.h>
#include <time.h>

#include "md5-utils.h"
#include "soup-auth.h"
#include "soup-context.h"
#include "soup-headers.h"
#include "soup-message.h"
#include "soup-private.h"
#include "soup-ntlm.h"

/* 
 * Basic Authentication Support 
 */

typedef struct {
	SoupAuth auth;
	gchar *token;
} SoupAuthBasic;

static char *
basic_auth_func (SoupAuth *auth, SoupMessage *message)
{
	SoupAuthBasic *basic = (SoupAuthBasic *) auth;

	return g_strconcat ("Basic ", basic->token, NULL);
}

static void
basic_parse_func (SoupAuth *auth, const char *header)
{
	GHashTable *tokens;

	header += sizeof ("Basic");

	tokens = soup_header_param_parse_list (header);
	if (!tokens) return;

	auth->realm = soup_header_param_copy_token (tokens, "realm");

	soup_header_param_destroy_hash (tokens);
}

static GSList *
basic_pspace_func (SoupAuth *auth, const SoupUri *source_uri)
{
	char *space, *p;

	space = g_strdup (source_uri->path);

	/* Strip query and filename component */
	p = strrchr (space, '/');
	if (p && p != space && p[1])
		*p = '\0';

	return g_slist_prepend (NULL, space);
}

static void
basic_init_func (SoupAuth *auth, const SoupUri *uri)
{
	SoupAuthBasic *basic = (SoupAuthBasic *) auth;
	char *user_pass;

	user_pass = g_strdup_printf ("%s:%s", uri->user, uri->passwd);
	basic->token = soup_base64_encode (user_pass, strlen (user_pass));
	g_free (user_pass);

	auth->authenticated = TRUE;
}

static gboolean
basic_invalidate_func (SoupAuth *auth)
{
	SoupAuthBasic *basic = (SoupAuthBasic *) auth;

	g_free (basic->token);
	basic->token = NULL;
	auth->authenticated = FALSE;

	return TRUE;
}

static void
basic_free (SoupAuth *auth)
{
	SoupAuthBasic *basic = (SoupAuthBasic *) auth;

	g_free (basic->token);
	g_free (basic);
}

static SoupAuth *
soup_auth_new_basic (void)
{
	SoupAuthBasic *basic;
	SoupAuth *auth;

	basic = g_new0 (SoupAuthBasic, 1);
	auth = (SoupAuth *) basic;
	auth->type = SOUP_AUTH_TYPE_BASIC;
	auth->authenticated = FALSE;

	auth->parse_func = basic_parse_func;
	auth->init_func = basic_init_func;
	auth->invalidate_func = basic_invalidate_func;
	auth->pspace_func = basic_pspace_func;
	auth->auth_func = basic_auth_func;
	auth->free_func = basic_free;

	return auth;
}


/* 
 * Digest Authentication Support 
 */

typedef enum {
	QOP_NONE     = 0,
	QOP_AUTH     = 1 << 0,
	QOP_AUTH_INT = 1 << 1
} QOPType;

typedef enum {
	ALGORITHM_MD5      = 1 << 0,
	ALGORITHM_MD5_SESS = 1 << 1
} AlgorithmType;

typedef struct {
	SoupAuth auth;

	gchar  *user;
	guchar  hex_a1 [33];

	/* These are provided by the server */
	char *nonce;
	QOPType qop_options;
	AlgorithmType algorithm;
	char *domain;

	/* These are generated by the client */
	char *cnonce;
	int nc;
	QOPType qop;
} SoupAuthDigest;

static void
digest_hex (guchar *digest, guchar hex[33])
{
	guchar *s, *p;

	/* lowercase hexify that bad-boy... */
	for (s = digest, p = hex; p < hex + 32; s++, p += 2)
		sprintf (p, "%.2x", *s);
}

static char *
compute_response (SoupAuthDigest *digest, SoupMessage *msg)
{
	const SoupUri *uri;
	guchar hex_a2[33], o[33];
	guchar d[16];
	MD5Context ctx;
	char *url;

	uri = soup_context_get_uri (msg->context);
	url = soup_uri_to_string (uri, TRUE);

	/* compute A2 */
	md5_init (&ctx);
	md5_update (&ctx, msg->method, strlen (msg->method));
	md5_update (&ctx, ":", 1);
	md5_update (&ctx, url, strlen (url));
	g_free (url);

	if (digest->qop == QOP_AUTH_INT) {
		/* FIXME: Actually implement. Ugh. */
		md5_update (&ctx, ":", 1);
		md5_update (&ctx, "00000000000000000000000000000000", 32);
	}

	/* now hexify A2 */
	md5_final (&ctx, d);
	digest_hex (d, hex_a2);

	/* compute KD */
	md5_init (&ctx);
	md5_update (&ctx, digest->hex_a1, 32);
	md5_update (&ctx, ":", 1);
	md5_update (&ctx, digest->nonce, strlen (digest->nonce));
	md5_update (&ctx, ":", 1);

	if (digest->qop) {
		char *tmp;

		tmp = g_strdup_printf ("%.8x", digest->nc);

		md5_update (&ctx, tmp, strlen (tmp));
		g_free (tmp);
		md5_update (&ctx, ":", 1);
		md5_update (&ctx, digest->cnonce, strlen (digest->cnonce));
		md5_update (&ctx, ":", 1);

		if (digest->qop == QOP_AUTH)
			tmp = "auth";
		else if (digest->qop == QOP_AUTH_INT)
			tmp = "auth-int";
		else
			g_assert_not_reached ();

		md5_update (&ctx, tmp, strlen (tmp));
		md5_update (&ctx, ":", 1);
	}

	md5_update (&ctx, hex_a2, 32);
	md5_final (&ctx, d);

	digest_hex (d, o);

	return g_strdup (o);
}

static char *
digest_auth_func (SoupAuth *auth, SoupMessage *message)
{
	SoupAuthDigest *digest = (SoupAuthDigest *) auth;
	const SoupUri *uri;
	char *response;
	char *qop = NULL;
	char *nc;
	char *url;
	char *out;

	g_return_val_if_fail (message, NULL);

	response = compute_response (digest, message);

	if (digest->qop == QOP_AUTH)
		qop = "auth";
	else if (digest->qop == QOP_AUTH_INT)
		qop = "auth-int";
	else
		g_assert_not_reached ();

	uri = soup_context_get_uri (message->context);
	url = soup_uri_to_string (uri, TRUE);

	nc = g_strdup_printf ("%.8x", digest->nc);

	out = g_strdup_printf (
		"Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", %s%s%s "
		"%s%s%s %s%s%s uri=\"%s\", response=\"%s\"",
		digest->user,
		auth->realm,
		digest->nonce,

		digest->qop ? "cnonce=\"" : "",
		digest->qop ? digest->cnonce : "",
		digest->qop ? "\"," : "",

		digest->qop ? "nc=" : "",
		digest->qop ? nc : "",
		digest->qop ? "," : "",

		digest->qop ? "qop=" : "",
		digest->qop ? qop : "",
		digest->qop ? "," : "",

		url,
		response);

	g_free (response);
	g_free (url);
	g_free (nc);

	digest->nc++;

	return out;
}

typedef struct {
	char *name;
	guint type;
} DataType;

static DataType qop_types[] = {
	{ "auth",     QOP_AUTH     },
	{ "auth-int", QOP_AUTH_INT }
};

static DataType algorithm_types[] = {
	{ "MD5",      ALGORITHM_MD5      },
	{ "MD5-sess", ALGORITHM_MD5_SESS }
};

static guint
decode_data_type (DataType *dtype, const char *name)
{
        int i;

	if (!name)
		return 0;

        for (i = 0; dtype[i].name; i++) {
                if (!g_strcasecmp (dtype[i].name, name))
			return dtype[i].type;
        }

	return 0;
}

static inline guint
decode_qop (const char *name)
{
	return decode_data_type (qop_types, name);
}

static inline guint
decode_algorithm (const char *name)
{
	return decode_data_type (algorithm_types, name);
}

static void
digest_parse_func (SoupAuth *auth, const char *header)
{
	SoupAuthDigest *digest = (SoupAuthDigest *) auth;
	GHashTable *tokens;
	char *tmp, *ptr;

	header += sizeof ("Digest");

	tokens = soup_header_param_parse_list (header);
	if (!tokens) return;

	auth->realm = soup_header_param_copy_token (tokens, "realm");

	digest->nonce = soup_header_param_copy_token (tokens, "nonce");
	digest->domain = soup_header_param_copy_token (tokens, "domain");

	tmp = soup_header_param_copy_token (tokens, "qop");
	ptr = tmp;

	while (ptr && *ptr) {
		char *token;

		token = soup_header_param_decode_token (&ptr);
		if (token)
			digest->qop_options |= decode_qop (token);
		g_free (token);

		if (*ptr == ',')
			ptr++;
	}

	g_free (tmp);

	tmp = soup_header_param_copy_token (tokens, "algorithm");
	digest->algorithm = decode_algorithm (tmp);
	g_free (tmp);

	soup_header_param_destroy_hash (tokens);
}

static GSList *
digest_pspace_func (SoupAuth *auth, const SoupUri *source_uri)
{
	SoupAuthDigest *digest = (SoupAuthDigest *) auth;
	GSList *space = NULL;
	SoupUri *uri;
	char *domain, *d, *lasts, *dir, *slash;

	if (!digest->domain || !*digest->domain) {
		/* If no domain directive, the protection space is the
		 * whole server.
		 */
		return g_slist_prepend (NULL, g_strdup (""));
	}

	domain = g_strdup (digest->domain);
	for (d = strtok_r (domain, " ", &lasts); d; d = strtok_r (NULL, " ", &lasts)) {
		if (*d == '/')
			dir = g_strdup (d);
		else {
			uri = soup_uri_new (d);
			if (uri && uri->protocol == source_uri->protocol &&
			    uri->port == source_uri->port &&
			    !strcmp (uri->host, source_uri->host))
				dir = g_strdup (uri->path);
			else
				dir = NULL;
			if (uri)
				soup_uri_free (uri);
		}

		if (dir) {
			slash = strrchr (dir, '/');
			if (slash && !slash[1])
				*slash = '\0';

			space = g_slist_prepend (space, dir);
		}
	}
	g_free (domain);

	return space;
}

static void
digest_init_func (SoupAuth *auth, const SoupUri *uri)
{
	SoupAuthDigest *digest = (SoupAuthDigest *) auth;
	MD5Context ctx;
	guchar d[16];

	digest->user = g_strdup (uri->user);

	/* compute A1 */
	md5_init (&ctx);

	md5_update (&ctx, uri->user, strlen (uri->user));

	md5_update (&ctx, ":", 1);
	if (auth->realm)
		md5_update (&ctx, auth->realm, strlen (auth->realm));

	md5_update (&ctx, ":", 1);
	if (uri->passwd)
		md5_update (&ctx, uri->passwd, strlen (uri->passwd));

	if (digest->algorithm == ALGORITHM_MD5_SESS) {
		md5_final (&ctx, d);

		md5_init (&ctx);
		md5_update (&ctx, d, 16);
		md5_update (&ctx, ":", 1);
		md5_update (&ctx, digest->nonce, strlen (digest->nonce));
		md5_update (&ctx, ":", 1);
		md5_update (&ctx, digest->cnonce, strlen (digest->cnonce));
	}

	/* hexify A1 */
	md5_final (&ctx, d);
	digest_hex (d, digest->hex_a1);

	auth->authenticated = TRUE;
}

static gboolean
digest_invalidate_func (SoupAuth *auth)
{
	/* If we failed, we need to get a new nonce from the server
	 * next time, so this can't be reused.
	 */
	return FALSE;
}

static void
digest_free (SoupAuth *auth)
{
	SoupAuthDigest *digest = (SoupAuthDigest *) auth;

	g_free (digest->user);
	g_free (digest->domain);

	g_free (digest->nonce);
	g_free (digest->cnonce);
	g_free (digest);
}

static SoupAuth *
soup_auth_new_digest (void)
{
	SoupAuthDigest *digest;
	SoupAuth *auth;
	char *bgen;

	digest = g_new0 (SoupAuthDigest, 1);

	auth = (SoupAuth *) digest;
	auth->type = SOUP_AUTH_TYPE_DIGEST;
	auth->authenticated = FALSE;

	auth->parse_func = digest_parse_func;
	auth->init_func = digest_init_func;
	auth->invalidate_func = digest_invalidate_func;
	auth->pspace_func = digest_pspace_func;
	auth->auth_func = digest_auth_func;
	auth->free_func = digest_free;

	bgen = g_strdup_printf ("%p:%lu:%lu",
			       auth,
			       (unsigned long) getpid (),
			       (unsigned long) time (0));
	digest->cnonce = soup_base64_encode (bgen, strlen (bgen));
	digest->nc = 1;
	/* We're just going to do qop=auth for now */
	digest->qop = QOP_AUTH;

	g_free (bgen);

	return auth;
}


/*
 * NTLM Authentication Support
 */

typedef struct {
	SoupAuth  auth;
	gchar    *response;
	gchar    *header;
} SoupAuthNTLM;

static gchar *
ntlm_auth (SoupAuth *sa, SoupMessage *msg)
{
	SoupAuthNTLM *auth = (SoupAuthNTLM *) sa;
	char *ret;

	if (!sa->authenticated)
		return soup_ntlm_request ();

	/* Otherwise, return the response; but only once */
	ret = auth->response;
	auth->response = NULL;
	return ret;
}

static inline gchar *
ntlm_get_authmech_token (const SoupUri *uri, gchar *key)
{
	gchar *idx;
	gint len;

	if (!uri->authmech) return NULL;

      	idx = strstr (uri->authmech, key);
	if (idx) {
		idx += strlen (key);

		len = strcspn (idx, ",; ");
		if (len)
			return g_strndup (idx, len);
		else
			return g_strdup (idx);
	}

	return NULL;
}

static void
ntlm_parse (SoupAuth *sa, const char *header)
{
	SoupAuthNTLM *auth = (SoupAuthNTLM *) sa;

	auth->header = g_strdup (header);
	g_strstrip (auth->header);
}

static GSList *
ntlm_pspace (SoupAuth *auth, const SoupUri *source_uri)
{
	/* The protection space is the whole server. */
	return g_slist_prepend (NULL, g_strdup (""));
}

static void
ntlm_init (SoupAuth *sa, const SoupUri *uri)
{
	SoupAuthNTLM *auth = (SoupAuthNTLM *) sa;
	gchar *host, *domain, *nonce;

	if (!auth->header || strlen (auth->header) < sizeof ("NTLM"))
		return;

	if (auth->response)
		g_free (auth->response);

	host   = ntlm_get_authmech_token (uri, "host=");
	domain = ntlm_get_authmech_token (uri, "domain=");

	if (!soup_ntlm_parse_challenge (auth->header, &nonce,
					domain ? NULL : &domain))
		auth->response = NULL;
	else {
		auth->response = soup_ntlm_response (nonce,
						     uri->user,
						     uri->passwd,
						     host,
						     domain);
		g_free (nonce);
	}

	g_free (host);
	g_free (domain);

	g_free (auth->header);
	auth->header = NULL;

	sa->authenticated = TRUE;
}

static gboolean
ntlm_invalidate (SoupAuth *sa)
{
	SoupAuthNTLM *auth = (SoupAuthNTLM *) sa;

	g_free (auth->response);
	auth->response = NULL;
	g_free (auth->header);
	auth->header = NULL;

	sa->authenticated = FALSE;
	return TRUE;
}

static void
ntlm_free (SoupAuth *sa)
{
	SoupAuthNTLM *auth = (SoupAuthNTLM *) sa;

	g_free (auth->response);
	g_free (auth->header);
	g_free (auth);
}

SoupAuth *
soup_auth_new_ntlm (void)
{
	SoupAuthNTLM *auth;

	auth = g_new0 (SoupAuthNTLM, 1);
	auth->auth.type = SOUP_AUTH_TYPE_NTLM;
	auth->auth.authenticated = FALSE;
	auth->auth.realm = g_strdup ("");

	auth->auth.parse_func = ntlm_parse;
	auth->auth.init_func = ntlm_init;
	auth->auth.invalidate_func = ntlm_invalidate;
	auth->auth.pspace_func = ntlm_pspace;
	auth->auth.auth_func = ntlm_auth;
	auth->auth.free_func = ntlm_free;

	return (SoupAuth *) auth;
}


/*
 * Generic Authentication Interface
 */

typedef SoupAuth *(*SoupAuthNewFn) (void);

typedef struct {
	const gchar   *scheme;
	SoupAuthNewFn  ctor;
	gint           strength;
} AuthScheme; 

static AuthScheme known_auth_schemes [] = {
	{ "Basic",  soup_auth_new_basic,  0 },
	{ "NTLM",   soup_auth_new_ntlm,   2 },
	{ "Digest", soup_auth_new_digest, 3 },
	{ NULL }
};

SoupAuth *
soup_auth_new_from_header_list (const SoupUri *uri,
				const GSList  *vals)
{
	gchar *header = NULL;
	AuthScheme *scheme = NULL, *iter;
	SoupAuth *auth = NULL;

	g_return_val_if_fail (vals != NULL, NULL);

	while (vals) {
		gchar *tryheader = vals->data;

		for (iter = known_auth_schemes; iter->scheme; iter++) {
			if (uri->authmech &&
			    g_strncasecmp (uri->authmech,
					   iter->scheme,
					   strlen (iter->scheme)) != 0)
				continue;
			if (!g_strncasecmp (tryheader, 
					    iter->scheme, 
					    strlen (iter->scheme))) {
				if (!scheme || 
				    scheme->strength < iter->strength) {
					header = tryheader;
					scheme = iter;
				}

				break;
			}
		}

		vals = vals->next;
	}

	if (!scheme) return NULL;

	auth = scheme->ctor ();
	if (!auth) return NULL;

	if (!auth->parse_func || 
	    !auth->init_func || 
	    !auth->auth_func || 
	    !auth->free_func)
		g_error ("Faulty Auth Created!!");

	auth->parse_func (auth, header);

	return auth;
}

void
soup_auth_initialize (SoupAuth *auth, const SoupUri *uri)
{
	g_return_if_fail (auth != NULL);
	g_return_if_fail (uri != NULL);

	auth->init_func (auth, uri);
}

gboolean
soup_auth_invalidate (SoupAuth *auth)
{
	g_return_val_if_fail (auth != NULL, FALSE);

	return auth->invalidate_func (auth);
}

gchar *
soup_auth_authorize (SoupAuth *auth, SoupMessage *msg)
{
	g_return_val_if_fail (auth != NULL, NULL);
	g_return_val_if_fail (msg != NULL, NULL);

	return auth->auth_func (auth, msg);
}

void
soup_auth_free (SoupAuth *auth)
{
	g_return_if_fail (auth != NULL);

	g_free (auth->realm);
	auth->free_func (auth);
}

GSList *
soup_auth_get_protection_space (SoupAuth *auth, const SoupUri *source_uri)
{
	g_return_val_if_fail (auth != NULL, NULL);
	g_return_val_if_fail (source_uri != NULL, NULL);

	return auth->pspace_func (auth, source_uri);
}

void
soup_auth_free_protection_space (SoupAuth *auth, GSList *space)
{
	GSList *s;

	for (s = space; s; s = s->next)
		g_free (s->data);
	g_slist_free (space);
}
