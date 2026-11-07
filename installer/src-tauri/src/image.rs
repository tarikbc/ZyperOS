// A card image: read as a stream, unpacked on the way if it is .img.xz, so a
// 350 MB image never lands on disk unpacked; and its size once unpacked, for
// the progress bar and the update's checks.
//
// The size of an .xz comes from its index, at the end of the file: the
// unpacked size of every block, written down when it was made. Reading that
// takes a few kilobytes where counting the bytes would take a pass over the
// whole image. An .xz the index can't be read from (several streams joined,
// say) is counted after all.

use std::fs::File;
use std::io::{self, BufReader, Read, Seek, SeekFrom};
use std::path::Path;

fn is_xz(path: &Path) -> bool {
    path.extension().is_some_and(|e| e.eq_ignore_ascii_case("xz"))
}

/// The image from its first byte, unpacked.
pub fn open(path: &Path) -> io::Result<Box<dyn Read + Send>> {
    let file = BufReader::with_capacity(1 << 20, File::open(path)?);
    Ok(if is_xz(path) {
        Box::new(lzma_rust2::XzReader::new(file, true))
    } else {
        Box::new(file)
    })
}

/// The image's size unpacked, in bytes.
pub fn size(path: &Path) -> io::Result<u64> {
    if !is_xz(path) {
        return Ok(std::fs::metadata(path)?.len());
    }
    match xz_index_size(path) {
        Some(n) => Ok(n),
        None => io::copy(&mut open(path)?, &mut io::sink()),
    }
}

/// A FAT boot sector's volume label, FAT32's or FAT12/16's: what mkdosfs -n
/// wrote, as genimage makes the boot partitions.
fn fat_label(sector: &[u8]) -> Option<String> {
    if sector.len() < 512 || sector[510..512] != [0x55, 0xAA] {
        return None;
    }
    // The extended boot signature 0x29 says a serial and label follow: at
    // 0x42 on FAT32 (label 0x47..0x52), at 0x26 on FAT12/16 (0x2B..0x36)
    let label = if sector[0x42] == 0x29 && &sector[0x52..0x5A] == b"FAT32   " {
        &sector[0x47..0x52]
    } else if sector[0x26] == 0x29 {
        &sector[0x2B..0x36]
    } else {
        return None;
    };
    Some(String::from_utf8_lossy(label).trim_end().to_string())
}

/// Which device a card image is for, from the label of its boot partition:
/// PX2BOOT the Pixel 2's, BRICKBOOT the Brick's. None for an image with
/// neither, which is not ZyperOS's. Reads the image only as far as that
/// partition's first sector, a few MB in.
pub fn device_of(path: &Path) -> io::Result<Option<crate::cards::Device>> {
    use crate::writer::{table_of, HEAD, SECTOR};
    let mut image = open(path)?;
    let mut head = vec![0u8; HEAD];
    image.read_exact(&mut head)?;
    let Some(table) = table_of(&head) else { return Ok(None) };
    let mut starts: Vec<u64> = table.iter().flatten().map(|p| p.start * SECTOR).collect();
    starts.sort_unstable();
    let mut at = HEAD as u64;
    let mut sector = [0u8; 512];
    for start in starts {
        // Past the first 512 MB is not where a boot partition is
        if start < at || start > 512 << 20 {
            continue;
        }
        io::copy(&mut (&mut image).take(start - at), &mut io::sink())?;
        if image.read_exact(&mut sector).is_err() {
            return Ok(None);
        }
        at = start + 512;
        if let Some(device) = fat_label(&sector).as_deref().and_then(crate::cards::system_of) {
            return Ok(Some(device));
        }
    }
    Ok(None)
}

/// An xz number: seven bits a byte, low first, the top bit saying more follow.
fn varint(buf: &[u8], at: &mut usize) -> Option<u64> {
    let mut n = 0u64;
    for shift in (0..63).step_by(7) {
        let b = *buf.get(*at)?;
        *at += 1;
        n |= u64::from(b & 0x7f) << shift;
        if b & 0x80 == 0 {
            return Some(n);
        }
    }
    None
}

/// The unpacked size from a single-stream .xz's index, or None to count.
fn xz_index_size(path: &Path) -> Option<u64> {
    let mut f = File::open(path).ok()?;
    let len = f.metadata().ok()?.len();
    if len < 32 {
        return None;
    }
    // The stream footer, the last 12 bytes: CRC32, backward size, flags, "YZ"
    let mut footer = [0u8; 12];
    f.seek(SeekFrom::Start(len - 12)).ok()?;
    f.read_exact(&mut footer).ok()?;
    if &footer[10..12] != b"YZ" {
        return None;
    }
    let index_len = (u64::from(u32::from_le_bytes(footer[4..8].try_into().ok()?)) + 1) * 4;
    if index_len + 12 + 12 > len {
        return None;
    }
    let mut index = vec![0u8; index_len as usize];
    f.seek(SeekFrom::Start(len - 12 - index_len)).ok()?;
    f.read_exact(&mut index).ok()?;
    // The index: an indicator of 0, the number of records, then each block's
    // packed and unpacked size
    if index[0] != 0 {
        return None;
    }
    let mut at = 1;
    let records = varint(&index, &mut at)?;
    let mut packed = 0u64;
    let mut unpacked = 0u64;
    for _ in 0..records {
        packed += varint(&index, &mut at)?.div_ceil(4) * 4;
        unpacked += varint(&index, &mut at)?;
    }
    // One stream only: its header, the blocks and the index fill the file.
    // Anything more (streams joined, padding) is counted instead.
    (12 + packed + index_len + 12 == len).then_some(unpacked)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    fn sample(n: usize) -> Vec<u8> {
        (0..n).map(|i| (i * 31 % 251) as u8).collect()
    }

    fn xz(data: &[u8]) -> Vec<u8> {
        let mut w = lzma_rust2::XzWriter::new(Vec::new(), lzma_rust2::XzOptions::with_preset(1)).unwrap();
        w.write_all(data).unwrap();
        w.finish().unwrap()
    }

    #[test]
    fn reads_an_xz_image_and_its_size_from_the_index() {
        let dir = std::env::temp_dir().join("zyperos-installer-image");
        std::fs::create_dir_all(&dir).unwrap();
        let data = sample(3 << 20);
        let p = dir.join("t.img.xz");
        std::fs::write(&p, xz(&data)).unwrap();
        assert_eq!(xz_index_size(&p), Some(data.len() as u64), "from the index, not a count");
        assert_eq!(size(&p).unwrap(), data.len() as u64);
        let mut back = Vec::new();
        open(&p).unwrap().read_to_end(&mut back).unwrap();
        assert!(back == data);
    }

    #[test]
    fn two_streams_joined_are_counted() {
        let dir = std::env::temp_dir().join("zyperos-installer-image");
        std::fs::create_dir_all(&dir).unwrap();
        let (a, b) = (sample(100_000), sample(50_000));
        let mut joined = xz(&a);
        joined.extend(xz(&b));
        let p = dir.join("two.img.xz");
        std::fs::write(&p, joined).unwrap();
        assert_eq!(xz_index_size(&p), None);
        assert_eq!(size(&p).unwrap(), 150_000);
    }

    /// A real image's size from its index, against a full count: IMAGE=path
    /// cargo test real_image -- --ignored --nocapture
    #[test]
    #[ignore]
    fn real_image() {
        let p = std::path::PathBuf::from(std::env::var("IMAGE").expect("IMAGE"));
        let t = std::time::Instant::now();
        let from_index = xz_index_size(&p);
        let index_ms = t.elapsed().as_millis();
        let t = std::time::Instant::now();
        let counted = io::copy(&mut open(&p).unwrap(), &mut io::sink()).unwrap();
        println!("index {from_index:?} in {index_ms} ms, counted {counted} in {} ms", t.elapsed().as_millis());
        assert_eq!(from_index, Some(counted));
    }

    /// A boot sector as mkdosfs makes one, FAT16 or FAT32, with a label
    fn boot_sector(fat32: bool, label: &str) -> [u8; 512] {
        let mut s = [0u8; 512];
        let mut name = [b' '; 11];
        name[..label.len()].copy_from_slice(label.as_bytes());
        if fat32 {
            s[0x42] = 0x29;
            s[0x47..0x52].copy_from_slice(&name);
            s[0x52..0x5A].copy_from_slice(b"FAT32   ");
        } else {
            s[0x26] = 0x29;
            s[0x2B..0x36].copy_from_slice(&name);
            s[0x36..0x3E].copy_from_slice(b"FAT16   ");
        }
        s[510] = 0x55;
        s[511] = 0xAA;
        s
    }

    /// An MBR image with one FAT partition at 1 MiB labelled `label`
    fn mbr_image(label: &str, fat32: bool) -> Vec<u8> {
        let mut img = vec![0u8; 2 << 20];
        let e = 446;
        img[e + 4] = 0x0C;
        img[e + 8..e + 12].copy_from_slice(&2048u32.to_le_bytes());
        img[e + 12..e + 16].copy_from_slice(&2048u32.to_le_bytes());
        img[510] = 0x55;
        img[511] = 0xAA;
        img[1 << 20..(1 << 20) + 512].copy_from_slice(&boot_sector(fat32, label));
        img
    }

    #[test]
    fn knows_which_device_an_image_is_for() {
        use crate::cards::Device;
        let dir = std::env::temp_dir().join(format!("zyperos-installer-device-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let cases = [("PX2BOOT", false, Some(Device::Pixel2)), ("PX2BOOT", true, Some(Device::Pixel2)),
                     ("BRICKBOOT", true, Some(Device::Brick)), ("EMUELEC", true, None)];
        for (label, fat32, want) in cases {
            let p = dir.join("t.img.xz");
            std::fs::write(&p, xz(&mbr_image(label, fat32))).unwrap();
            assert_eq!(device_of(&p).unwrap(), want, "{label}");
        }
        let p = dir.join("blank.img");
        std::fs::write(&p, vec![0u8; 1 << 20]).unwrap();
        assert_eq!(device_of(&p).unwrap(), None, "no table at all");
        std::fs::remove_dir_all(&dir).unwrap();
    }

    /// A real image's device: IMAGE=path cargo test real_device -- --ignored
    #[test]
    #[ignore]
    fn real_device() {
        let p = std::path::PathBuf::from(std::env::var("IMAGE").expect("IMAGE"));
        println!("{:?}", device_of(&p).unwrap());
    }

    #[test]
    fn a_plain_image_is_its_file() {
        let dir = std::env::temp_dir().join("zyperos-installer-image");
        std::fs::create_dir_all(&dir).unwrap();
        let p = dir.join("t.img");
        std::fs::write(&p, sample(4096)).unwrap();
        assert_eq!(size(&p).unwrap(), 4096);
    }
}
