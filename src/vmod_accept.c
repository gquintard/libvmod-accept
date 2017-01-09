#include "config.h"

#include <stdio.h>
#include <stdlib.h>

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
find_token(struct vmod_accept_rule *r, VCL_STRING s, ssize_t l, VRT_CTX)
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

	t = find_token(r, s, -1, ctx);

	if (t == NULL) {
		ALLOC_OBJ(t, TOKEN_MAGIC);
		AN(t);
		REPLACE(t->string, s);
		VTAILQ_INSERT_HEAD(&r->tokens, t, list);
	}

	AZ(pthread_rwlock_unlock(&r->mtx));
}

static unsigned
valid_char(char c) {
	if (		(c >= '0' && c <= '9') ||
			(c >= 'A' && c <= 'Z') ||
			(c >= 'a' && c <= 'z') ||
			 c == '-' ||
			 c == '/')
		return (1);
	else
		return (0);
}

VCL_STRING
vmod_rule_filter(VRT_CTX, struct vmod_accept_rule *r, VCL_STRING s)
{
	const char *normalized = NULL, *b, *e = s;
	struct vmod_accept_token *t;

	CHECK_OBJ_NOTNULL(r, RULE_MAGIC);
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	AZ(pthread_rwlock_rdlock(&r->mtx));

	while (s) {
		b = e;
		while (*b != '\0' && !valid_char(*b))
			b++;
		if (*b == '\0')
			break;
		e = b + 1;
		while (valid_char(*e))
			e++;

		t = find_token(r, b, e - b, ctx);
		if (t != NULL) {
			normalized = WS_Copy(ctx->ws, t->string, -1);
			break;
		}

		while (*e != '\0' && *e != ',')
			e++;
	}

	if (normalized == NULL)
		normalized = WS_Copy(ctx->ws, r->fallback, -1);

	AZ(pthread_rwlock_unlock(&r->mtx));

	return (normalized);
}
