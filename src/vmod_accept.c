#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

/* need vcl.h before vrt.h for vmod_evet_f typedef */
#include "vcl.h"
#include "vrt.h"
#include "cache/cache.h"
#include "vqueue.h"

#include "vtim.h"
#include "vcc_accept_if.h"

struct vmod_accept_token {
	unsigned			magic;
#define TOKEN_MAGIC			0x1ba7712d
	char				*string;
	VTAILQ_ENTRY(vmod_accept_token)	list;
};

struct vmod_accept_rule {
	unsigned				magic;
#define RULE_MAGIC				0x04895617
	char					*fallback;
	VTAILQ_HEAD(, vmod_accept_token)	tokens;
	pthread_rwlock_t			mtx;
};

VCL_VOID __match_proto__()
vmod_rule__init(VRT_CTX,
		struct vmod_accept_rule **rulep, const char *vcl_name,
		VCL_STRING fallback)
{
	struct vmod_accept_rule *r;

	ALLOC_OBJ(r, RULE_MAGIC);
	AN(r);

	VTAILQ_INIT(&r->tokens);
	AZ(pthread_rwlock_init(&r->mtx, NULL));
	if (fallback == NULL)
		r->fallback = strdup("");
	else
		r->fallback = strdup(fallback);
	AN(r->fallback);

	*rulep = r;
}


VCL_VOID
vmod_rule__fini(struct vmod_accept_rule **rulep)
{
	struct vmod_accept_rule *r = *rulep;
	struct vmod_accept_token *t, *token2;

	VTAILQ_FOREACH_SAFE(t, &r->tokens, list, token2)
		free(t->string);

	AZ(pthread_rwlock_destroy(&r->mtx));
	free(r->fallback);
	free(r);

	*rulep = NULL;
}

static struct vmod_accept_token *
match_token(struct vmod_accept_rule *r, VCL_STRING s, ssize_t l)
{
	struct vmod_accept_token *t;
	int match;

	CHECK_OBJ_NOTNULL(r, RULE_MAGIC);
	AN(s);

	VTAILQ_FOREACH(t, &r->tokens, list) {
		AN(t->string);
		if (l == -1)
			match = strcmp(t->string, s) ? 0 : 1;
		else
			match = strncmp(t->string, s, l) ? 0 : 1;

		if (match)
			break;
	}
	return (t);
}

VCL_VOID
vmod_rule_add(VRT_CTX, struct vmod_accept_rule *r, VCL_STRING s)
{
	struct vmod_accept_token *t;

	CHECK_OBJ_NOTNULL(r, RULE_MAGIC);

	if (s == NULL)
		return;

	AZ(pthread_rwlock_wrlock(&r->mtx));

	t = match_token(r, s, -1);

	if (t == NULL) {
		ALLOC_OBJ(t, TOKEN_MAGIC);
		AN(t);
		REPLACE(t->string, s);
		VTAILQ_INSERT_HEAD(&r->tokens, t, list);
	}

	AZ(pthread_rwlock_unlock(&r->mtx));
}

#define SKIPSPACE(p) while (*p && isspace(*p)) {p++;}

/* 0 all good, got a token
 * 1 reached the end of string
 * 2 got a comma
 * 3 got a semi-colon
 * 4 got an equal sign */
static unsigned
next_token(VCL_STRING s, const char **b, const char **e) {
	AN(s);
	while (*s && isspace(*s))
		s++;
	*b = s;
	*e = s + 1;

	switch (*s) {
		case '\0': return (1);
		case ',' : return (2);
		case ';' : return (3);
		case '=' : return (4);
	}

	while (*s != '\0' && *s != ',' && *s != ';' && *s != '=' &&
			!isspace(*s))
		s++;
	*e = s;
	return (0);
}

/* 0 all good, got a token
 * 1 reached the end of string
 * 2 parsing error */
unsigned
parse_accept(VCL_STRING s, const char **endptr, const char **b, const char **e,
		double *q) {
	const char *tb;
	char *tep;
	unsigned r, expectq = 1;
	*q = 1;

	r = next_token(s, b, e);
	if (r >= 2)
		return (2);
	if (r == 1) {
		*endptr = NULL;
		return (1);
	}
	s = *e;
	while (1) {
#define EXPECT_OR_RETURN(n)				\
		do {					\
			r = next_token(s, &tb, endptr);	\
			s = *endptr;			\
			if (r != n)			\
				return (2);		\
			AN(s);				\
		} while (0)

		/* comma and '\0' end the block cleanly, otherwise, we want a
		 * semi-colon */
		r = next_token(s, &tb, endptr);
		s = *endptr;
		if (r == 1 || r == 2)
			return (0);
		if (r != 3)
			return (2);
		AN(s);

		/* expect a string, if it's not 'q', unset expectq */
		EXPECT_OR_RETURN(0);
		if (*endptr - tb != 1 || *tb != 'q')
			expectq = 0;

		/* expect an equal sign */
		EXPECT_OR_RETURN(4);

		/* expect a string */
		EXPECT_OR_RETURN(0);
#undef EXPECT_OR_RETURN

		if (expectq) {
			s = tb;
			/* testing that string starts with 0 or 1 avoid checking
			 * for NAN and INF */
			if ((*s != '0' && *s != '1') ||
					*(s+1) == 'x' ||
					*(s+1) == 'X')
				return (2);
			errno = 0;
			*q = strtod(s, &tep);
			if (errno || *q < 0 || *q > 1)
				return (2);
			s = tep;
			AN(s);
		}
		expectq = 0;
	}

	AN(0);
}

VCL_STRING
vmod_rule_filter(VRT_CTX, struct vmod_accept_rule *rule, VCL_STRING s)
{
	const char *normalized = NULL, *b, *e = s, *endptr;
	struct vmod_accept_token *t, *bt = NULL;
	double q, bq = 0;
	unsigned r;

	CHECK_OBJ_NOTNULL(rule, RULE_MAGIC);
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	AZ(pthread_rwlock_rdlock(&rule->mtx));

	while (s && (r = parse_accept(s, &endptr, &b, &e, &q)) == 0) {
		AN(endptr);
		s = endptr;
		t = match_token(rule, b, e - b);
		if (!t)
			continue;

		if (q > bq) {
			bt = t;
			bq = q;
		}
	}

	if (r == 2 || bt == NULL)
		normalized = WS_Copy(ctx->ws, rule->fallback, -1);
	else
		normalized = WS_Copy(ctx->ws, bt->string, -1);

	AZ(pthread_rwlock_unlock(&rule->mtx));

	return (normalized);
}
