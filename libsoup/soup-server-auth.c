/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-server-auth.c: Server-side authentication handling
 *
 * Authors:
 *      Alex Graveley (alex@ximian.com)
 *
 * Copyright (C) 2001-2002, Ximian, Inc.
 */

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "soup-server-auth.h"

#include "md5-utils.h"
#include "soup-headers.h"
#include "soup-ntlm.h"

typedef struct {
	const gchar   *scheme;
	SoupAuthType   type;
	gint           strength;
} AuthScheme; 

static AuthScheme known_auth_schemes [] = {
	{ "Basic",  SOUP_AUTH_TYPE_BASIC,  0 },
	{ "NTLM",   SOUP_AUTH_TYPE_NTLM,   2 },
	{ "Digest", SOUP_AUTH_TYPE_DIGEST, 3 },
	{ NULL }
};

static SoupAuthType
soup_auth_get_strongest_header (guint          auth_types,
				const GSList  *vals, 
				gchar        **out_hdr)
{
	gchar *header = NULL;
	AuthScheme *scheme = NULL, *iter;

	g_return_val_if_fail (vals != NULL, 0);

	if (!auth_types) 
		return 0;

	while (vals) {
		for (iter = known_auth_schemes; iter->scheme; iter++) {
			gchar *tryheader = vals->data;

			if ((iter->type & auth_types) &&
			    !g_strncasecmp (tryheader, 
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

	if (!scheme) 
		return 0;

	*out_hdr = header + strlen (scheme->scheme) + 1;
	return scheme->type;
}

static void
digest_hex (guchar *digest, guchar hex[33])
{
	guchar *s, *p;

	/* lowercase hexify that bad-boy... */
	for (s = digest, p = hex; p < hex + 32; s++, p += 2)
		sprintf (p, "%.2x", *s);
}

static gboolean 
check_digest_passwd (SoupServerAuthDigest *digest,
		     gchar                *passwd)
{
	MD5Context ctx;
	guchar d[16];
	guchar hex_a1 [33], hex_a2[33], o[33];
	gchar *tmp;

	/* compute A1 */
	md5_init (&ctx);
	md5_update (&ctx, digest->user, strlen (digest->user));
	md5_update (&ctx, ":", 1);
	md5_update (&ctx, digest->realm, strlen (digest->realm));
	md5_update (&ctx, ":", 1);

	if (passwd)
		md5_update (&ctx, passwd, strlen (passwd));

	if (digest->algorithm == SOUP_ALGORITHM_MD5_SESS) {
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
	digest_hex (d, hex_a1);

	/* compute A2 */
	md5_init (&ctx);
	md5_update (&ctx, 
		    digest->request_method, 
		    strlen (digest->request_method));
	md5_update (&ctx, ":", 1);
	md5_update (&ctx, digest->digest_uri, strlen (digest->digest_uri));

	if (digest->integrity) {
		/* FIXME: Actually implement. Ugh. */
		md5_update (&ctx, ":", 1);
		md5_update (&ctx, "00000000000000000000000000000000", 32);
	}

	/* hexify A2 */
	md5_final (&ctx, d);
	digest_hex (d, hex_a2);

	/* compute KD */
	md5_init (&ctx);
	md5_update (&ctx, hex_a1, 32);
	md5_update (&ctx, ":", 1);
	md5_update (&ctx, digest->nonce, strlen (digest->nonce));
	md5_update (&ctx, ":", 1);

	tmp = g_strdup_printf ("%.8x", digest->nonce_count);
	md5_update (&ctx, tmp, strlen (tmp));
	g_free (tmp);

	md5_update (&ctx, ":", 1);
	md5_update (&ctx, digest->cnonce, strlen (digest->cnonce));
	md5_update (&ctx, ":", 1);

	if (digest->integrity)
		tmp = "auth-int";
	else 
		tmp = "auth";

	md5_update (&ctx, tmp, strlen (tmp));
	md5_update (&ctx, ":", 1);

	md5_update (&ctx, hex_a2, 32);
	md5_final (&ctx, d);

	digest_hex (d, o);

	return strcmp (o, digest->digest_response) == 0;
}

gboolean 
soup_server_auth_check_passwd (SoupServerAuth *auth,
			       gchar          *passwd)
{
	g_return_val_if_fail (auth != NULL, TRUE);

	switch (auth->type) {
	case SOUP_AUTH_TYPE_BASIC:
		if (passwd && auth->basic.passwd)
			return strcmp (auth->basic.passwd, passwd) == 0;
		else
			return passwd == auth->basic.passwd;
	case SOUP_AUTH_TYPE_DIGEST:
		return check_digest_passwd (&auth->digest, passwd);
	case SOUP_AUTH_TYPE_NTLM:
		if (passwd) {
			gchar lm_hash [21], nt_hash [21];

			soup_ntlm_lanmanager_hash (passwd, lm_hash);
			soup_ntlm_nt_hash (passwd, nt_hash);

			if (memcmp (lm_hash, 
				    auth->ntlm.lm_hash, 
				    sizeof (lm_hash)) != 0)
				return FALSE;

			if (memcmp (nt_hash, 
				    auth->ntlm.nt_hash, 
				    sizeof (nt_hash)) != 0)
				return FALSE;

			return TRUE;
		}
		return FALSE;
	}

	return FALSE;
}

const gchar *
soup_server_auth_get_user (SoupServerAuth *auth)
{
	g_return_val_if_fail (auth != NULL, NULL);

	switch (auth->type) {
	case SOUP_AUTH_TYPE_BASIC:
		return auth->basic.user;
	case SOUP_AUTH_TYPE_DIGEST:
		return auth->digest.user;
	case SOUP_AUTH_TYPE_NTLM:
		return auth->ntlm.user;
	}

	return NULL;
}

static gboolean
parse_digest (SoupServerAuthContext *auth_ctx, 
	      gchar                 *header,
	      SoupMessage           *msg,
	      SoupServerAuth        *out_auth)
{
	GHashTable *tokens;
	gchar *user, *realm, *uri, *response;
	gchar *nonce, *cnonce;
	gint nonce_count;
	gboolean integrity;

	user = realm = uri = response = NULL;
	nonce = cnonce = NULL;
	nonce_count = 0;
	integrity = FALSE;

	tokens = soup_header_param_parse_list (header);
	if (!tokens) 
		goto DIGEST_AUTH_FAIL;

	/* Check uri */
	{
		SoupUri *dig_uri;
		const SoupUri *req_uri;

		uri = soup_header_param_copy_token (tokens, "uri");
		if (!uri)
			goto DIGEST_AUTH_FAIL;

		req_uri = soup_context_get_uri (msg->context);

		dig_uri = soup_uri_new (uri);
		if (dig_uri) {
			if (!soup_uri_equal (dig_uri, req_uri)) {
				soup_uri_free (dig_uri);
				goto DIGEST_AUTH_FAIL;
			}
			soup_uri_free (dig_uri);
		} else {	
			char *req_path;

			req_path = soup_uri_to_string (req_uri, TRUE);
			if (strcmp (uri, req_path) != 0) {
				g_free (req_path);
				goto DIGEST_AUTH_FAIL;
			}
			g_free (req_path);
		}
	}

	/* Check qop */
	{
		gchar *qop;
		qop = soup_header_param_copy_token (tokens, "qop");
		if (!qop)
			goto DIGEST_AUTH_FAIL;

		if (!strcmp (qop, "auth-int")) {
			g_free (qop);
			integrity = TRUE;
		} else if (auth_ctx->digest_info.force_integrity) {
			g_free (qop);
			goto DIGEST_AUTH_FAIL;
		}
	}			

	/* Check realm */
	realm = soup_header_param_copy_token (tokens, "realm");
	if (!realm && auth_ctx->digest_info.realm)
		goto DIGEST_AUTH_FAIL;
	else if (realm && 
		 auth_ctx->digest_info.realm &&
		 strcmp (realm, auth_ctx->digest_info.realm) != 0)
		goto DIGEST_AUTH_FAIL;

	/* Check username */
	user = soup_header_param_copy_token (tokens, "username");
	if (!user)
		goto DIGEST_AUTH_FAIL;

	/* Check nonce */
	nonce = soup_header_param_copy_token (tokens, "nonce");
	if (!nonce)
		goto DIGEST_AUTH_FAIL;

	/* Check nonce count */
	{
		gchar *nc;
		nc = soup_header_param_copy_token (tokens, "nc");
		if (!nc)
			goto DIGEST_AUTH_FAIL;

		nonce_count = atoi (nc);
		if (nonce_count <= 0) {
			g_free (nc);
			goto DIGEST_AUTH_FAIL;
		}
		g_free (nc);
	}

	cnonce = soup_header_param_copy_token (tokens, "cnonce");
	if (!cnonce)
		goto DIGEST_AUTH_FAIL;

	response = soup_header_param_copy_token (tokens, "response");
	if (!response)
		goto DIGEST_AUTH_FAIL;

	out_auth->digest.type            = SOUP_AUTH_TYPE_DIGEST;
	out_auth->digest.digest_uri      = uri;
	out_auth->digest.integrity       = integrity;
	out_auth->digest.realm           = realm;
	out_auth->digest.user            = user;
	out_auth->digest.nonce           = nonce;
	out_auth->digest.nonce_count     = nonce_count;
	out_auth->digest.cnonce          = cnonce;
	out_auth->digest.digest_response = response;
	out_auth->digest.request_method  = msg->method;

	soup_header_param_destroy_hash (tokens);

	return TRUE;

 DIGEST_AUTH_FAIL:
	if (tokens)
		soup_header_param_destroy_hash (tokens);

	g_free (user);
	g_free (realm);
	g_free (nonce);
	g_free (response);
	g_free (cnonce);
	g_free (uri);

	return FALSE;
}

SoupServerAuth * 
soup_server_auth_new (SoupServerAuthContext *auth_ctx, 
		      const GSList          *auth_hdrs, 
		      SoupMessage           *msg)
{
	SoupServerAuth *ret;
	SoupAuthType type;
	gchar *header = NULL;

	g_return_val_if_fail (auth_ctx != NULL, NULL);
	g_return_val_if_fail (msg != NULL, NULL);

	if (!auth_hdrs && auth_ctx->types) {
		soup_message_set_error (msg, SOUP_ERROR_UNAUTHORIZED);
		return NULL;
	}

	type = soup_auth_get_strongest_header (auth_ctx->types,
					       auth_hdrs, 
					       &header);

	if (!type && auth_ctx->types) {
		soup_message_set_error (msg, SOUP_ERROR_UNAUTHORIZED);
		return NULL;
	}

	ret = g_new0 (SoupServerAuth, 1);

	switch (type) {
	case SOUP_AUTH_TYPE_BASIC:
		{
			gchar *userpass, *colon;
			gint len;

			userpass = soup_base64_decode (header, &len);
			if (!userpass)
				break;

			colon = strchr (userpass, ':');
			if (!colon) {
				g_free (userpass);
				break;
			}

			ret->basic.type = SOUP_AUTH_TYPE_BASIC;
			ret->basic.user = g_strndup (userpass, 
						     colon - userpass);
			ret->basic.passwd = g_strdup (colon + 1);

			g_free (userpass);

			return ret;
		}
	case SOUP_AUTH_TYPE_DIGEST:
		if (parse_digest (auth_ctx, header, msg, ret))
			return ret;
		break;
	case SOUP_AUTH_TYPE_NTLM:
		g_warning ("NTLM server authentication not yet implemented\n");
		break;
	}

	g_free (ret);

	soup_message_set_error (msg, SOUP_ERROR_UNAUTHORIZED);
	return NULL;
}

void
soup_server_auth_free (SoupServerAuth *auth)
{
	g_return_if_fail (auth != NULL);

	switch (auth->type) {
	case SOUP_AUTH_TYPE_BASIC:
		g_free ((gchar *) auth->basic.user);
		g_free ((gchar *) auth->basic.passwd);
		break;
	case SOUP_AUTH_TYPE_DIGEST:
		g_free ((gchar *) auth->digest.realm);
		g_free ((gchar *) auth->digest.user);
		g_free ((gchar *) auth->digest.nonce);
		g_free ((gchar *) auth->digest.cnonce);
		g_free ((gchar *) auth->digest.digest_uri);
		g_free ((gchar *) auth->digest.digest_response);
		break;
	case SOUP_AUTH_TYPE_NTLM:
		g_free ((gchar *) auth->ntlm.host);
		g_free ((gchar *) auth->ntlm.domain);
		g_free ((gchar *) auth->ntlm.user);
		g_free ((gchar *) auth->ntlm.lm_hash);
		g_free ((gchar *) auth->ntlm.nt_hash);
		break;
	}

	g_free (auth);
}

void
soup_server_auth_context_challenge (SoupServerAuthContext *auth_ctx,
				    SoupMessage           *msg,
				    gchar                 *header_name)
{
	if (auth_ctx->types & SOUP_AUTH_TYPE_BASIC) {
		gchar *hdr;

		hdr = g_strdup_printf ("Basic realm=\"%s\"", 
				       auth_ctx->basic_info.realm);
		soup_message_add_header (msg->response_headers,
					 header_name,
					 hdr);
		g_free (hdr);
	}

	if (auth_ctx->types & SOUP_AUTH_TYPE_DIGEST) {
		GString *str;

		str = g_string_new ("Digest ");

		if (auth_ctx->digest_info.realm)
			g_string_sprintfa (str, 
					   "realm=\"%s\", ", 
					   auth_ctx->digest_info.realm);

		g_string_sprintfa (str, 
				   "nonce=\"%lu%lu\", ", 
				   (unsigned long) msg,
				   (unsigned long) time (0));

		if (auth_ctx->digest_info.force_integrity) 
			g_string_sprintfa (str, "qop=\"auth-int\", ");
		else
			g_string_sprintfa (str, "qop=\"auth,auth-int\", ");

		g_string_sprintfa (str, "algorithm=\"MD5,MD5-sess\"");

		soup_message_add_header (msg->response_headers,
					 header_name,
					 str->str);
		g_string_free (str, TRUE);
	}
}
