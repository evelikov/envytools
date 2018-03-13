/*
 * Copyright (C) 2016 Karol Herbst
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "bios.h"

static void envy_bios_parse_d_dp_info(struct envy_bios *);

struct d_known_tables {
	uint8_t offset;
	uint16_t *ptr;
	const char *name;
};

static int
parse_at(struct envy_bios *bios, unsigned int idx, const char **name)
{
	struct envy_bios_d *d = &bios->d;
	struct d_known_tables tbls[] = {
		{ 0x0, &d->dp_info.offset, "DP INFO" },
	};
	int entries_count = (sizeof(tbls) / sizeof(struct d_known_tables));

	/* check the index */
	if (idx >= entries_count)
		return -ENOENT;

	/* check the table has the right size */
	if (tbls[idx].offset + 2 > d->bit->t_len)
		return -ENOENT;

	if (name)
		*name = tbls[idx].name;

	return bios_u16(bios, d->bit->t_offset + tbls[idx].offset, tbls[idx].ptr);
}

int
envy_bios_parse_bit_d(struct envy_bios *bios, struct envy_bios_bit_entry *bit)
{
	struct envy_bios_d *d = &bios->d;
	unsigned int idx = 0;

	d->bit = bit;

	while (!parse_at(bios, idx, NULL))
		idx++;

	/* parse tables */
	envy_bios_parse_d_dp_info(bios);

	return 0;
}

void
envy_bios_print_bit_d(struct envy_bios *bios, FILE *out, unsigned mask)
{
	struct envy_bios_d *d = &bios->d;
	uint16_t addr;
	int ret = 0, i = 0;

	if (!d->bit || !(mask & ENVY_BIOS_PRINT_d))
		return;

	fprintf(out, "BIT table 'd' at 0x%x, version %i\n",
		d->bit->offset, d->bit->version);

	for (i = 0; i * 2 < d->bit->t_len; ++i) {
		ret = bios_u16(bios, d->bit->t_offset + (i * 2), &addr);
		if (!ret && addr) {
			const char *name;
			ret = parse_at(bios, i, &name);
			fprintf(out, "0x%02x: 0x%x => d %s\n", i * 2, addr, name);
		}
	}

	fprintf(out, "\n");
}

static void
envy_bios_parse_d_dp_info(struct envy_bios *bios)
{
	struct envy_bios_d_dp_info *dp_info = &bios->d.dp_info;
	int err = 0, i, j;

	if (!dp_info->offset)
		return;

	bios_u8(bios, dp_info->offset, &dp_info->version);
	switch (dp_info->version) {
	case 0x40:
	case 0x41:
		err |= bios_u8(bios, dp_info->offset + 0x1, &dp_info->hlen);
		err |= bios_u8(bios, dp_info->offset + 0x2, &dp_info->rlen);
		err |= bios_u8(bios, dp_info->offset + 0x3, &dp_info->entriesnum);
		err |= bios_u8(bios, dp_info->offset + 0x4, &dp_info->target_size);
		err |= bios_u8(bios, dp_info->offset + 0x5, &dp_info->levelentrytables_count);
		err |= bios_u8(bios, dp_info->offset + 0x6, &dp_info->levelentry_size);
		err |= bios_u8(bios, dp_info->offset + 0x7, &dp_info->levelentry_count);
		err |= bios_u8(bios, dp_info->offset + 0x8, &dp_info->flags);
		dp_info->regular_vswing = 0;
		dp_info->low_vswing     = 0;
		dp_info->valid = !err;
		break;
	case 0x42:
		err |= bios_u8(bios, dp_info->offset + 0x1, &dp_info->hlen);
		err |= bios_u8(bios, dp_info->offset + 0x2, &dp_info->rlen);
		err |= bios_u8(bios, dp_info->offset + 0x3, &dp_info->entriesnum);
		err |= bios_u8(bios, dp_info->offset + 0x4, &dp_info->target_size);
		err |= bios_u8(bios, dp_info->offset + 0x5, &dp_info->levelentrytables_count);
		err |= bios_u8(bios, dp_info->offset + 0x6, &dp_info->levelentry_size);
		err |= bios_u8(bios, dp_info->offset + 0x7, &dp_info->levelentry_count);
		err |= bios_u8(bios, dp_info->offset + 0x8, &dp_info->flags);
		err |= bios_u16(bios, dp_info->offset + 0x9, &dp_info->regular_vswing);
		err |= bios_u16(bios, dp_info->offset + 0xb, &dp_info->low_vswing);
		dp_info->valid = !err;
		break;
	default:
		ENVY_BIOS_ERR("Unknown d DP INFO table version 0x%x\n", dp_info->version);
		return;
	}

	dp_info->entries = malloc(dp_info->entriesnum * sizeof(struct envy_bios_d_dp_info_entry));
	for (i = 0; i < dp_info->entriesnum; ++i) {
		struct envy_bios_d_dp_info_entry *e = &dp_info->entries[i];
		e->offset = dp_info->offset + dp_info->hlen +
		            i * dp_info->rlen;
	}

	dp_info->level_entry_tables = malloc(dp_info->levelentrytables_count * sizeof(struct envy_bios_d_dp_info_level_entry_table));
	for (i = 0; i < dp_info->levelentrytables_count; ++i) {
		struct envy_bios_d_dp_info_level_entry_table *let = &dp_info->level_entry_tables[i];
		let->offset = dp_info->offset + dp_info->hlen +
		              dp_info->entriesnum * dp_info->rlen +
		              i * dp_info->levelentry_count * dp_info->levelentry_size;

		let->level_entries = malloc(dp_info->levelentry_count * sizeof(struct envy_bios_d_dp_info_level_entry));
		for (j = 0; j < dp_info->levelentry_count; ++j) {
			struct envy_bios_d_dp_info_level_entry *le = &let->level_entries[j];
			le->offset = let->offset + j * dp_info->levelentry_size;

			switch (dp_info->version) {
				case 0x40:
				case 0x41:
					err |= bios_u8(bios, le->offset + 0x0, &le->post_cursor_2);
					err |= bios_u8(bios, le->offset + 0x1, &le->drive_current);
					err |= bios_u8(bios, le->offset + 0x2, &le->pre_emphasis);
					err |= bios_u8(bios, le->offset + 0x3, &le->tx_pu);
					le->valid = !err;
					break;
				case 0x42:
					le->post_cursor_2 = 0;
					err |= bios_u8(bios, le->offset + 0x0, &le->drive_current);
					err |= bios_u8(bios, le->offset + 0x1, &le->pre_emphasis);
					err |= bios_u8(bios, le->offset + 0x2, &le->tx_pu);
					le->valid = !err;
					break;
				default:
					ENVY_BIOS_ERR("Unknown d DP INFO LEVEL ENTRY TABLE version 0x%x\n", dp_info->version);
					return;
				}
		}
	}
}

void
envy_bios_print_d_dp_info(struct envy_bios *bios, FILE *out, unsigned mask)
{
	struct envy_bios_d_dp_info *dp_info = &bios->d.dp_info;
	int i, j;

	if (!dp_info->offset || !(mask & ENVY_BIOS_PRINT_d))
		return;
	if (!dp_info->valid) {
		ENVY_BIOS_ERR("Failed to parse d DP INFO table at 0x%x, version %x\n\n", dp_info->offset, dp_info->version);
		return;
	}

	fprintf(out, "d DP INFO table at 0x%x, version %x\n", dp_info->offset, dp_info->version);
	fprintf(out, " -- flags 0x%02x\n", dp_info->flags);
	if (dp_info->version == 0x42)
		fprintf(out, " -- regular_vswing 0x%04x, low_vswing 0x%04x\n", dp_info->regular_vswing, dp_info->low_vswing);
	envy_bios_dump_hex(bios, out, dp_info->offset, dp_info->hlen, mask);
	if (mask & ENVY_BIOS_PRINT_VERBOSE) fprintf(out, "\n");

	fprintf(out, " -- DP INFO TABLE entries:\n");
	for (i = 0; i < dp_info->entriesnum; ++i) {
		struct envy_bios_d_dp_info_entry *e = &dp_info->entries[i];
		envy_bios_dump_hex(bios, out, e->offset, dp_info->rlen, mask);
		if (mask & ENVY_BIOS_PRINT_VERBOSE) fprintf(out, "\n");
	}

	fprintf(out, " -- DP INFO LEVEL TABLE entries:\n");
	for (i = 0; i < dp_info->levelentrytables_count; ++i) {
		struct envy_bios_d_dp_info_level_entry_table *let = &dp_info->level_entry_tables[i];

		fprintf(out, "    [%i] DP INFO LEVEL TABLE:\n", i);
		for (j = 0; j < dp_info->levelentry_count; ++j) {
			struct envy_bios_d_dp_info_level_entry *le = &let->level_entries[j];
			if (dp_info->version == 0x42)
			  fprintf(out, "     %02i: DriveCurrent 0x%02x, PreEmphasis 0x%02x, TxPu 0x%02x\n", j,
			    le->drive_current, le->pre_emphasis, le->tx_pu);
			else
			  fprintf(out, "     %02i: PostCursor2 0x%02x, DriveCurrent 0x%02x, PreEmphasis 0x%02x, TxPu 0x%02x\n", j,
			    le->post_cursor_2, le->drive_current, le->pre_emphasis, le->tx_pu);
			envy_bios_dump_hex(bios, out, le->offset, dp_info->levelentry_size, mask);
		}

		if (mask & ENVY_BIOS_PRINT_VERBOSE) fprintf(out, "\n");
	}

	fprintf(out, "\n");
}
