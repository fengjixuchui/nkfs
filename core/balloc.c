#include "trace.h"
#include "balloc.h"
#include "dio.h"
#include "helpers.h"

#include <include/nkfs_obj_info.h>
#include <crt/include/crt.h>

int nkfs_balloc_bm_clear(struct nkfs_sb *sb)
{
	struct dio_cluster *clu;
	u64 i;
	int err;

	for (i = sb->bm_block; i < sb->bm_block + sb->bm_blocks; i++) {
		clu = dio_clu_get(sb->ddev, i);
		if (!clu) {
			err = -EIO;
			goto out;
		}

		err = dio_clu_zero(clu);
		if (err) {
			dio_clu_put(clu);
			goto out;
		}

		err = dio_clu_sync(clu);
		if (err) {
			dio_clu_put(clu);
			goto out;
		}
		dio_clu_put(clu);
	}
	atomic64_set(&sb->used_blocks, 0);

	err = 0;
out:
	return err;
}

static int nkfs_balloc_block_bm_bit(struct nkfs_sb *sb, u64 block,
	u64 *pblock, unsigned long *plong, long *pbit)
{
	u64 long_num;
	u32 bits_per_long = 8*sizeof(unsigned long);

	if (block >= sb->nr_blocks) {
		nkfs_error(-EINVAL, "block %llu out of sb 0x%p blocks %llu",
			   block, sb, sb->nr_blocks);
		return -EINVAL;
	}

	long_num = nkfs_div(block, bits_per_long);

	*pbit = nkfs_mod(block, bits_per_long);
	*pblock = sb->bm_block + nkfs_div(long_num*sizeof(unsigned long),
					sb->bsize);
	*plong = nkfs_mod(long_num*sizeof(unsigned long), sb->bsize);

	return 0;
}

int nkfs_balloc_block_mark(struct nkfs_sb *sb, u64 block, int use)
{
	int err;
	long bit;
	unsigned long long_off;
	u64 bm_block;
	struct dio_cluster *clu;

	err = nkfs_balloc_block_bm_bit(sb, block, &bm_block, &long_off, &bit);
	if (err)
		return err;

	clu = dio_clu_get(sb->ddev, bm_block);
	if (!clu) {
		err = -EIO;
		goto out;
	}

	dio_clu_read_lock(clu);
	if (use) {
		NKFS_BUG_ON(test_bit_le(bit, dio_clu_map(clu, long_off)));
		set_bit_le(bit, dio_clu_map(clu, long_off));
		atomic64_inc(&sb->used_blocks);
		trace_balloc_block_alloc(block);
	} else {
		NKFS_BUG_ON(!test_bit_le(bit, dio_clu_map(clu, long_off)));
		clear_bit_le(bit, dio_clu_map(clu, long_off));
		atomic64_dec(&sb->used_blocks);
		trace_balloc_block_free(block);
	}
	dio_clu_read_unlock(clu);

	dio_clu_set_dirty(clu);
	err = dio_clu_sync(clu);
	if (err) {
		goto cleanup;
	}

	err = 0;

cleanup:
	dio_clu_put(clu);
out:
	up_write(&sb->rw_lock);
	return err;
}

int nkfs_balloc_block_free(struct nkfs_sb *sb, u64 block)
{
	return nkfs_balloc_block_mark(sb, block, 0);
}

static int nkfs_balloc_block_find_set_free_bit(struct nkfs_sb *sb,
	u64 bm_block, struct dio_cluster *clu, unsigned long *plong, long *pbit)
{
	int i, j;
	u64 block;
	long bit;
	int pg_idx;
	int err;
	char *page;
	unsigned long *addr;

	NKFS_BUG_ON((clu->clu_size & (PAGE_SIZE - 1)));

	dio_clu_read_lock(clu);
	i = 0;
	for (pg_idx = 0; pg_idx < (clu->clu_size/PAGE_SIZE); pg_idx++) {
		page = dio_clu_map(clu, i);
		for (j = 0; j < PAGE_SIZE; j += sizeof(unsigned long),
			i += sizeof(unsigned long)) {
			block = 8*(bm_block - sb->bm_block)*sb->bsize + 8*i;
			if (block >= sb->nr_blocks)
				goto nospace;

			addr = (unsigned long *)(page + j);
			if (*addr == (~((unsigned long)0)))
				continue;

			for (bit = 0; bit < (8*sizeof(unsigned long)); bit++) {
				if ((block + bit) >= sb->nr_blocks)
					goto nospace;

				if (!test_bit_le(bit, addr) &&
					!test_and_set_bit_le(bit, addr)) {

					atomic64_inc(&sb->used_blocks);
					dio_clu_set_dirty(clu);
					err = dio_clu_sync(clu);
					if (err) {
						return err;
					}
					*plong = i;
					*pbit = bit;
					return 0;
				}
			}
		}
	}

nospace:
	dio_clu_read_unlock(clu);

	return -ENOENT;
}

int nkfs_balloc_block_alloc(struct nkfs_sb *sb, u64 *pblock)
{
	struct dio_cluster *clu;
	u64 i;
	int err;
	long bit;
	unsigned long long_off;

	*pblock = 0;
	for (i = sb->bm_block; i < sb->bm_block + sb->bm_blocks; i++) {
		clu = dio_clu_get(sb->ddev, i);
		if (!clu) {
			return -EIO;
		}

		err = nkfs_balloc_block_find_set_free_bit(sb, i, clu,
			&long_off, &bit);
		if (!err) {
			u64 block = 8*(i - sb->bm_block)*sb->bsize + 8*long_off
					+ bit;
			trace_balloc_block_alloc(block);
			*pblock = block;
			dio_clu_put(clu);
			return 0;
		}
		dio_clu_put(clu);
	}

	nkfs_error(-ENOSPC, "No space left on sb 0x%p", sb);

	return -ENOSPC;
}
