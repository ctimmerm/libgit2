#include "common.h"
#include "git/odb.h"
#include "delta-apply.h"

/*
 * This file was heavily cribbed from BinaryDelta.java in JGit, which
 * itself was heavily cribbed from <code>patch-delta.c</code> in the
 * GIT project.   The original delta patching code was written by
 * Nicolas Pitre <nico@cam.org>.
 */

static size_t hdr_sz(
	const unsigned char **delta,
	const unsigned char *end)
{
	const unsigned char *d = *delta;
	size_t r = 0;
	unsigned int c, shift = 0;

	do {
		if (d == end)
			return -1;
		c = *d++;
		r |= (c & 0x7f) << shift;
		shift += 7;
	} while (c & 0x80);
	*delta = d;
	return r;
}

int git__delta_apply(
	git_obj *out,
	const unsigned char *base,
	size_t base_len,
	const unsigned char *delta,
	size_t delta_len)
{
	const unsigned char *delta_end = delta + delta_len;
	size_t res_sz;
	unsigned char *res_dp;

	/* Check that the base size matches the data we were given;
	 * if not we would underflow while accessing data from the
	 * base object, resulting in data corruption or segfault.
	 */
	if (base_len != hdr_sz(&delta, delta_end))
		return GIT_ERROR;

	res_sz = hdr_sz(&delta, delta_end);
	if (!(res_dp = git__malloc(res_sz + 1)))
		return GIT_ERROR;
	res_dp[res_sz] = '\0';
	out->data = res_dp;
	out->len = res_sz;

	while (delta < delta_end) {
		unsigned char cmd = *delta++;
		if (cmd & 0x80) {
			/* cmd is a copy instruction; copy from the base.
			 */
			size_t off = 0, len = 0;

			if (cmd & 0x01) off  = *delta++;
			if (cmd & 0x02) off |= *delta++ <<  8;
			if (cmd & 0x04) off |= *delta++ << 16;
			if (cmd & 0x08) off |= *delta++ << 24;

			if (cmd & 0x10) len  = *delta++;
			if (cmd & 0x20) len |= *delta++ <<  8;
			if (cmd & 0x40) len |= *delta++ << 16;
			if (!len)       len  = 0x10000;

			if (base_len < off + len || res_sz < len)
				goto fail;
			memcpy(res_dp, base + off, len);
			res_dp += len;
			res_sz -= len;

		} else if (cmd) {
			/* cmd is a literal insert instruction; copy from
			 * the delta stream itself.
			 */
			if (delta_end - delta < cmd || res_sz < cmd)
				goto fail;
			memcpy(res_dp, delta, cmd);
			delta  += cmd;
			res_dp += cmd;
			res_sz -= cmd;

		} else {
			/* cmd == 0 is reserved for future encodings.
			 */
			goto fail;
		}
	}

	if (delta != delta_end || res_sz)
		goto fail;
	return GIT_SUCCESS;

fail:
	free(out->data);
	out->data = NULL;
	return GIT_ERROR;
}