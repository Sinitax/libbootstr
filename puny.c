#include "bootstr.h"

#include <unistr.h>
#include <err.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define CHUNKSIZE 4096

bool is_ascii(uint32_t c);

const struct bootstr_cfg puny_cfg = {
	.base = U"abcdefghijklmnopqrstuvwxyz0123456789",
	.baselen = 36,
	.delim = U"-",
	.is_basic = is_ascii,
	.tmin = 1,
	.tmax = 26,
	.skew = 38,
	.damp = 700,
	.initial_bias = 72,
	.initial_n = 128
};

bool
is_ascii(uint32_t c)
{
	return c < 128;
}

uint8_t *
readall(FILE *file, size_t *len)
{
	ssize_t nread;
	size_t cap;
	uint8_t *data;

	*len = 0;
	cap = CHUNKSIZE + 1;
	data = malloc(cap);
	if (!data) err(1, "malloc");

	while (1) {
		if (*len + CHUNKSIZE + 1 > cap) {
			cap *= 2;
			data = realloc(data, cap);
			if (!data) err(1, "realloc");
		}

		nread = fread(data + *len, 1, CHUNKSIZE, file);
		if (nread <= 0) break;

		*len += nread;
	}

	*(data + *len) = '\0';

	return data;
}

int
main(int argc, const char **argv)
{
	const char **arg;
	uint8_t *in, *out;
	uint32_t *u_in, *u_out;
	size_t inlen, outlen;
	size_t u_inlen, u_outlen;
	const char *filepath;
	bool encode;
	char *tok;
	FILE *file;
	int ret;

	encode = true;
	filepath = NULL;
	for (arg = argv + 1; *arg; arg++) {
		if (!strcmp(*arg, "-e")) {
			encode = true;
		} else if (!strcmp(*arg, "-d")) {
			encode = false;
		} else if (!filepath) {
			filepath = *arg;
		} else {
			errx(1, "unknown arg %s", *arg);
		}
	}

	out = NULL;
	if (filepath) {
		file = fopen(filepath, "r");
		if (!file) err(1, "fopen %s", filepath);
		in = readall(file, &inlen);
		fclose(file);
	} else {
		in = readall(stdin, &inlen);
	}
	tok = strchr((char *)in, '\n');
	if (tok) *tok = '\0';

	u_in = u8_to_u32(in, inlen + 1, NULL, &u_inlen);
	u_out = NULL;

	if (encode) {
		ret = bootstr_encode(&puny_cfg, u_in, &u_out);
		if (ret) errx(1, "encode: %s", strerror(ret));
	} else {
		ret = bootstr_decode(&puny_cfg, u_in, &u_out);
		if (ret) errx(1, "decode: %s", strerror(ret));
	}

	out = u32_to_u8(u_out, u32_strlen(u_out) + 1, NULL, &outlen);
	printf("%s\n", (char *)out);

	free(u_out);
	free(u_in);
	free(out);
	free(in);
}
