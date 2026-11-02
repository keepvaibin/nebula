use std::{
    io::{Read, Seek, SeekFrom},
    sync::{Arc, Mutex},
    time::Instant,
};

use rayon::iter::{IntoParallelIterator, ParallelIterator};
use sha1::{Digest, Sha1};
use zerocopy::FromZeros;

use crate::{
    array_ref, array_ref_mut,
    disc::{
        reader::DiscReader,
        wii::{HASHES_SIZE, SECTOR_DATA_SIZE},
    },
    io::HashBytes,
    util::read::read_box_slice,
    OpenOptions, Result, ResultContext, SECTOR_SIZE,
};

/// In a sector, following the 0x400 byte block of hashes, each 0x400 bytes of decrypted data is
/// hashed, yielding 31 H0 hashes.
/// Then, 8 sectors are aggregated into a subgroup, and the 31 H0 hashes for each sector are hashed,
/// yielding 8 H1 hashes.
/// Then, 8 subgroups are aggregated into a group, and the 8 H1 hashes for each subgroup are hashed,
/// yielding 8 H2 hashes.
/// Finally, the 8 H2 hashes for each group are hashed, yielding 1 H3 hash.
/// The H3 hashes for each group are stored in the partition's H3 table.
#[derive(Clone, Debug)]
pub struct HashTable {
    /// SHA-1 hash of each 0x400 byte block of decrypted data.
    pub h0_hashes: Box<[HashBytes]>,
    /// SHA-1 hash of the 31 H0 hashes for each sector.
    pub h1_hashes: Box<[HashBytes]>,
    /// SHA-1 hash of the 8 H1 hashes for each subgroup.
    pub h2_hashes: Box<[HashBytes]>,
    /// SHA-1 hash of the 8 H2 hashes for each group.
    pub h3_hashes: Box<[HashBytes]>,
}

#[derive(Clone, FromZeros)]
struct HashResult {
    h0_hashes: [HashBytes; 1984],
    h1_hashes: [HashBytes; 64],
    h2_hashes: [HashBytes; 8],
    h3_hash: HashBytes,
}

impl HashTable {
    fn new(num_sectors: u32) -> Self {
        let num_sectors = num_sectors.next_multiple_of(64) as usize;
        let num_data_hashes = num_sectors * 31;
        let num_subgroups = num_sectors / 8;
        let num_groups = num_subgroups / 8;
        Self {
            h0_hashes: <[HashBytes]>::new_box_zeroed_with_elems(num_data_hashes).unwrap(),
            h1_hashes: <[HashBytes]>::new_box_zeroed_with_elems(num_sectors).unwrap(),
            h2_hashes: <[HashBytes]>::new_box_zeroed_with_elems(num_subgroups).unwrap(),
            h3_hashes: <[HashBytes]>::new_box_zeroed_with_elems(num_groups).unwrap(),
        }
    }

    fn extend(&mut self, group_index: usize, result: &HashResult) {
        *array_ref_mut![self.h0_hashes, group_index * 1984, 1984] = result.h0_hashes;
        *array_ref_mut![self.h1_hashes, group_index * 64, 64] = result.h1_hashes;
        *array_ref_mut![self.h2_hashes, group_index * 8, 8] = result.h2_hashes;
        self.h3_hashes[group_index] = result.h3_hash;
    }
}

pub fn rebuild_hashes(reader: &mut DiscReader) -> Result<()> {
    const NUM_H0_HASHES: usize = SECTOR_DATA_SIZE / HASHES_SIZE;

    log::info!(
        "Rebuilding hashes for Wii partition data (using {} threads)",
        rayon::current_num_threads()
    );

    let start = Instant::now();

    // Precompute hashes for zeroed sectors.
    const ZERO_H0_BYTES: &[u8] = &[0u8; HASHES_SIZE];
    let zero_h0_hash = hash_bytes(ZERO_H0_BYTES);

    let partitions = reader.partitions();
    let mut hash_tables = Vec::with_capacity(partitions.len());
    for part in partitions {
        let part_sectors = part.data_end_sector - part.data_start_sector;
        let hash_table = HashTable::new(part_sectors);
        log::debug!(
            "Rebuilding hashes: {} sectors, {} subgroups, {} groups",
            hash_table.h1_hashes.len(),
            hash_table.h2_hashes.len(),
            hash_table.h3_hashes.len()
        );

        let group_count = hash_table.h3_hashes.len();
        let mutex = Arc::new(Mutex::new(hash_table));
        (0..group_count).into_par_iter().try_for_each_with(
            (reader.open_partition(part.index, &OpenOptions::default())?, mutex.clone()),
            |(stream, mutex), h3_index| -> Result<()> {
                let mut result = HashResult::new_box_zeroed()?;
                let mut data_buf = <[u8]>::new_box_zeroed_with_elems(SECTOR_DATA_SIZE)?;
                let mut h3_hasher = Sha1::new();
                for h2_index in 0..8 {
                    let mut h2_hasher = Sha1::new();
                    for h1_index in 0..8 {
                        let sector = h1_index + h2_index * 8;
                        let part_sector = sector as u32 + h3_index as u32 * 64;
                        let mut h1_hasher = Sha1::new();
                        if part_sector >= part_sectors {
                            for h0_index in 0..NUM_H0_HASHES {
                                result.h0_hashes[h0_index + sector * 31] = zero_h0_hash;
                                h1_hasher.update(zero_h0_hash);
                            }
                        } else {
                            stream
                                .seek(SeekFrom::Start(part_sector as u64 * SECTOR_DATA_SIZE as u64))
                                .with_context(|| format!("Seeking to sector {}", part_sector))?;
                            stream
                                .read_exact(&mut data_buf)
                                .with_context(|| format!("Reading sector {}", part_sector))?;
                            for h0_index in 0..NUM_H0_HASHES {
                                let h0_hash = hash_bytes(array_ref![
                                    data_buf,
                                    h0_index * HASHES_SIZE,
                                    HASHES_SIZE
                                ]);
                                result.h0_hashes[h0_index + sector * 31] = h0_hash;
                                h1_hasher.update(h0_hash);
                            }
                        };
                        let h1_hash = h1_hasher.finalize().into();
                        result.h1_hashes[sector] = h1_hash;
                        h2_hasher.update(h1_hash);
                    }
                    let h2_hash = h2_hasher.finalize().into();
                    result.h2_hashes[h2_index] = h2_hash;
                    h3_hasher.update(h2_hash);
                }
                result.h3_hash = h3_hasher.finalize().into();
                let mut hash_table = mutex.lock().map_err(|_| "Failed to lock mutex")?;
                hash_table.extend(h3_index, &result);
                Ok(())
            },
        )?;

        let hash_table = Arc::try_unwrap(mutex)
            .map_err(|_| "Failed to unwrap Arc")?
            .into_inner()
            .map_err(|_| "Failed to lock mutex")?;
        hash_tables.push(hash_table);
    }

    // Verify against H3 table
    for (part, hash_table) in reader.partitions.clone().iter().zip(hash_tables.iter()) {
        log::debug!(
            "Verifying H3 table for partition {} (count {})",
            part.index,
            hash_table.h3_hashes.len()
        );
        reader
            .seek(SeekFrom::Start(
                part.start_sector as u64 * SECTOR_SIZE as u64 + part.header.h3_table_off(),
            ))
            .context("Seeking to H3 table")?;
        let h3_table: Box<[HashBytes]> =
            read_box_slice(reader, hash_table.h3_hashes.len()).context("Reading H3 table")?;
        let mut mismatches = 0;
        for (idx, (expected_hash, h3_hash)) in
            h3_table.iter().zip(hash_table.h3_hashes.iter()).enumerate()
        {
            if expected_hash != h3_hash {
                let mut got_bytes = [0u8; 40];
                let got = base16ct::lower::encode_str(h3_hash, &mut got_bytes).unwrap();
                let mut expected_bytes = [0u8; 40];
                let expected =
                    base16ct::lower::encode_str(expected_hash, &mut expected_bytes).unwrap();
                log::debug!(
                    "Partition {} H3 table does not match:\n\tindex {}\n\texpected: {}\n\tgot:      {}",
                    part.index, idx, expected, got
                );
                mismatches += 1;
            }
        }
        if mismatches > 0 {
            log::warn!("Partition {} H3 table has {} hash mismatches", part.index, mismatches);
        }
    }

    for (part, hash_table) in reader.partitions.iter_mut().zip(hash_tables) {
        part.hash_table = Some(hash_table);
    }
    log::info!("Rebuilt hashes in {:?}", start.elapsed());
    Ok(())
}

#[inline]
pub fn hash_bytes(buf: &[u8]) -> HashBytes {
    let mut hasher = Sha1::new();
    hasher.update(buf);
    hasher.finalize().into()
}
