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

	VTAILQ_FOREACH_SAFE(t, &r->tokens, list, token2) {
		VTAILQ_REMOVE(&r->tokens, t, list);
		free(t->string);
		FREE_OBJ(t);
	}

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

enum tok_code {
	TOK_STR,
	TOK_EOS,
	TOK_ERR,
	TOK_COMMA,
	TOK_SEMI,
	TOK_EQ
};

static enum tok_code
next_token(const char **b, const char **e)
{
	const char *s;

	AN(b);
	AN(*b);
	AN(e);

	s = *b;
	while (*s && isspace(*s))
		s++;
	*b = s;
	*e = s + 1;

	switch (*s) {
		case '\0': return (TOK_EOS);
		case ',' : return (TOK_COMMA);
		case ';' : return (TOK_SEMI);
		case '=' : return (TOK_EQ);
	}

	while (*s != '\0' && *s != ',' && *s != ';' && *s != '=' &&
			!isspace(*s))
		s++;
	*e = s;
	return (TOK_STR);
}

#define NEXT()					\
	do {					\
		AN(*nxtok);			\
		start = *nxtok;			\
		tc = next_token(&start, nxtok);	\
	}while (0)
#define EXPECT(n)	if (tc != n) {return (2);}

/* 0 all good, got a token
 * 1 reached the end of string
 * 2 parsing error */
unsigned
parse_accept(const char **b, const char **e, const char **nxtok, double *q)
{
	const char *start = *b;
	unsigned expectq = 1;
	enum tok_code tc;
	char *eod;

	AN(b);
	AN(*b);
	AN(e);
	AN(nxtok);
	AN(q);

	/* grab the next token, if not EOS, must be a string */
	tc = next_token(b, e);
	if (tc == TOK_EOS)
		return (1);
	EXPECT(TOK_STR);
	*nxtok = *e;
	*q = 1;

	/* look for parameters */
	while (1) {
		/* comma and '\0' end the block cleanly, otherwise, we want a
		 * semi-colon */
		NEXT();
		if (tc == TOK_EOS || tc == TOK_COMMA)
			return (0);
		EXPECT(TOK_SEMI);

		NEXT();
		EXPECT(TOK_STR);
		if (*nxtok - start != 1 || *start != 'q')
			expectq = 0;

		NEXT();
		EXPECT(TOK_EQ);

		NEXT();
		EXPECT(TOK_STR);
		if (expectq) {
			/* testing that string starts with 0 or 1 avoids
			 * checking for NAN and INF */
			if ((*start != '0' && *start != '1') ||
					*(start+1) == 'x' ||
					*(start+1) == 'X')
				return (2);
			errno = 0;
			*q = strtod(start, &eod);
			if (errno || *q < 0 || *q > 1)
				return (2);
			*nxtok = eod;
		}
		expectq = 0;
	}
}
#undef NEXT
#undef EXPECT

VCL_STRING
vmod_rule_filter(VRT_CTX, struct vmod_accept_rule *rule, VCL_STRING s)
{
	const char *candidate, *normalized, *b, *e, *nxtok = s;
	struct vmod_accept_token *t;
	double q, maxq = 0;
	unsigned r;

	CHECK_OBJ_NOTNULL(rule, RULE_MAGIC);
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	candidate = rule->fallback;

	AZ(pthread_rwlock_rdlock(&rule->mtx));

	while (s) {
		b = nxtok;
		r = parse_accept(&b, &e, &nxtok, &q);
		if (r == 2)
			candidate = rule->fallback;
		if (r != 0)
			break;

		t = match_token(rule, b, e - b);
		if (t && q > maxq) {
			maxq = q;
			candidate = t->string;
		}
	}

	normalized = WS_Copy(ctx->ws, candidate, -1);
	AN(normalized);

	AZ(pthread_rwlock_unlock(&rule->mtx));

	return (normalized);
}
