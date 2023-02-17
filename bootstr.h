#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

struct bootstr_cfg {
	const uint32_t *base;
	ssize_t baselen;
	const uint32_t *delim;
	bool (*is_basic)(uint32_t c);
	ssize_t tmin, tmax;
	ssize_t skew, damp;
	ssize_t initial_bias;
	ssize_t initial_n;
};

int bootstr_encode(const struct bootstr_cfg *cfg, uint32_t *in, uint32_t **out);
int bootstr_decode(const struct bootstr_cfg *cfg, uint32_t *in, uint32_t **out);
