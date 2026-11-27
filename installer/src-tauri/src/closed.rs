// The TrimUI Brick's closed files, the player's own copy (ZyperOS's spec,
// section 7): Imagination's PowerVR libraries and GPU firmware, and XRadio's
// Wi-Fi and Bluetooth firmware. ZyperOS's images carry none of them. For a
// Brick card, the installer fetches them from where their publisher put them,
// Creality's Sonic Pad SDK at 7f37a7f, checks each against plastron's own
// sums, and puts them in closed/ on the card's boot partition, outside the
// system slots, where the Brick's system links to them and System Update
// leaves them alone. They are fetched before anything is written, so a failed
// download writes nothing; and they go into the boot partition of the image
// in memory, on its way to the card, so no card is ever mounted to do it.

use std::io::{self, Cursor, Read};
use std::path::{Path, PathBuf};

use sha2::{Digest, Sha256};

/// A closed file: its name on the card, its path in the Sonic Pad SDK, and
/// its sum
pub struct ClosedFile {
    pub name: &'static str,
    pub path: &'static str,
    pub sha256: &'static str,
}

// LIST, made by build.rs from plastron's package/pvr-ge8300/files.sha256 and
// package/xr829-firmware/files.sha256
include!(concat!(env!("OUT_DIR"), "/closed_list.rs"));

/// Where the files are: the Sonic Pad SDK at its pinned commit
pub const SDK: &str = "https://raw.githubusercontent.com/CrealityTech/sonic_pad_os/7f37a7fbf4ad59907034715e3d967b9177de1cd4/";

/// Their folder on the boot partition
pub const FOLDER: &str = "closed";

/// The biggest a closed file can be: the GPU's compiler is under 2 MB
const MAX_BYTES: u64 = 16 << 20;

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

fn sum_of(bytes: &[u8]) -> String {
    hex(&Sha256::digest(bytes))
}

/// `dir`, made if it isn't there, private to whoever runs this: 0700 and
/// theirs, so no one else can put a file or a link where the closed files
/// are kept
fn private_dir(dir: &Path) -> Result<(), String> {
    std::fs::create_dir_all(dir).map_err(|e| format!("Making a folder for the Brick's files: {e}"))?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::{MetadataExt, PermissionsExt};
        let meta = std::fs::symlink_metadata(dir).map_err(|e| format!("The Brick's files' folder: {e}"))?;
        // SAFETY: geteuid has no preconditions and cannot fail
        if !meta.is_dir() || meta.uid() != unsafe { libc::geteuid() } {
            return Err("The folder for the Brick's files isn't this user's own.".into());
        }
        std::fs::set_permissions(dir, std::fs::Permissions::from_mode(0o700))
            .map_err(|e| format!("The Brick's files' folder: {e}"))?;
    }
    Ok(())
}

/// A file's bytes, opened without following a link at its name
fn read_no_follow(path: &Path) -> std::io::Result<Vec<u8>> {
    let mut opts = std::fs::OpenOptions::new();
    opts.read(true);
    #[cfg(unix)]
    std::os::unix::fs::OpenOptionsExt::custom_flags(&mut opts, libc::O_NOFOLLOW);
    let mut v = Vec::new();
    opts.open(path)?.read_to_end(&mut v)?;
    Ok(v)
}

/// `bytes` into a new file at `path`: never over one there, never through a
/// link
fn write_new(path: &Path, bytes: &[u8]) -> std::io::Result<()> {
    let _ = std::fs::remove_file(path);
    let mut opts = std::fs::OpenOptions::new();
    opts.write(true).create_new(true);
    #[cfg(unix)]
    std::os::unix::fs::OpenOptionsExt::custom_flags(&mut opts, libc::O_NOFOLLOW);
    io::Write::write_all(&mut opts.open(path)?, bytes)
}

/// A URL's bytes, from the network
pub fn http_get(url: &str) -> Result<Vec<u8>, String> {
    let agent: ureq::Agent = ureq::Agent::config_builder()
        .user_agent(concat!("zyperos-installer/", env!("CARGO_PKG_VERSION")))
        .timeout_connect(Some(std::time::Duration::from_secs(15)))
        .build()
        .into();
    let mut response = agent.get(url).call().map_err(|e| e.to_string())?;
    response
        .body_mut()
        .with_config()
        .limit(MAX_BYTES)
        .read_to_vec()
        .map_err(|e| e.to_string())
}

/// The files of `list` in `cache`, by their names: one already there with
/// its sum is kept, the rest got with `get` from `base` and checked. An error
/// names the file and says what went wrong; then nothing is to be written.
pub fn fetch(
    list: &[ClosedFile],
    cache: &Path,
    base: &str,
    get: &dyn Fn(&str) -> Result<Vec<u8>, String>,
    progress: &mut dyn FnMut(u64, u64),
    cancelled: &dyn Fn() -> bool,
) -> Result<Vec<PathBuf>, String> {
    private_dir(cache)?;
    let total = list.len() as u64;
    let mut out = Vec::with_capacity(list.len());
    for (i, f) in list.iter().enumerate() {
        if cancelled() {
            return Err("cancelled".into());
        }
        progress(i as u64, total);
        let path = cache.join(f.name);
        let kept = read_no_follow(&path).map(|b| sum_of(&b) == f.sha256).unwrap_or(false);
        if !kept {
            let bytes = get(&format!("{base}{}", f.path)).map_err(|e| {
                format!("Couldn't get the Brick's file {} from GitHub: {e}. Nothing was written.", f.name)
            })?;
            if sum_of(&bytes) != f.sha256 {
                return Err(format!(
                    "The Brick's file {} from GitHub isn't the one ZyperOS expects (its checksum differs). Nothing was written.",
                    f.name
                ));
            }
            let part = cache.join(format!("{}.part", f.name));
            write_new(&part, &bytes)
                .and_then(|_| std::fs::rename(&part, &path))
                .map_err(|e| format!("Keeping the Brick's file {}: {e}", f.name))?;
        }
        out.push(path);
    }
    progress(total, total);
    Ok(out)
}

/// The closed files at `files`, each checked again here against `list`: its
/// name one of the list's, opened without following a link, its bytes the
/// sum the list has. What goes on the card is exactly these bytes. The
/// installer's helper runs this as root on Linux, from paths the window
/// gave it, so it trusts nothing about them.
pub fn load_checked(list: &[ClosedFile], files: &[PathBuf]) -> Result<Vec<(String, Vec<u8>)>, String> {
    files
        .iter()
        .map(|path| {
            let name = path.file_name().and_then(|n| n.to_str()).ok_or("A closed file has no name")?;
            let want = list
                .iter()
                .find(|f| f.name == name)
                .ok_or_else(|| format!("{name} isn't one of the Brick's files."))?;
            let bytes = read_no_follow(path).map_err(|e| format!("Reading the Brick's file {name}: {e}"))?;
            if sum_of(&bytes) != want.sha256 {
                return Err(format!("The Brick's file {name} changed after it was checked. Nothing was written."));
            }
            Ok((name.to_string(), bytes))
        })
        .collect()
}

/// Files for a folder on the boot partition: (name, bytes)
pub type Files = [(String, Vec<u8>)];

/// How many clusters `folder` with `files` takes on a FAT whose clusters are
/// `cluster` bytes, less what its files there already take (written over),
/// and whether the folder is there yet. Its directory: each long name takes
/// up to three entries of 32 bytes.
fn clusters_for<T: io::Read + io::Write + io::Seek>(
    root: &fatfs::Dir<T>,
    folder: &str,
    files: &Files,
    cluster: u64,
) -> u64 {
    let of = |bytes: u64| bytes.div_ceil(cluster);
    let mut need: u64 = files.iter().map(|(_, b)| of(b.len() as u64)).sum();
    match root.open_dir(folder) {
        Ok(dir) => {
            for e in dir.iter().flatten() {
                if e.is_file() && files.iter().any(|(n, _)| e.file_name().eq_ignore_ascii_case(n)) {
                    need = need.saturating_sub(of(e.len()));
                }
            }
        }
        // A new folder: its entries, and one more in the folder above
        Err(_) => need += of((files.len() as u64 * 3 + 2) * 32) + 1,
    }
    need
}

/// Why `folders` don't fit, with what they need and what is free, in bytes
fn no_room(folders: &[(&str, &Files)], need: &[u64], free: u64) -> String {
    let mb = |n: u64| format!("{:.1}", n as f64 / 1e6);
    match folders.iter().position(|(f, _)| *f == crate::pico8::FOLDER) {
        Some(i) => {
            let others: u64 = need.iter().enumerate().filter(|(j, _)| *j != i).map(|(_, n)| n).sum();
            format!(
                "Your PICO-8 doesn't fit in the card image's boot partition: it needs {} MB, and {} MB are free. \
                 Install without it, and add it with Add PICO-8 after the card's first start. Nothing was written.",
                mb(need[i]),
                mb(free.saturating_sub(others))
            )
        }
        None => format!(
            "The Brick's files don't fit in the card image's boot partition: they need {} MB, and {} MB are free. \
             Nothing was written.",
            mb(need.iter().sum()),
            mb(free)
        ),
    }
}

/// A boot partition's FAT (its whole bytes) with each folder's files (closed/
/// for the Brick's, pico8/ for the player's PICO-8): a folder made if it isn't
/// there, a file there already written over, everything else left as it was.
/// Refused, with nothing changed, when they don't fit in its free space.
pub fn into_boot(boot: Vec<u8>, folders: &[(&str, &Files)]) -> Result<Vec<u8>, String> {
    let mut disk = Cursor::new(boot);
    {
        let fs = fatfs::FileSystem::new(&mut disk, fatfs::FsOptions::new())
            .map_err(|e| format!("Reading the image's boot partition: {e}"))?;
        {
            let root = fs.root_dir();
            let stats = fs.stats().map_err(|e| format!("Reading the image's boot partition: {e}"))?;
            let cluster = u64::from(stats.cluster_size());
            let need: Vec<u64> = folders.iter().map(|(f, files)| clusters_for(&root, f, files, cluster)).collect();
            let free = u64::from(stats.free_clusters());
            if need.iter().sum::<u64>() > free {
                let bytes: Vec<u64> = need.iter().map(|n| n * cluster).collect();
                return Err(no_room(folders, &bytes, free * cluster));
            }
            for (folder, files) in folders {
                let dir = match root.open_dir(folder) {
                    Ok(d) => d,
                    Err(_) => root
                        .create_dir(folder)
                        .map_err(|e| format!("Making {folder}/ on the boot partition: {e}"))?,
                };
                for (name, bytes) in files.iter() {
                    let name = name.as_str();
                    let mut file =
                        dir.create_file(name).map_err(|e| format!("Putting {name} on the boot partition: {e}"))?;
                    file.truncate().map_err(|e| format!("Putting {name} on the boot partition: {e}"))?;
                    io::Write::write_all(&mut file, bytes).map_err(|e| {
                        format!("Putting {name} on the boot partition: {e} (is the image's boot partition full?)")
                    })?;
                    io::Write::flush(&mut file).map_err(|e| format!("Putting {name} on the boot partition: {e}"))?;
                }
            }
        }
        fs.unmount().map_err(|e| format!("Finishing the boot partition: {e}"))?;
    }
    Ok(disk.into_inner())
}

/// An image read with other bytes over part of it: `data` in place of the
/// image's own from byte `at`. The writer reads the image twice (writing,
/// then checking the card), so both see the same.
pub struct Overlay<R: Read> {
    inner: R,
    pos: u64,
    at: u64,
    data: std::sync::Arc<Vec<u8>>,
}

impl<R: Read> Overlay<R> {
    pub fn new(inner: R, at: u64, data: std::sync::Arc<Vec<u8>>) -> Self {
        Overlay { inner, pos: 0, at, data }
    }
}

impl<R: Read> Read for Overlay<R> {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let n = self.inner.read(buf)?;
        let (start, end) = (self.pos, self.pos + n as u64);
        let (over_start, over_end) = (self.at, self.at + self.data.len() as u64);
        let (from, to) = (start.max(over_start), end.min(over_end));
        if from < to {
            let dst = (from - start) as usize..(to - start) as usize;
            let src = (from - over_start) as usize..(to - over_start) as usize;
            buf[dst].copy_from_slice(&self.data[src]);
        }
        self.pos = end;
        Ok(n)
    }
}

/// The image's first partition (its boot partition): where it starts and
/// how many bytes it is, from the image's own table. Reads only the head.
pub fn boot_span(image: &mut dyn Read) -> Result<(u64, u64), String> {
    use crate::writer::{table_of, HEAD, SECTOR};
    let mut head = vec![0u8; HEAD];
    image.read_exact(&mut head).map_err(|e| format!("Reading the image: {e}"))?;
    let table = table_of(&head).ok_or("The image has no partition table")?;
    let boot = table.first().copied().flatten().ok_or("The image has no boot partition")?;
    Ok((boot.start * SECTOR, boot.sectors * SECTOR))
}

/// The image's boot partition, read from its start: where it starts and its
/// whole bytes, for what goes over the image's own at its offset (Overlay)
pub fn boot_of(open: &dyn Fn() -> io::Result<Box<dyn Read + Send>>) -> Result<(u64, Vec<u8>), String> {
    let (at, len) = boot_span(&mut *open().map_err(|e| format!("Opening the image: {e}"))?)?;
    let mut image = open().map_err(|e| format!("Opening the image: {e}"))?;
    io::copy(&mut (&mut image).take(at), &mut io::sink()).map_err(|e| format!("Reading the image: {e}"))?;
    let mut boot = vec![0u8; len as usize];
    image.read_exact(&mut boot).map_err(|e| format!("Reading the image's boot partition: {e}"))?;
    Ok((at, boot))
}

/// The image's boot partition with each folder's files in (into_boot)
#[cfg(test)]
pub fn boot_with(
    open: &dyn Fn() -> io::Result<Box<dyn Read + Send>>,
    folders: &[(&str, &Files)],
) -> Result<(u64, Vec<u8>), String> {
    let (at, boot) = boot_of(open)?;
    Ok((at, into_boot(boot, folders)?))
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use std::io::{Seek, SeekFrom, Write};

    fn temp(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("zyperos-closed-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    // A boot partition as the Brick's image has it: FAT, a/ with its kernel,
    // bootstate.txt
    pub(crate) fn boot_fat() -> Vec<u8> {
        let mut disk = Cursor::new(vec![0u8; 8 << 20]);
        fatfs::format_volume(&mut disk, fatfs::FormatVolumeOptions::new().volume_label(*b"BRICKBOOT  ")).unwrap();
        {
            let fs = fatfs::FileSystem::new(&mut disk, fatfs::FsOptions::new()).unwrap();
            {
                let a = fs.root_dir().create_dir("a").unwrap();
                a.create_file("Image.lz4").unwrap().write_all(b"kernel").unwrap();
                fs.root_dir().create_file("bootstate.txt").unwrap().write_all(b"slot=a\n").unwrap();
            }
            fs.unmount().unwrap();
        }
        disk.into_inner()
    }

    pub(crate) fn read_in(boot: &[u8], path: &str) -> Option<Vec<u8>> {
        let mut disk = Cursor::new(boot.to_vec());
        let fs = fatfs::FileSystem::new(&mut disk, fatfs::FsOptions::new()).unwrap();
        let mut f = fs.root_dir().open_file(path).ok()?;
        let mut v = Vec::new();
        f.read_to_end(&mut v).unwrap();
        Some(v)
    }

    #[test]
    fn the_list_is_plastrons() {
        assert_eq!(LIST.len(), 13);
        let names: Vec<_> = LIST.iter().map(|f| f.name).collect();
        for n in ["libEGL.so", "libGLESv2.so", "libpvrNULL_WSEGL.so", "rgx.fw.22.102.54.38", "fw_xr829.bin", "fw_xr829_bt.bin"] {
            assert!(names.contains(&n), "{n} in the list");
        }
        assert!(LIST.iter().any(|f| f.path == "package/libs/libgpu/ge8300/fbdev/glibc/lib64/firmware/rgx.fw.22.102.54.38"));
        assert!(LIST.iter().any(|f| f.path == "package/firmware/linux-firmware/xr829/boot_xr829.bin"));
        assert!(LIST.iter().all(|f| f.sha256.len() == 64));
    }

    fn fixture_list(bytes: &[(&'static str, &[u8])]) -> Vec<ClosedFile> {
        bytes
            .iter()
            .map(|(name, b)| ClosedFile { name, path: name, sha256: Box::leak(sum_of(b).into_boxed_str()) })
            .collect()
    }

    #[test]
    fn fetches_checks_and_keeps() {
        let cache = temp("fetch");
        let list = fixture_list(&[("libEGL.so", b"egl"), ("fw_xr829.bin", b"wifi")]);
        let asked = std::cell::RefCell::new(Vec::new());
        let get = |url: &str| {
            asked.borrow_mut().push(url.to_string());
            Ok(match url { "base/libEGL.so" => b"egl".to_vec(), _ => b"wifi".to_vec() })
        };
        let files = fetch(&list, &cache, "base/", &get, &mut |_, _| {}, &|| false).unwrap();
        assert_eq!(files.len(), 2);
        assert_eq!(std::fs::read(cache.join("libEGL.so")).unwrap(), b"egl");
        // A second time, the cache has them: nothing fetched
        asked.borrow_mut().clear();
        fetch(&list, &cache, "base/", &get, &mut |_, _| {}, &|| false).unwrap();
        assert!(asked.borrow().is_empty());
        // A cached file that changed is fetched again
        std::fs::write(cache.join("fw_xr829.bin"), b"broken").unwrap();
        fetch(&list, &cache, "base/", &get, &mut |_, _| {}, &|| false).unwrap();
        assert_eq!(asked.borrow().as_slice(), ["base/fw_xr829.bin"]);
    }

    #[test]
    fn a_wrong_sum_or_no_network_names_the_file() {
        let cache = temp("wrong");
        let list = fixture_list(&[("libEGL.so", b"egl")]);
        let e = fetch(&list, &cache, "", &|_: &str| Ok(b"other".to_vec()), &mut |_, _| {}, &|| false).unwrap_err();
        assert!(e.contains("libEGL.so") && e.contains("checksum") && e.contains("Nothing was written"), "{e}");
        assert!(!cache.join("libEGL.so").exists());
        let e = fetch(&list, &cache, "", &|_: &str| Err("offline".into()), &mut |_, _| {}, &|| false).unwrap_err();
        assert!(e.contains("libEGL.so") && e.contains("offline") && e.contains("Nothing was written"), "{e}");
    }

    #[test]
    fn into_the_boot_partition_and_nothing_else() {
        let files = vec![
            ("libGLESv2.so".to_string(), b"gles".to_vec()),
            ("rgx.fw.22.102.54.38".to_string(), b"firmware".to_vec()),
        ];
        let boot = into_boot(boot_fat(), &[(FOLDER, &files)]).unwrap();
        assert_eq!(read_in(&boot, "closed/libGLESv2.so").unwrap(), b"gles");
        assert_eq!(read_in(&boot, "closed/rgx.fw.22.102.54.38").unwrap(), b"firmware");
        assert_eq!(read_in(&boot, "a/Image.lz4").unwrap(), b"kernel");
        assert_eq!(read_in(&boot, "bootstate.txt").unwrap(), b"slot=a\n");
        // A folder from before, one file cut short: written over whole
        let again = into_boot(boot, &[(FOLDER, &[("libGLESv2.so".to_string(), b"gles, all of it".to_vec())])]).unwrap();
        assert_eq!(read_in(&again, "closed/libGLESv2.so").unwrap(), b"gles, all of it");
        assert_eq!(read_in(&again, "closed/rgx.fw.22.102.54.38").unwrap(), b"firmware");
    }

    #[test]
    fn the_helper_checks_each_file_again() {
        let dir = temp("checked");
        let list = fixture_list(&[("libEGL.so", b"egl")]);
        std::fs::write(dir.join("libEGL.so"), b"egl").unwrap();
        let ok = load_checked(&list, &[dir.join("libEGL.so")]).unwrap();
        assert_eq!(ok, vec![("libEGL.so".to_string(), b"egl".to_vec())]);
        // Changed after the window checked it
        std::fs::write(dir.join("libEGL.so"), b"not egl").unwrap();
        assert!(load_checked(&list, &[dir.join("libEGL.so")]).unwrap_err().contains("changed"));
        // A name that isn't the list's
        std::fs::write(dir.join("other.so"), b"egl").unwrap();
        assert!(load_checked(&list, &[dir.join("other.so")]).unwrap_err().contains("isn't one of"));
        // A link where the file should be is not followed
        #[cfg(unix)]
        {
            std::fs::write(dir.join("real"), b"egl").unwrap();
            let link = temp("link").join("libEGL.so");
            std::os::unix::fs::symlink(dir.join("real"), &link).unwrap();
            assert!(load_checked(&list, &[link]).is_err());
        }
    }

    #[cfg(unix)]
    #[test]
    fn the_cache_is_private() {
        use std::os::unix::fs::PermissionsExt;
        let cache = temp("private").join("brick-closed");
        let list = fixture_list(&[("libEGL.so", b"egl")]);
        fetch(&list, &cache, "", &|_: &str| Ok(b"egl".to_vec()), &mut |_, _| {}, &|| false).unwrap();
        assert_eq!(std::fs::metadata(&cache).unwrap().permissions().mode() & 0o777, 0o700);
    }

    #[test]
    fn the_overlay_puts_its_bytes_in_place() {
        let image: Vec<u8> = (0..100u8).collect();
        let data = std::sync::Arc::new(vec![0xEEu8; 10]);
        // Read in odd pieces, across the overlay's edges
        let mut r = Overlay::new(Cursor::new(image.clone()), 37, data);
        let mut out = Vec::new();
        let mut buf = [0u8; 7];
        loop {
            let n = r.read(&mut buf).unwrap();
            if n == 0 {
                break;
            }
            out.extend_from_slice(&buf[..n]);
        }
        let mut want = image;
        want[37..47].fill(0xEE);
        assert_eq!(out, want);
    }

    #[test]
    fn the_boot_partition_from_a_gpt_image() {
        // A GPT image whose first partition is a FAT at 1 MiB: boot_with
        // reads it, puts the files in, and says where it goes
        let fat = boot_fat();
        let mut image = vec![0u8; (1 << 20) + fat.len() + (1 << 20)];
        image[(1 << 20)..(1 << 20) + fat.len()].copy_from_slice(&fat);
        let table = crate::writer::tests::gpt_with(&[(2048, (fat.len() as u64) / 512)]);
        image[..table.len()].copy_from_slice(&table);
        let path = temp("gpt").join("brick.img");
        std::fs::write(&path, &image).unwrap();
        let open = || crate::image::open(&path);
        let (at, boot) = boot_with(&open, &[(FOLDER, &[("fw_xr829.bin".to_string(), b"wifi".to_vec())])]).unwrap();
        assert_eq!(at, 1 << 20);
        assert_eq!(boot.len(), fat.len());
        assert_eq!(read_in(&boot, "closed/fw_xr829.bin").unwrap(), b"wifi");
        // The image file itself is not changed
        let mut f = std::fs::File::open(&path).unwrap();
        f.seek(SeekFrom::Start(1 << 20)).unwrap();
        let mut first = vec![0u8; 512];
        f.read_exact(&mut first).unwrap();
        assert_eq!(first, fat[..512]);
    }
}
