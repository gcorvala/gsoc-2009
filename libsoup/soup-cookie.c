/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-cookie.c
 *
 * Copyright (C) 2007 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "soup-cookie.h"
#include "soup-date.h"
#include "soup-dns.h"
#include "soup-headers.h"
#include "soup-message.h"
#include "soup-message-headers.h"
#include "soup-uri.h"

/**
 * SECTION:soup-cookie
 * @short_description: HTTP Cookies
 * @see_also: #SoupMessage
 *
 * #SoupCookie implements HTTP cookies, primarily as described by
 * <ulink
 * url="http://wp.netscape.com/newsref/std/cookie_spec.html">the
 * original Netscape cookie specification</ulink>, but with slight
 * modifications based on <ulink
 * url="http://www.ietf.org/rfc/rfc2109.txt">RFC 2109</ulink>, <ulink
 * url="http://msdn2.microsoft.com/en-us/library/ms533046.aspx">Microsoft's
 * HttpOnly extension attribute</ulink>, and observed real-world usage
 * (and, in particular, based on what Firefox does).
 *
 * To have a #SoupSession handle cookies for your appliction
 * automatically, use a #SoupCookieJar.
 **/

/**
 * SoupCookie:
 * @name: the cookie name
 * @value: the cookie value
 * @domain: the "domain" attribute, or else the hostname that the
 * cookie came from.
 * @path: the "path" attribute, or %NULL
 * @expires: the cookie expiration time, or %NULL for a session cookie
 * @secure: %TRUE if the cookie should only be tranferred over SSL
 * @http_only: %TRUE if the cookie should not be exposed to scripts
 *
 * An HTTP cookie.
 *
 * @name and @value will be set for all cookies. If the cookie is
 * generated from a string that appears to have no name, then @name
 * will be the empty string.
 *
 * @domain and @path give the host or domain, and path within that
 * host/domain, to restrict this cookie to. If @domain starts with
 * ".", that indicates a domain (which matches the string after the
 * ".", or any hostname that has @domain as a suffix). Otherwise, it
 * is a hostname and must match exactly.
 *
 * @expires will be non-%NULL if the cookie uses either the original
 * "expires" attribute, or the "max-age" attribute specified in RFC
 * 2109. If @expires is %NULL, it indicates that neither "expires" nor
 * "max-age" was specified, and the cookie expires at the end of the
 * session.
 * 
 * If @http_only is set, the cookie should not be exposed to untrusted
 * code (eg, javascript), so as to minimize the danger posed by
 * cross-site scripting attacks.
 *
 * Since: 2.24
 **/

/* Our Set-Cookie grammar is something like the following, in terms of
 * RFC 2616 BNF:
 *
 * set-cookie             =  "Set-Cookie:" cookies
 * cookies                =  #cookie
 *
 * cookie                 =  [ NAME "=" ] VALUE *(";" [ cookie-av ] )
 * NAME                   =  cookie-attr
 * VALUE                  =  cookie-comma-value
 * cookie-av              =  "Domain" "=" cookie-value
 *                        |  "Expires" "=" cookie-date-value
 *                        |  "HttpOnly"
 *                        |  "Max-Age" "=" cookie-value
 *                        |  "Path" "=" cookie-value
 *                        |  "Secure"
 *                        |  cookie-attr [ "=" cookie-value ]
 *
 * cookie-attr            =  1*<any CHAR except CTLs or ";" or "," or "=">
 *
 * cookie-value           =  cookie-raw-value | cookie-quoted-string
 * cookie-raw-value       =  *<any CHAR except CTLs or ";" or ",">
 *
 * cookie-comma-value     =  cookie-raw-comma-value | cookie-quoted-string
 * cookie-raw-comma-value =  *<any CHAR except CTLs or ";">
 *
 * cookie-date-value      =  cookie-raw-date-value | cookie-quoted-string
 * cookie-raw-date-value  =  [ token "," ] cookie-raw-value
 *
 * cookie-quoted-string   =  quoted-string [ cookie-raw-value ]
 *
 * NAME is optional, as described in
 * https://bugzilla.mozilla.org/show_bug.cgi?id=169091#c16
 *
 * When VALUE is a quoted-string, the quotes (and any internal
 * backslashes) are considered part of the value, and returned
 * literally. When other cookie-values or cookie-comma-values are
 * quoted-strings, the quotes are NOT part of the value. If a
 * cookie-value or cookie-comma-value has trailing junk after the
 * quoted-string, it is discarded.
 *
 * Note that VALUE and "Expires" are allowed to have commas in them,
 * but anywhere else, a comma indicates a new cookie.
 *
 * The literal strings in cookie-av ("Domain", "Expires", etc) are all
 * case-insensitive. Unrecognized cookie attributes are discarded.
 *
 * Cookies are allowed to have excess ";"s, and in particular, can
 * have a trailing ";".
 */

GType
soup_cookie_get_type (void)
{
	static volatile gsize type_volatile = 0;

	if (g_once_init_enter (&type_volatile)) {
		GType type = g_boxed_type_register_static (
			g_intern_static_string ("SoupCookie"),
			(GBoxedCopyFunc) soup_cookie_copy,
			(GBoxedFreeFunc) soup_cookie_free);
		g_once_init_leave (&type_volatile, type);
	}
	return type_volatile;
}

/**
 * soup_cookie_copy:
 * @cookie: a #SoupCookie
 *
 * Copies @cookie.
 *
 * Return value: a copy of @cookie
 *
 * Since: 2.24
 **/
SoupCookie *
soup_cookie_copy (SoupCookie *cookie)
{
	SoupCookie *copy = g_slice_new0 (SoupCookie);

	copy->name = g_strdup (cookie->name);
	copy->value = g_strdup (cookie->value);
	copy->domain = g_strdup (cookie->domain);
	copy->path = g_strdup (cookie->path);
	if (cookie->expires)
		copy->expires = soup_date_copy(cookie->expires);
	copy->secure = cookie->secure;
	copy->http_only = cookie->http_only;

	return copy;
}

static gboolean
domain_matches (const char *domain, const char *host)
{
	char *match;
	int dlen;

	if (!g_ascii_strcasecmp (domain, host))
		return TRUE;
	if (*domain != '.')
		return FALSE;
	if (!g_ascii_strcasecmp (domain + 1, host))
		return TRUE;
	dlen = strlen (domain);
	while ((match = strstr (host, domain))) {
		if (!match[dlen])
			return TRUE;
		host = match + 1;
	}
	return FALSE;
}

static inline const char *
skip_lws (const char *s)
{
	while (g_ascii_isspace (*s))
		s++;
	return s;
}

static inline const char *
unskip_lws (const char *s, const char *start)
{
	while (s > start && g_ascii_isspace (*(s - 1)))
		s--;
	return s;
}

#define is_attr_ender(ch) ((ch) < ' ' || (ch) == ';' || (ch) == ',' || (ch) == '=')
#define is_value_ender(ch, allow_comma) ((ch) < ' ' || (ch) == ';' || (!(allow_comma) && (ch) == ','))

static char *
parse_value (const char **val_p, gboolean keep_quotes, gboolean allow_comma)
{
	const char *start, *end, *p;
	char *value, *q;

	p = *val_p;
	if (*p == '=')
		p++;
	start = skip_lws (p);
	if (*start == '"') {
		for (p = start + 1; *p && *p != '"'; p++) {
			if (*p == '\\' && *(p + 1))
				p++;
		}
		if (keep_quotes)
			value = g_strndup (start, p - start + 1);
		else {
			value = g_malloc (p - (start + 1) + 1);
			for (p = start + 1, q = value; *p && *p != '"'; p++, q++) {
				if (*p == '\\' && *(p + 1))
					p++;
				*q = *p;
			}
			*q = '\0';
		}

		/* Skip anything after the quoted-string */
		while (!is_value_ender (*p, FALSE))
			p++;
	} else {
		for (p = start; !is_value_ender (*p, allow_comma); p++)
			;
		end = unskip_lws (p, start);
		value = g_strndup (start, end - start);
	}

	*val_p = p;
	return value;
}

static SoupDate *
parse_date (const char **val_p)
{
	const char *start, *end, *p;
	char *value;
	SoupDate *date;

	p = *val_p + 1;
	start = skip_lws (p);
	if (*start == '"')
		value = parse_value (&p, FALSE, FALSE);
	else {
		gboolean allow_comma = TRUE;

		for (p = start; !is_value_ender (*p, allow_comma); p++) {
			if (*p == ' ')
				allow_comma = FALSE;
		}
		end = unskip_lws (p, start);
		value = g_strndup (start, end - start);
	}

	date = soup_date_new_from_string (value);
	g_free (value);
	*val_p = p;
	return date;
}

static SoupCookie *
parse_one_cookie (const char **header_p, SoupURI *origin)
{
	const char *header = *header_p, *p;
	const char *start, *end;
	gboolean has_value;
	SoupCookie *cookie;	

	cookie = g_slice_new0 (SoupCookie);

	/* Parse the NAME */
	start = skip_lws (header);
	for (p = start; !is_attr_ender (*p); p++)
		;
	if (*p == '=') {
		end = unskip_lws (p, start);
		cookie->name = g_strndup (start, end - start);
	} else {
		/* No NAME; Set cookie->name to "" and then rewind to
		 * re-parse the string as a VALUE.
		 */
		cookie->name = g_strdup ("");
		p = start;
	}

	/* Parse the VALUE */
	cookie->value = parse_value (&p, TRUE, TRUE);

	/* Parse attributes */
	while (*p == ';') {
		start = skip_lws (p + 1);
		for (p = start; !is_attr_ender (*p); p++)
			;
		end = unskip_lws (p, start);

		has_value = (*p == '=');
#define MATCH_NAME(name) ((end - start == strlen (name)) && !g_ascii_strncasecmp (start, name, end - start))

		if (MATCH_NAME ("domain") && has_value) {
			cookie->domain = parse_value (&p, FALSE, FALSE);
		} else if (MATCH_NAME ("expires") && has_value) {
			cookie->expires = parse_date (&p);
		} else if (MATCH_NAME ("httponly") && !has_value) {
			cookie->http_only = TRUE;
		} else if (MATCH_NAME ("max-age") && has_value) {
			char *max_age = parse_value (&p, FALSE, FALSE);
			soup_cookie_set_max_age (cookie, strtoul (max_age, NULL, 10));
			g_free (max_age);
		} else if (MATCH_NAME ("path") && has_value) {
			cookie->path = parse_value (&p, FALSE, FALSE);
		} else if (MATCH_NAME ("secure") && !has_value) {
			cookie->secure = TRUE;
		} else {
			/* Ignore unknown attributes, but we still have
			 * to skip over the value.
			 */
			if (has_value)
				g_free (parse_value (&p, TRUE, FALSE));
		}
	}

	if (*p == ',') {
		p = skip_lws (p + 1);
		if (*p)
			*header_p = p;
	} else
		*header_p = NULL;

	if (cookie->domain) {
		/* Domain must have at least one '.' (not counting an
		 * initial one. (We check this now, rather than
		 * bailing out sooner, because we don't want to force
		 * any cookies after this one in the Set-Cookie header
		 * to be discarded.)
		 */
		if (!strchr (cookie->domain + 1, '.')) {
			soup_cookie_free (cookie);
			return NULL;
		}

		/* If the domain string isn't an IP addr, and doesn't
		 * start with a '.', prepend one.
		 */
		if (!soup_dns_is_ip_address (cookie->domain) &&
		    cookie->domain[0] != '.') {
			char *tmp = g_strdup_printf (".%s", cookie->domain);
			g_free (cookie->domain);
			cookie->domain = tmp;
		}
	}

	if (origin) {
		/* Sanity-check domain */
		if (cookie->domain) {
			if (!domain_matches (cookie->domain, origin->host)) {
				soup_cookie_free (cookie);
				return NULL;
			}
		} else
			cookie->domain = g_strdup (origin->host);

		/* The original cookie spec didn't say that pages
		 * could only set cookies for paths they were under.
		 * RFC 2109 adds that requirement, but some sites
		 * depend on the old behavior
		 * (https://bugzilla.mozilla.org/show_bug.cgi?id=156725#c20).
		 * So we don't check the path.
		 */

		if (!cookie->path) {
			char *slash;

			cookie->path = g_strdup (origin->path);
			slash = strrchr (cookie->path, '/');
			if (slash)
				*slash = '\0';
		}
	}

	return cookie;
}

/**
 * soup_cookie_new:
 * @name: cookie name
 * @value: cookie value
 * @domain: cookie domain or hostname
 * @path: cookie path, or %NULL
 * @max_age: max age of the cookie, or -1 for a session cookie
 *
 * Creates a new #SoupCookie with the given attributes. (Use
 * soup_cookie_set_secure() and soup_cookie_set_http_only() if you
 * need to set those attributes on the returned cookie.)
 *
 * @max_age is used to set the "expires" attribute on the cookie; pass
 * -1 to not include the attribute (indicating that the cookie expires
 * with the current session), 0 for an already-expired cookie, or a
 * lifetime in seconds. You can use the constants
 * %SOUP_COOKIE_MAX_AGE_ONE_HOUR, %SOUP_COOKIE_MAX_AGE_ONE_DAY,
 * %SOUP_COOKIE_MAX_AGE_ONE_WEEK and %SOUP_COOKIE_MAX_AGE_ONE_YEAR (or
 * multiples thereof) to calculate this value. (If you really care
 * about setting the exact time that the cookie will expire, use
 * soup_cookie_set_expires().)
 *
 * Return value: a new #SoupCookie.
 *
 * Since: 2.24
 **/
SoupCookie *
soup_cookie_new (const char *name, const char *value,
		 const char *domain, const char *path,
		 int max_age)
{
	SoupCookie *cookie;	

	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (value != NULL, NULL);

	/* We ought to return if domain is NULL too, but this used to
	 * do be incorrectly documented as legal, and it wouldn't
	 * break anything as long as you called
	 * soup_cookie_set_domain() immediately after. So we warn but
	 * don't return, to discourage that behavior but not actually
	 * break anyone doing it.
	 */
	g_warn_if_fail (domain != NULL);

	cookie = g_slice_new0 (SoupCookie);
	cookie->name = g_strdup (name);
	cookie->value = g_strdup (value);
	cookie->domain = g_strdup (domain);
	cookie->path = g_strdup (path);
	soup_cookie_set_max_age (cookie, max_age);

	return cookie;
}

/**
 * soup_cookie_parse:
 * @header: a cookie string (eg, the value of a Set-Cookie header)
 * @origin: origin of the cookie, or %NULL
 *
 * Parses @header and returns a #SoupCookie. (If @header contains
 * multiple cookies, only the first one will be parsed.)
 *
 * If @header does not have "path" or "domain" attributes, they will
 * be defaulted from @origin. If @origin is %NULL, path will default
 * to "/", but domain will be left as %NULL. Note that this is not a
 * valid state for a #SoupCookie, and you will need to fill in some
 * appropriate string for the domain if you want to actually make use
 * of the cookie.
 *
 * Return value: a new #SoupCookie, or %NULL if it could not be
 * parsed, or contained an illegal "domain" attribute for a cookie
 * originating from @origin.
 *
 * Since: 2.24
 **/
SoupCookie *
soup_cookie_parse (const char *cookie, SoupURI *origin)
{
	return parse_one_cookie (&cookie, origin);
}

/**
 * soup_cookie_set_name:
 * @cookie: a #SoupCookie
 * @name: the new name
 *
 * Sets @cookie's name to @name
 *
 * Since: 2.24
 **/
void
soup_cookie_set_name (SoupCookie *cookie, const char *name)
{
	g_free (cookie->name);
	cookie->name = g_strdup (name);
}

/**
 * soup_cookie_set_value:
 * @cookie: a #SoupCookie
 * @value: the new value
 *
 * Sets @cookie's value to @value
 *
 * Since: 2.24
 **/
void
soup_cookie_set_value (SoupCookie *cookie, const char *value)
{
	g_free (cookie->value);
	cookie->value = g_strdup (value);
}

/**
 * soup_cookie_set_domain:
 * @cookie: a #SoupCookie
 * @domain: the new domain
 *
 * Sets @cookie's domain to @domain
 *
 * Since: 2.24
 **/
void
soup_cookie_set_domain (SoupCookie *cookie, const char *domain)
{
	g_free (cookie->domain);
	cookie->domain = g_strdup (domain);
}

/**
 * soup_cookie_set_path:
 * @cookie: a #SoupCookie
 * @path: the new path
 *
 * Sets @cookie's path to @path
 *
 * Since: 2.24
 **/
void
soup_cookie_set_path (SoupCookie *cookie, const char *path)
{
	g_free (cookie->path);
	cookie->path = g_strdup (path);
}

/**
 * soup_cookie_set_max_age:
 * @cookie: a #SoupCookie
 * @max_age: the new max age
 *
 * Sets @cookie's max age to @max_age. If @max_age is -1, the cookie
 * is a session cookie, and will expire at the end of the client's
 * session. Otherwise, it is the number of seconds until the cookie
 * expires. You can use the constants %SOUP_COOKIE_MAX_AGE_ONE_HOUR,
 * %SOUP_COOKIE_MAX_AGE_ONE_DAY, %SOUP_COOKIE_MAX_AGE_ONE_WEEK and
 * %SOUP_COOKIE_MAX_AGE_ONE_YEAR (or multiples thereof) to calculate
 * this value. (A value of 0 indicates that the cookie should be
 * considered already-expired.)
 *
 * (This sets the same property as soup_cookie_set_expires().)
 *
 * Since: 2.24
 **/
void
soup_cookie_set_max_age (SoupCookie *cookie, int max_age)
{
	if (cookie->expires)
		soup_date_free (cookie->expires);

	if (max_age == -1)
		cookie->expires = NULL;
	else if (max_age == 0) {
		/* Use a date way in the past, to protect against
		 * clock skew.
		 */
		cookie->expires = soup_date_new (1970, 1, 1, 0, 0, 0);
	} else
		cookie->expires = soup_date_new_from_now (max_age);
}

/**
 * SOUP_COOKIE_MAX_AGE_ONE_HOUR:
 *
 * A constant corresponding to 1 hour, for use with soup_cookie_new()
 * and soup_cookie_set_max_age().
 *
 * Since: 2.24
 **/
/**
 * SOUP_COOKIE_MAX_AGE_ONE_DAY:
 *
 * A constant corresponding to 1 day, for use with soup_cookie_new()
 * and soup_cookie_set_max_age().
 *
 * Since: 2.24
 **/
/**
 * SOUP_COOKIE_MAX_AGE_ONE_WEEK:
 *
 * A constant corresponding to 1 week, for use with soup_cookie_new()
 * and soup_cookie_set_max_age().
 *
 * Since: 2.24
 **/
/**
 * SOUP_COOKIE_MAX_AGE_ONE_YEAR:
 *
 * A constant corresponding to 1 year, for use with soup_cookie_new()
 * and soup_cookie_set_max_age().
 *
 * Since: 2.24
 **/

/**
 * soup_cookie_set_expires:
 * @cookie: a #SoupCookie
 * @expires: the new expiration time, or %NULL
 *
 * Sets @cookie's expiration time to @expires. If @expires is %NULL,
 * @cookie will be a session cookie and will expire at the end of the
 * client's session.
 *
 * (This sets the same property as soup_cookie_set_max_age().)
 *
 * Since: 2.24
 **/
void
soup_cookie_set_expires (SoupCookie *cookie, SoupDate *expires)
{
	if (cookie->expires)
		soup_date_free (cookie->expires);

	if (expires)
		cookie->expires = soup_date_copy (expires);
	else
		cookie->expires = NULL;
}

/**
 * soup_cookie_set_secure:
 * @cookie: a #SoupCookie
 * @secure: the new value for the secure attribute
 *
 * Sets @cookie's secure attribute to @secure. If %TRUE, @cookie will
 * only be transmitted from the client to the server over secure
 * (https) connections.
 *
 * Since: 2.24
 **/
void
soup_cookie_set_secure (SoupCookie *cookie, gboolean secure)
{
	cookie->secure = secure;
}

/**
 * soup_cookie_set_http_only:
 * @cookie: a #SoupCookie
 * @http_only: the new value for the HttpOnly attribute
 *
 * Sets @cookie's HttpOnly attribute to @http_only. If %TRUE, @cookie
 * will be marked as "http only", meaning it should not be exposed to
 * web page scripts or other untrusted code.
 *
 * Since: 2.24
 **/
void
soup_cookie_set_http_only (SoupCookie *cookie, gboolean http_only)
{
	cookie->http_only = http_only;
}

static void
serialize_cookie (SoupCookie *cookie, GString *header, gboolean set_cookie)
{
	if (header->len) {
		if (set_cookie)
			g_string_append (header, ", ");
		else
			g_string_append (header, "; ");
	}

	g_string_append (header, cookie->name);
	g_string_append (header, "=");
	g_string_append (header, cookie->value);
	if (!set_cookie)
		return;

	if (cookie->expires) {
		char *timestamp;

		g_string_append (header, "; expires=");
		timestamp = soup_date_to_string (cookie->expires,
						 SOUP_DATE_COOKIE);
		g_string_append (header, timestamp);
		g_free (timestamp);
	}
	if (cookie->path) {
		g_string_append (header, "; path=");
		g_string_append (header, cookie->path);
	}
	if (cookie->domain) {
		g_string_append (header, "; domain=");
		g_string_append (header, cookie->domain);
	}
	if (cookie->secure)
		g_string_append (header, "; secure");
	if (cookie->secure)
		g_string_append (header, "; HttpOnly");
}

/**
 * soup_cookie_to_set_cookie_header:
 * @cookie: a #SoupCookie
 *
 * Serializes @cookie in the format used by the Set-Cookie header
 * (ie, for sending a cookie from a #SoupServer to a client).
 *
 * Return value: the header
 *
 * Since: 2.24
 **/
char *
soup_cookie_to_set_cookie_header (SoupCookie *cookie)
{
	GString *header = g_string_new (NULL);

	serialize_cookie (cookie, header, TRUE);
	return g_string_free (header, FALSE);
}

/**
 * soup_cookie_to_cookie_header:
 * @cookie: a #SoupCookie
 *
 * Serializes @cookie in the format used by the Cookie header (ie, for
 * returning a cookie from a #SoupSession to a server).
 *
 * Return value: the header
 *
 * Since: 2.24
 **/
char *
soup_cookie_to_cookie_header (SoupCookie *cookie)
{
	GString *header = g_string_new (NULL);

	serialize_cookie (cookie, header, FALSE);
	return g_string_free (header, FALSE);
}

/**
 * soup_cookie_free:
 * @cookie: a #SoupCookie
 *
 * Frees @cookie
 *
 * Since: 2.24
 **/
void
soup_cookie_free (SoupCookie *cookie)
{
	g_return_if_fail (cookie != NULL);

	g_free (cookie->name);
	g_free (cookie->value);
	g_free (cookie->domain);
	g_free (cookie->path);

	if (cookie->expires)
		soup_date_free (cookie->expires);

	g_slice_free (SoupCookie, cookie);
}

/**
 * soup_cookies_from_response:
 * @msg: a #SoupMessage containing a "Set-Cookie" response header
 *
 * Parses @msg's Set-Cookie response headers and returns a #GSList of
 * #SoupCookie<!-- -->s. Cookies that do not specify "path" or
 * "domain" attributes will have their values defaulted from @msg.
 *
 * Return value: a #GSList of #SoupCookie<!-- -->s, which can be freed
 * with soup_cookies_free().
 *
 * Since: 2.24
 **/
GSList *
soup_cookies_from_response (SoupMessage *msg)
{
	SoupURI *origin;
	const char *name, *value;
	SoupCookie *cookie;
	GSList *cookies = NULL;
	SoupMessageHeadersIter iter;

	origin = soup_message_get_uri (msg);

	/* Although parse_one_cookie tries to deal with multiple
	 * comma-separated cookies, it is impossible to do that 100%
	 * reliably, so we try to pass it separate Set-Cookie headers
	 * instead.
	 */
	soup_message_headers_iter_init (&iter, msg->response_headers);
	while (soup_message_headers_iter_next (&iter, &name, &value)) {
		if (g_ascii_strcasecmp (name, "Set-Cookie") != 0)
			continue;

		while (value) {
			cookie = parse_one_cookie (&value, origin);
			if (cookie)
				cookies = g_slist_prepend (cookies, cookie);
		}
	}
	return g_slist_reverse (cookies);
}

/**
 * soup_cookies_from_request:
 * @msg: a #SoupMessage containing a "Cookie" request header
 *
 * Parses @msg's Cookie request header and returns a #GSList of
 * #SoupCookie<!-- -->s. As the "Cookie" header, unlike "Set-Cookie",
 * only contains cookie names and values, none of the other
 * #SoupCookie fields will be filled in. (Thus, you can't generally
 * pass a cookie returned from this method directly to
 * soup_cookies_to_response().)
 *
 * Return value: a #GSList of #SoupCookie<!-- -->s, which can be freed
 * with soup_cookies_free().
 *
 * Since: 2.24
 **/
GSList *
soup_cookies_from_request (SoupMessage *msg)
{
	SoupCookie *cookie;
	GSList *cookies = NULL;
	GHashTable *params;
	GHashTableIter iter;
	gpointer name, value;
	const char *header;

	header = soup_message_headers_get_one (msg->request_headers, "Cookie");
	if (!header)
		return NULL;

	params = soup_header_parse_semi_param_list (header);
	g_hash_table_iter_init (&iter, params);
	while (g_hash_table_iter_next (&iter, &name, &value)) {
		cookie = soup_cookie_new (name, value, NULL, NULL, 0);
		cookies = g_slist_prepend (cookies, cookie);
	}
	soup_header_free_param_list (params);

	return g_slist_reverse (cookies);
}

/**
 * soup_cookies_to_response:
 * @cookies: a #GSList of #SoupCookie
 * @msg: a #SoupMessage
 *
 * Appends a "Set-Cookie" response header to @msg for each cookie in
 * @cookies. (This is in addition to any other "Set-Cookie" headers
 * @msg may already have.)
 *
 * Since: 2.24
 **/
void
soup_cookies_to_response (GSList *cookies, SoupMessage *msg)
{
	GString *header;

	header = g_string_new (NULL);
	while (cookies) {
		serialize_cookie (cookies->data, header, TRUE);
		soup_message_headers_append (msg->response_headers,
					     "Set-Cookie", header->str);
		g_string_truncate (header, 0);
		cookies = cookies->next;
	}
	g_string_free (header, TRUE);
}

/**
 * soup_cookies_to_request:
 * @cookies: a #GSList of #SoupCookie
 * @msg: a #SoupMessage
 *
 * Adds the name and value of each cookie in @cookies to @msg's
 * "Cookie" request. (If @msg already has a "Cookie" request header,
 * these cookies will be appended to the cookies already present. Be
 * careful that you do not append the same cookies twice, eg, when
 * requeuing a message.)
 *
 * Since: 2.24
 **/
void
soup_cookies_to_request (GSList *cookies, SoupMessage *msg)
{
	GString *header;

	header = g_string_new (soup_message_headers_get_one (msg->request_headers,
							     "Cookie"));
	while (cookies) {
		serialize_cookie (cookies->data, header, FALSE);
		cookies = cookies->next;
	}
	soup_message_headers_replace (msg->request_headers,
				      "Cookie", header->str);
	g_string_free (header, TRUE);
}

/**
 * soup_cookies_free:
 * @cookies: a #GSList of #SoupCookie
 *
 * Frees @cookies.
 *
 * Since: 2.24
 **/
void
soup_cookies_free (GSList *cookies)
{
	GSList *c;

	for (c = cookies; c; c = c->next)
		soup_cookie_free (c->data);
	g_slist_free (cookies);
}

/**
 * soup_cookies_to_cookie_header:
 * @cookies: a #GSList of #SoupCookie
 *
 * Serializes a #GSList of #SoupCookie into a string suitable for
 * setting as the value of the "Cookie" header.
 *
 * Return value: the serialization of @cookies
 *
 * Since: 2.24
 **/
char *
soup_cookies_to_cookie_header (GSList *cookies)
{
	GString *str;

	g_return_val_if_fail (cookies != NULL, NULL);

	str = g_string_new (NULL);
	while (cookies) {
		serialize_cookie (cookies->data, str, FALSE);
		cookies = cookies->next;
	}

	return g_string_free (str, FALSE);
}

/**
 * soup_cookie_applies_to_uri:
 * @cookie: a #SoupCookie
 * @uri: a #SoupURI
 *
 * Tests if @cookie should be sent to @uri.
 *
 * (At the moment, this does not check that @cookie's domain matches
 * @uri, because it assumes that the caller has already done that.
 * But don't rely on that; it may change in the future.)
 *
 * Return value: %TRUE if @cookie should be sent to @uri, %FALSE if
 * not
 *
 * Since: 2.24
 **/
gboolean
soup_cookie_applies_to_uri (SoupCookie *cookie, SoupURI *uri)
{
	int plen;

	if (cookie->secure && uri->scheme != SOUP_URI_SCHEME_HTTPS)
		return FALSE;

	if (cookie->expires && soup_date_is_past (cookie->expires))
		return FALSE;

	/* uri->path is required to be non-NULL */
	g_return_val_if_fail (uri->path != NULL, FALSE);

	/* The spec claims "/foo would match /foobar", but fortunately
	 * no one is really that crazy.
	 */
	plen = strlen (cookie->path);
	if (cookie->path[plen - 1] == '/')
		plen--;
	if (strncmp (cookie->path, uri->path, plen) != 0)
		return FALSE;
	if (uri->path[plen] && uri->path[plen] != '/')
		return FALSE;

	return TRUE;
}

gboolean
soup_cookie_equal (SoupCookie *cookie1, SoupCookie *cookie2)
{
	g_return_val_if_fail (cookie1, FALSE);
	g_return_val_if_fail (cookie2, FALSE);

	return (!strcmp (cookie1->name, cookie2->name) &&
		!strcmp (cookie1->value, cookie2->value) &&
		!strcmp (cookie1->path, cookie2->path));
}
