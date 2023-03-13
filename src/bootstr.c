#include "bootstr.h"

#include <limits.h>
#include <stdint.h>
#include <unistr.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

static int bootstr_realloc(uint32_t **alloc, size_t reserve, size_t *cap);
static int append_codes(uint32_t **alloc, size_t *len, size_t *cap,
	const uint32_t *src, size_t srclen);
static int check_config(const struct bootstr_cfg *cfg);

static inline size_t
bootstr_adapt(const struct bootstr_cfg *cfg, ssize_t delta,
	ssize_t len, bool first)
{
	size_t k;

	delta = first ? delta / cfg->damp : delta / 2;
	delta += delta / len;

	k = 0;
	while (delta > (cfg->baselen - cfg->tmin) * cfg->tmax / 2) {
		delta /= cfg->baselen - cfg->tmin;
		k += cfg->baselen;
	}
	k += (cfg->baselen - cfg->tmin + 1) * delta / (delta + cfg->skew);

	return k;
}

int
bootstr_realloc(uint32_t **alloc, size_t reserve, size_t *cap)
{
	if (reserve >= *cap) {
		if (!*cap) {
			*cap = reserve;
		} else {
			*cap = MAX(*cap * 2, reserve);
		}
		*alloc = realloc(*alloc, *cap * sizeof(uint32_t));
		if (!*alloc) return -errno;
	}

	return 0;
}

int
append_codes(uint32_t **alloc, size_t *len, size_t *cap,
	const uint32_t *src, size_t srclen)
{
	int ret;

	ret = bootstr_realloc(alloc, *len + srclen, cap);
	if (ret) return ret;

	memcpy(*alloc + *len, src, srclen * sizeof(uint32_t));
	*len += srclen;

	return 0;
}

int
check_config(const struct bootstr_cfg *cfg)
{
	if (cfg->tmin >= cfg->baselen || cfg->tmin <= 0)
		return -EINVAL;

	if (cfg->tmax < cfg->tmin)
		return -EINVAL;

	if (!cfg->delim)
		return -EINVAL;

	if (!cfg->base || cfg->baselen <= 0)
		return -EINVAL;

	if (!cfg->damp)
		return -EINVAL;

	return 0;
}

int
bootstr_encode_delta(const struct bootstr_cfg *cfg, uint32_t *in, uint32_t **out,
	size_t *outlen, size_t *outcap, ssize_t bias, ssize_t delta)
{
	ssize_t thresh;
	ssize_t val;
	ssize_t off;
	ssize_t ci;
	int ret;

	val = delta;

	off = cfg->baselen;
	while (1) {
		/* final digit must be under threshold */
		thresh = MIN(cfg->tmax, MAX(cfg->tmin, off - bias));
		if (val < thresh) break;

		/* no room for encoding, invalid params */
		if (thresh >= cfg->baselen)
			return -EINVAL;

		/* encode char according to current base */
		ci = thresh + (val - thresh) % (cfg->baselen - thresh);
		val = (val - thresh) / (cfg->baselen - thresh);
		if (ci >= cfg->baselen)
			return -EINVAL;

		ret = append_codes(out, outlen, outcap, &cfg->base[ci], 1);
		if (ret) return ret;

		off += cfg->baselen;
	}

	ret = append_codes(out, outlen, outcap, &cfg->base[val], 1);
	if (ret) return ret;

	return 0;
}

int
bootstr_encode(const struct bootstr_cfg *cfg, uint32_t *in, uint32_t **out)
{
	size_t outlen, outcap;
	size_t inlen;
	ssize_t processed, basiclen;
	ssize_t next_code, n;
	ssize_t delta, bias;
	ssize_t i;
	int ret;

	ret = check_config(cfg);
	if (ret) return ret;

	outlen = 0;
	outcap = 0;

	/* parse out safe character prefix */
	inlen = u32_strlen(in);
	for (i = 0; i < inlen; i++) {
		if (cfg->is_basic(in[i]))
			append_codes(out, &outlen, &outcap, &in[i], 1);
	}
	processed = outlen;
	basiclen = outlen;

	/* if basic prefix avail, add delim */
	if (outlen) {
		ret = append_codes(out, &outlen, &outcap,
			cfg->delim, u32_strlen(cfg->delim));
		if (ret) return ret;
	}

	bias = cfg->initial_bias;
	n = cfg->initial_n;
	delta = 0;

	/* encode rest of non-basic chars */
	while (processed < inlen) {
		next_code = SSIZE_MAX;
		for (i = 0; i < inlen; i++) {
			if (in[i] >= n && in[i] < next_code)
				next_code = in[i];
		}

		/* calc insertions to skip until start of last round:
		 * (processed + 1) insertions possible per round
		 * (next_code - n) rounds todo */
		if ((next_code - n) > (SSIZE_MAX - delta) / (processed + 1))
			return -EOVERFLOW;
		delta += (next_code - n) * (processed + 1);

		/* calculate number of skip to reach code in output at n */
		n = next_code;
		for (i = 0; i < inlen; i++) {
			/* only consider characters already in output */
			if (in[i] < n || cfg->is_basic(in[i])) {
				delta += 1;
				if (delta <= 0)
					return -EOVERFLOW;
			}

			/* reached the position of ONE of next_code */
			if (in[i] == n) {
				ret = bootstr_encode_delta(cfg, in, out,
					&outlen, &outcap, bias, delta);
				if (ret) return ret;
				bias = bootstr_adapt(cfg, delta,
					processed + 1, processed == basiclen);
				delta = 0;
				processed += 1;
			}
		}

		delta += 1;
		n += 1;
	}

	ret = append_codes(out, &outlen, &outcap, U"\x00", 1);
	if (ret) return ret;

	return 0;
}

int
bootstr_decode_delta(const struct bootstr_cfg *cfg, uint32_t *in,
	ssize_t *processed, ssize_t bias, ssize_t state, ssize_t *state_new)
{
	ssize_t thresh;
	ssize_t digit;
	ssize_t mul;
	ssize_t off;
	uint32_t *tok;

	/* construct integer from digits while accounting
	 * for possibly different bases per digit */

	mul = 1;
	off = cfg->baselen;
	while (1) {
		if (!in[*processed]) return -EINVAL;

		tok = u32_strchr(cfg->base, in[*processed]);
		if (!tok) return -EINVAL;
		*processed += 1;

		digit = tok - cfg->base;
		if (digit > (SSIZE_MAX - state) / mul)
			return -EOVERFLOW;
		state += digit * mul;

		thresh = MIN(cfg->tmax, MAX(cfg->tmin, off - bias));
		if (digit < thresh) break;

		if (mul > SSIZE_MAX / (cfg->baselen - thresh))
			return -EOVERFLOW;
		mul *= cfg->baselen - thresh;

		off += cfg->baselen;
	}
	*state_new = state;

	return 0;
}

int
bootstr_decode(const struct bootstr_cfg *cfg, uint32_t *in, uint32_t **out)
{
	size_t outlen, outcap;
	size_t inlen;
	ssize_t basiclen;
	ssize_t processed, n;
	ssize_t state, state_new, bias;
	ssize_t i, len;
	int rc;

	rc = check_config(cfg);
	if (rc) return rc;

	outlen = 0;
	outcap = 0;

	basiclen = 0;
	inlen = u32_strlen(in);

	/* find basic prefix delim */
	for (i = 0; i < inlen; i++) {
		if (!u32_strcmp(in + i, cfg->delim)) {
			basiclen = i;
			break;
		}
		if (!cfg->is_basic(in[i]))
			return -EINVAL;
	}

	/* copy basic prefix to output */
	if (basiclen)
		append_codes(out, &outlen, &outcap, in, basiclen);

	n = cfg->initial_n;
	bias = cfg->initial_bias;
	state = 0;

	/* decode rest of non-basic chars */
	for (processed = basiclen; processed < inlen; ) {
		/* decode delta and add to state */
		rc = bootstr_decode_delta(cfg, in, &processed,
			bias, state, &state_new);
		if (rc) return rc;

		/* use delta to calculate new bias */
		bias = bootstr_adapt(cfg, state_new - state,
			outlen + 1, state == 0);
		state = state_new;

		/* split up state into rounds and index */
		if (state / (outlen + 1) > (SSIZE_MAX - n))
			return -EOVERFLOW;
		n += state / (outlen + 1);
		state %= outlen + 1;

		/* insert current code */
		rc = bootstr_realloc(out, outlen + 1, &outcap);
		if (rc) return rc;
		memmove(*out + state + 1, *out + state,
			(outlen - state) * sizeof(uint32_t));
		(*out)[state] = n;
		state += 1;
		outlen += 1;
	}

	rc = append_codes(out, &outlen, &outcap, U"\x00", 1);
	if (rc) return rc;

	return 0;
}
