// ZyperOS Installer: the window, and the commands its page calls. Run with
// `--write` it is the helper instead, with no window (install.rs).
//
// Built on TortOS Installer by Eric Reinsmidt, from its MIT-licensed versions
// (LICENSES/MIT-TortOS-Installer.txt): the writer, the cards and the way each
// system opens a disk are his, adapted for ZyperOS; the page is ZyperOS's own.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod cards;
mod device;
mod image;
mod install;
mod release;
mod writer;

use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use tauri::{AppHandle, Emitter, Manager, State};

/// Windows' AutoPlay held off while a card is written. Blanking a card's table
/// makes Windows mount it as one unformatted volume, and AutoPlay then offered
/// to format it, or opened it and said "The directory name is invalid", in the
/// middle of an install (seen 2026-10-24). Windows asks the window in front
/// with the "QueryCancelAutoPlay" message before it shows anything, and an
/// answer of 1 cancels it: the installer's window answers so while it writes.
#[cfg(windows)]
mod autoplay {
    use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
    use windows::core::w;
    use windows::Win32::Foundation::{HWND, LPARAM, LRESULT, WPARAM};
    use windows::Win32::UI::Shell::{DefSubclassProc, SetWindowSubclass};
    use windows::Win32::UI::WindowsAndMessaging::RegisterWindowMessageW;

    pub static HOLD: AtomicBool = AtomicBool::new(false);
    static QUERY: AtomicU32 = AtomicU32::new(0);

    unsafe extern "system" fn answer(hwnd: HWND, msg: u32, wp: WPARAM, lp: LPARAM, _id: usize, _data: usize) -> LRESULT {
        if msg != 0 && msg == QUERY.load(Ordering::Relaxed) && HOLD.load(Ordering::Relaxed) {
            return LRESULT(1);
        }
        DefSubclassProc(hwnd, msg, wp, lp)
    }

    /// The window to answer from: called on its own thread
    pub fn watch(hwnd: isize) {
        unsafe {
            QUERY.store(RegisterWindowMessageW(w!("QueryCancelAutoPlay")), Ordering::Relaxed);
            let _ = SetWindowSubclass(HWND(hwnd as _), Some(answer), 1, 0);
        }
    }
}

/// The write under way, if any: its cancel flag.
#[derive(Default)]
struct Running(Mutex<Option<Arc<AtomicBool>>>);

#[derive(serde::Serialize, Clone)]
struct ProgressEvent {
    phase: &'static str,
    done: u64,
    total: u64,
}

#[derive(serde::Serialize, Clone)]
struct FinishedEvent {
    ok: bool,
    cancelled: bool,
    message: String,
    /// Whether the card was ejected after a job that finished well, and if
    /// not, why
    ejected: bool,
    eject_error: String,
}

/// The cards in this computer and what is on each. Asks the system for its
/// disks, which takes a moment, so it runs off the window's thread.
#[tauri::command]
async fn list_cards() -> Vec<cards::Card> {
    tauri::async_runtime::spawn_blocking(cards::list).await.unwrap_or_default()
}

/// ZyperOS's latest version, for the screen to say what it installs.
#[tauri::command]
async fn latest_zyperos() -> Result<String, String> {
    if let Some(path) = local_release(false).or_else(|| local_release(true)) {
        return Ok(format!("from {}", path.file_name().unwrap_or_default().to_string_lossy()));
    }
    tauri::async_runtime::spawn_blocking(|| release::latest().map(|r| r.version().to_string()))
        .await
        .map_err(|e| e.to_string())?
}

/// A newer installer's version, if one is out.
#[tauri::command]
async fn newer_installer() -> Option<String> {
    tauri::async_runtime::spawn_blocking(release::newer_installer).await.ok().flatten()
}

/// The installer's releases page, in the browser. A fixed address: nothing
/// from the page goes into the command.
#[tauri::command]
fn open_installer_releases() {
    let url = release::INSTALLER_RELEASES;
    #[cfg(target_os = "macos")]
    let opener = "open";
    #[cfg(windows)]
    let opener = "explorer";
    #[cfg(target_os = "linux")]
    let opener = "xdg-open";
    let _ = std::process::Command::new(opener).arg(url).spawn();
}

/// A card image on this computer, chosen instead of a download: what the
/// screen shows of it before anything is written.
#[derive(serde::Serialize, Clone)]
struct LocalImage {
    path: String,
    name: String,
    /// The file's size, and the image's once unpacked
    file_bytes: u64,
    image_bytes: u64,
    device: cards::Device,
    /// Whether a SHA256SUMS beside it names it: then it is checked against
    /// that before it is written. Without one, an .xz still checks itself as
    /// it unpacks (each block has its own check), a plain .img nothing
    checksum: bool,
}

/// A local image looked at: it must be ZyperOS's, for either device, a
/// whole number of sectors once unpacked. Reads it, never writes it.
fn inspect(path: &Path) -> Result<LocalImage, String> {
    let name = path.file_name().unwrap_or_default().to_string_lossy().into_owned();
    let lower = name.to_ascii_lowercase();
    if !(lower.ends_with(".img") || lower.ends_with(".img.xz")) {
        return Err(format!("{name} isn't a card image. Choose an .img.xz or .img file."));
    }
    let meta = std::fs::metadata(path).map_err(|e| format!("Reading {name}: {e}"))?;
    if !meta.is_file() {
        return Err(format!("{name} isn't a file."));
    }
    let image_bytes = image::size(path).map_err(|e| format!("Reading {name}: {e}"))?;
    if image_bytes == 0 || image_bytes % writer::SECTOR != 0 {
        return Err(format!("{name} isn't a whole card image: its size isn't a whole number of sectors."));
    }
    let device = image::device_of(path)
        .map_err(|e| format!("Reading {name}: {e}"))?
        .ok_or_else(|| format!("{name} isn't a ZyperOS card image: it has no PX2BOOT or BRICKBOOT partition."))?;
    Ok(LocalImage {
        path: path.to_string_lossy().into_owned(),
        name,
        file_bytes: meta.len(),
        image_bytes,
        device,
        checksum: release::local_sum(path).is_some(),
    })
}

/// A local image, from a path the page has: one dropped on the window.
#[tauri::command]
async fn check_image(path: String) -> Result<LocalImage, String> {
    tauri::async_runtime::spawn_blocking(move || inspect(Path::new(&path)))
        .await
        .map_err(|e| e.to_string())?
}

/// The system's own file chooser, for a card image. None when it is closed
/// without one.
#[tauri::command]
async fn pick_image(app: AppHandle) -> Result<Option<LocalImage>, String> {
    use tauri_plugin_dialog::DialogExt;
    tauri::async_runtime::spawn_blocking(move || {
        let picked = app
            .dialog()
            .file()
            .set_title("Choose a ZyperOS card image")
            .add_filter("ZyperOS card image", &["xz", "img"])
            .blocking_pick_file();
        match picked.map(|p| p.into_path()) {
            None => Ok(None),
            Some(Ok(path)) => inspect(&path).map(Some),
            Some(Err(e)) => Err(format!("Opening that file: {e}")),
        }
    })
    .await
    .map_err(|e| e.to_string())?
}

/// A card image to use instead of downloading one, for trying out a build
/// before it is published: ZYPEROS_BRICK_IMAGE for the Brick, ZYPEROS_IMAGE
/// for the Pixel.
fn local_release(brick: bool) -> Option<PathBuf> {
    std::env::var_os(if brick { "ZYPEROS_BRICK_IMAGE" } else { "ZYPEROS_IMAGE" }).map(PathBuf::from)
}

fn find(card_id: &str, size: u64) -> Result<cards::Card, String> {
    let card = cards::list()
        .into_iter()
        .find(|c| c.id == card_id)
        .ok_or("The card isn't there anymore. Put it back in and choose it again.")?;
    // The one confirmed, not another card given the same name by the system
    // since: a card swapped in the same reader gets the same disk
    if card.size_bytes != size {
        return Err("The card in that reader changed. Choose it again.".into());
    }
    if card.locked {
        return Err("This card is locked: slide the lock switch on its side up, put it back in, and try again.".into());
    }
    Ok(card)
}

/// A new folder for a download, made fresh with a name nobody can guess and
/// open only to this user: nothing else on the computer can have made it
/// first or put its own file in place of the one that was checked, before
/// the write reads it (as root, through the helper, on Linux, where the
/// temporary folder is shared). Removed with what is in it after the write.
fn private_dir() -> Result<PathBuf, String> {
    use std::hash::{BuildHasher, Hasher};
    let random = std::collections::hash_map::RandomState::new().build_hasher().finish();
    let dir = std::env::temp_dir().join(format!("zyperos-installer-{}-{random:016x}", std::process::id()));
    let mut builder = std::fs::DirBuilder::new();
    #[cfg(unix)]
    std::os::unix::fs::DirBuilderExt::mode(&mut builder, 0o700);
    // Fails if anything is there already: never someone else's folder
    builder.create(&dir).map_err(|e| format!("Making a folder for the download: {e}"))?;
    Ok(dir)
}

/// The image to write, ready: downloaded and checked, or the local file
/// checked against the SHA256SUMS beside it. Returns its path and, for a
/// download, the folder it is in, to be removed after.
fn source(
    brick: bool,
    local: Option<PathBuf>,
    progress: &mut dyn FnMut(writer::Progress),
    cancel: &AtomicBool,
) -> Result<(PathBuf, Option<PathBuf>), String> {
    let device = if brick { cards::Device::Brick } else { cards::Device::Pixel2 };
    let cancelled = || cancel.load(Ordering::Relaxed);
    let (path, folder) = match local.or_else(|| local_release(brick)) {
        Some(path) => {
            let image = inspect(&path)?;
            if image.device != device {
                return Err(format!("{} is for {}, not {}.", image.name, device_name(image.device), device_name(device)));
            }
            if let Some(want) = release::local_sum(&path) {
                release::check_local(&path, &want, progress, &cancelled)?;
            }
            (path, None)
        }
        None => {
            let latest = release::latest()?;
            let asset = latest.asset(brick)?;
            let dir = private_dir()?;
            match release::download(&latest, asset, &dir, progress, &cancelled) {
                Ok(path) => (path, Some(dir)),
                Err(e) => {
                    let _ = std::fs::remove_dir_all(&dir);
                    return Err(e);
                }
            }
        }
    };
    // A release for the wrong device, or not ZyperOS's at all, never goes on
    if let Some(dir) = &folder {
        if image::device_of(&path).ok().flatten() != Some(device) {
            let _ = std::fs::remove_dir_all(dir);
            return Err(format!("The download isn't ZyperOS for {}. Nothing was written.", device_name(device)));
        }
    }
    Ok((path, folder))
}

fn device_name(d: cards::Device) -> &'static str {
    match d {
        cards::Device::Pixel2 => "the GKD Pixel 2",
        cards::Device::Brick => "the TrimUI Brick",
    }
}

/// The write, once the card is found again: it must still be there, the
/// size the player confirmed, and an update must be onto that device's
/// system. The image comes first; a download is removed after, with its
/// folder.
fn write(
    card_id: &str,
    card_size: u64,
    brick: bool,
    action: install::Action,
    local: Option<PathBuf>,
    progress: &mut dyn FnMut(writer::Progress),
    cancel: &AtomicBool,
) -> Result<(), String> {
    let card = find(card_id, card_size)?;
    let device = if brick { cards::Device::Brick } else { cards::Device::Pixel2 };
    if action == install::Action::Update && card.device != Some(device) {
        return Err(format!("This card doesn't have ZyperOS for {} on it to update.", device_name(device)));
    }
    let (path, folder) = source(brick, local, progress, cancel)?;
    // Found again after the download, which can take minutes: still there,
    // still the same card
    let result = find(card_id, card_size).and_then(|_| {
        let action = match action {
            install::Action::Update if brick => install::Action::BrickUpdate,
            a => a,
        };
        install::run(card_id, action, &path, card_size, progress, cancel)
    });
    if let Some(dir) = folder {
        let _ = std::fs::remove_dir_all(dir);
    }
    result
}

/// Starts a job; progress and the end arrive as "progress" and "finished"
/// events. `action` is update or fresh; `size` is the card's size as the
/// player confirmed it; `image` a local card image, or none to download the
/// latest release.
#[tauri::command]
fn start(app: AppHandle, running: State<Running>, card: String, size: u64, device: String, action: String,
         image: Option<String>) -> Result<(), String> {
    let brick = match device.as_str() {
        "brick" => true,
        "pixel2" => false,
        _ => return Err("Unknown device".into()),
    };
    let action = match install::Action::parse(&action) {
        Some(a @ (install::Action::Update | install::Action::Fresh)) => a,
        _ => return Err("Unknown action".into()),
    };
    let local = image.filter(|p| !p.is_empty()).map(PathBuf::from);
    let cancel = Arc::new(AtomicBool::new(false));
    {
        let mut slot = running.0.lock().unwrap();
        if slot.is_some() {
            return Err("A card is already being written.".into());
        }
        *slot = Some(cancel.clone());
    }
    std::thread::spawn(move || {
        let mut last = std::time::Instant::now();
        let mut last_phase = "";
        let mut progress = |p: writer::Progress| {
            let (phase, done, total) = match p {
                writer::Progress::Downloading { done, total } => ("downloading", done, total),
                writer::Progress::Verifying { done, total } => ("verifying", done, total),
                writer::Progress::Writing { done, total } => ("writing", done, total),
                writer::Progress::Checking { done, total } => ("checking", done, total),
            };
            // A new phase always gets through: the screen tells a stop before
            // anything was written from one after
            if phase != last_phase || done == total || last.elapsed().as_millis() >= 100 {
                let _ = app.emit("progress", ProgressEvent { phase, done, total });
                last = std::time::Instant::now();
                last_phase = phase;
            }
        };
        #[cfg(windows)]
        autoplay::HOLD.store(true, Ordering::Relaxed);
        let result = write(&card, size, brick, action, local, &mut progress, &cancel);
        // A card that finished well is ejected, ready to take out; one that
        // didn't stays, to be written again
        let eject = result.is_ok().then(|| {
            let _ = app.emit("progress", ProgressEvent { phase: "ejecting", done: 0, total: 0 });
            device::eject(&card)
        });
        #[cfg(windows)]
        autoplay::HOLD.store(false, Ordering::Relaxed);
        if let Some(r) = app.try_state::<Running>() {
            *r.0.lock().unwrap() = None;
        }
        let cancelled = matches!(&result, Err(e) if e == "cancelled");
        let (ok, message) = match result {
            Ok(()) => (true, String::new()),
            Err(e) => (false, e),
        };
        let ejected = matches!(eject, Some(Ok(())));
        let eject_error = eject.and_then(Result::err).unwrap_or_default();
        let _ = app.emit("finished", FinishedEvent { ok, cancelled, message, ejected, eject_error });
    });
    Ok(())
}

/// Stops the job at the next chunk.
#[tauri::command]
fn cancel(running: State<Running>) {
    if let Some(c) = running.0.lock().unwrap().as_ref() {
        c.store(true, Ordering::Relaxed);
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.get(1).map(String::as_str) == Some("--write") {
        std::process::exit(install::helper(&args[2..]));
    }
    // The cards the window would offer, as JSON, and nothing else: reads
    // only. For checking on a new system (and in CI) that no fixed or system
    // disk is ever listed.
    if args.get(1).map(String::as_str) == Some("--list") {
        println!("{}", serde_json::to_string_pretty(&cards::list()).unwrap_or_default());
        return;
    }
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .manage(Running::default())
        .setup(|_app| {
            #[cfg(windows)]
            if let Some(Ok(hwnd)) = _app.get_webview_window("main").map(|w| w.hwnd()) {
                autoplay::watch(hwnd.0 as isize);
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![list_cards, latest_zyperos, newer_installer, open_installer_releases,
                                                 check_image, pick_image, start, cancel])
        .run(tauri::generate_context!())
        .expect("ZyperOS Installer could not start");
}

#[cfg(test)]
mod tests {
    #[test]
    fn a_download_folder_is_new_and_private() {
        let a = super::private_dir().unwrap();
        let b = super::private_dir().unwrap();
        assert_ne!(a, b, "a name of its own each time");
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            assert_eq!(std::fs::metadata(&a).unwrap().permissions().mode() & 0o777, 0o700);
        }
        std::fs::remove_dir(&a).unwrap();
        std::fs::remove_dir(&b).unwrap();
    }
}
