// ZyperOS Installer: the window, and the commands its page calls. Run with
// `--write` it is the helper instead, with no window (install.rs).
//
// Built on TortOS Installer by Eric Reinsmidt, from its MIT-licensed versions
// (LICENSES/MIT-TortOS-Installer.txt): the writer, the cards and the way each
// system opens a disk are his, adapted for ZyperOS; the page is ZyperOS's own.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod cards;
mod closed;
mod device;
mod image;
mod install;
mod pico8;
mod release;
mod selfupdate;
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

/// What the release row installs: for the screen to say which ZyperOS, and to
/// offer the devices it has an image for.
#[derive(serde::Serialize, Clone, Debug, PartialEq)]
struct Latest {
    /// "1.0.5", or "from <file>" with a local image in place of the download
    version: String,
    /// The devices the newest release has a card image for, and those with a
    /// local image: a device with neither is not offered
    devices: Vec<cards::Device>,
}

/// ZyperOS's latest version and its devices.
#[tauri::command]
async fn latest_zyperos() -> Result<Latest, String> {
    tauri::async_runtime::spawn_blocking(latest).await.map_err(|e| e.to_string())?
}

fn latest() -> Result<Latest, String> {
    let local: Vec<(cards::Device, PathBuf)> =
        cards::Device::ALL.into_iter().filter_map(|d| local_release(d).map(|p| (d, p))).collect();
    let Some((_, path)) = local.first() else {
        let r = release::latest()?;
        return Ok(Latest { version: r.version().to_string(), devices: r.devices() });
    };
    // A local image installs its own device whatever the release has; the
    // others still download, where GitHub answers
    let released = release::latest().map(|r| r.devices()).unwrap_or_default();
    Ok(Latest {
        version: format!("from {}", path.file_name().unwrap_or_default().to_string_lossy()),
        devices: with_local(&released, &local.iter().map(|(d, _)| *d).collect::<Vec<_>>()),
    })
}

/// The released devices and the local ones together, in the page's order
fn with_local(released: &[cards::Device], local: &[cards::Device]) -> Vec<cards::Device> {
    cards::Device::ALL.into_iter().filter(|d| released.contains(d) || local.contains(d)).collect()
}

/// A newer installer, if one is out, and whether this one can put it in its
/// own place (selfupdate.rs). Says nothing when GitHub can't be asked: this
/// is only ever a notice.
#[tauri::command]
async fn installer_update(app: AppHandle) -> Option<selfupdate::Offer> {
    let pending = app.state::<selfupdate::Pending>();
    let why = match selfupdate::way(&pending.exe, pending.original()) {
        selfupdate::Way::InPlace => {
            // Tauri's updater first; when its look doesn't work (no
            // latest.json in the latest release, as up to 1.0.5), the
            // releases' look below
            #[cfg(any(target_os = "macos", windows))]
            if let Ok(offer) = selfupdate::check(&app, &pending).await {
                return offer;
            }
            None
        }
        selfupdate::Way::Page(why) => why,
    };
    let version = tauri::async_runtime::spawn_blocking(release::newer_installer).await.ok().flatten()?;
    Some(selfupdate::Offer { version, in_place: false, why: why.map(String::from) })
}

#[cfg(any(target_os = "macos", windows))]
#[derive(serde::Serialize, Clone)]
struct UpdateProgress {
    done: u64,
    /// 0 when the server didn't say
    total: u64,
}

/// Downloads the newer installer installer_update found and checks its
/// signature; its progress arrives as "update-progress" events. It doesn't
/// touch any card, so it may go on during a write.
#[tauri::command]
async fn download_update(app: AppHandle) -> Result<(), String> {
    #[cfg(any(target_os = "macos", windows))]
    {
        let pending = app.state::<selfupdate::Pending>();
        let mut last: Option<std::time::Instant> = None;
        selfupdate::download(&pending, |done, total| {
            if last.is_none_or(|t| t.elapsed().as_millis() >= 100) || Some(done) == total {
                let _ = app.emit("update-progress", UpdateProgress { done, total: total.unwrap_or(0) });
                last = Some(std::time::Instant::now());
            }
        })
        .await
    }
    #[cfg(not(any(target_os = "macos", windows)))]
    {
        let _ = app;
        Err("This installer can't update itself on this system.".into())
    }
}

/// What restart_to_update says during a write; the page keeps offering it
const BEING_WRITTEN: &str = "A card is being written. Restart when it is done.";

/// Puts the downloaded installer in this one's place and opens it. Never
/// during a write; and no write starts while it goes in place.
#[tauri::command]
async fn restart_to_update(app: AppHandle) -> Result<(), String> {
    #[cfg(any(target_os = "macos", windows))]
    {
        let pending = app.state::<selfupdate::Pending>();
        {
            // Under the write's lock: start() looks at installing under it too
            let running = app.state::<Running>();
            let slot = running.0.lock().unwrap();
            if slot.is_some() {
                return Err(BEING_WRITTEN.into());
            }
            pending.installing.store(true, Ordering::SeqCst);
        }
        let handle = app.clone();
        let installed = tauri::async_runtime::spawn_blocking(move || {
            selfupdate::install(&handle.state::<selfupdate::Pending>())
        })
        .await
        .map_err(|e| e.to_string())
        .and_then(|r| r);
        match installed {
            Ok(()) => selfupdate::restart(&app, &pending),
            Err(e) => {
                pending.installing.store(false, Ordering::SeqCst);
                Err(e)
            }
        }
    }
    #[cfg(not(any(target_os = "macos", windows)))]
    {
        let _ = (app, BEING_WRITTEN);
        Err("This installer can't update itself on this system.".into())
    }
}

/// Debug builds only: with ZYPEROS_INSTALLER_SELFUPDATE_TEST naming a file,
/// the window updates the installer as soon as it opens, with no click, and
/// writes each step to that file: for trying an update end to end on this
/// computer with a test key and a local latest.json (installer/README.md).
#[cfg(all(debug_assertions, any(target_os = "macos", windows)))]
async fn selfupdate_test(app: AppHandle, log: PathBuf) {
    use std::io::Write;
    let say = |line: String| {
        if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(&log) {
            let _ = writeln!(f, "{line}");
        }
    };
    let pending = app.state::<selfupdate::Pending>();
    say(format!("running {} from {}", app.package_info().version, pending.exe.display()));
    match selfupdate::check(&app, &pending).await {
        Ok(Some(offer)) => say(format!("offered {}", offer.version)),
        Ok(None) => return say("up to date".into()),
        Err(e) => return say(format!("check failed: {e}")),
    }
    let mut seen = 0;
    if let Err(e) = selfupdate::download(&pending, |done, _| seen = done).await {
        return say(format!("download failed: {e}"));
    }
    say(format!("downloaded and checked {seen} bytes"));
    if let Err(e) = selfupdate::install(&pending) {
        return say(format!("install failed: {e}"));
    }
    say("installed, restarting".into());
    selfupdate::restart(&app, &pending);
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

/// The player's PICO-8 zip, from a path the page has: one dropped on the
/// window. Reads it, never keeps it.
#[tauri::command]
async fn check_pico8(path: String) -> Result<pico8::Chosen, String> {
    tauri::async_runtime::spawn_blocking(move || pico8::look(Path::new(&path)))
        .await
        .map_err(|e| e.to_string())
}

/// The system's own file chooser, for the player's PICO-8 zip: its path,
/// for check_pico8 next (the page says "Checking the zip" meanwhile). None
/// when it is closed without one.
#[tauri::command]
async fn pick_pico8(app: AppHandle) -> Result<Option<String>, String> {
    use tauri_plugin_dialog::DialogExt;
    tauri::async_runtime::spawn_blocking(move || {
        let picked = app
            .dialog()
            .file()
            .set_title("Choose your PICO-8 for Raspberry Pi zip")
            .add_filter("PICO-8 for Raspberry Pi", &["zip"])
            .blocking_pick_file();
        match picked.map(|p| p.into_path()) {
            None => Ok(None),
            Some(Ok(path)) => Ok(Some(path.to_string_lossy().into_owned())),
            Some(Err(e)) => Err(format!("Opening that file: {e}")),
        }
    })
    .await
    .map_err(|e| e.to_string())?
}

/// A card image to use instead of downloading one, for trying out a build
/// before it is published: ZYPEROS_BRICK_IMAGE for the Brick,
/// ZYPEROS_SMARTPRO_IMAGE for the Smart Pro, ZYPEROS_IMAGE for the Pixel. Its
/// device is offered even when the release has no image for it.
fn local_release(device: cards::Device) -> Option<PathBuf> {
    let var = match device {
        cards::Device::Pixel2 => "ZYPEROS_IMAGE",
        cards::Device::Brick => "ZYPEROS_BRICK_IMAGE",
        cards::Device::SmartPro => "ZYPEROS_SMARTPRO_IMAGE",
    };
    std::env::var_os(var).map(PathBuf::from)
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
    device: cards::Device,
    local: Option<PathBuf>,
    progress: &mut dyn FnMut(writer::Progress),
    cancel: &AtomicBool,
) -> Result<(PathBuf, Option<PathBuf>), String> {
    let cancelled = || cancel.load(Ordering::Relaxed);
    let (path, folder) = match local.or_else(|| local_release(device)) {
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
            let asset = latest.asset(device)?;
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
        cards::Device::SmartPro => "the TrimUI Smart Pro",
    }
}

/// What the PICO-8 row says when the card's games partition can't be
/// copied to
const NOT_MOUNTED: &str = "The card's ZYPEROS volume is not mounted.";

/// Where a started card's PICO-8 goes: its mounted games partition
fn games_volume(card: &cards::Card) -> Result<PathBuf, String> {
    card.volume.as_ref().map(PathBuf::from).ok_or_else(|| format!("{NOT_MOUNTED} Nothing was written."))
}

/// The write, once the card is found again: it must still be there, the
/// size the player confirmed, and an update must be onto that device's
/// system. The player's PICO-8 zip is checked first, so a zip that changed
/// writes nothing. For the Brick its closed files come next (closed.rs),
/// into `cache`, so a failed download writes nothing; then the image, with
/// PICO-8 copied to a started card's games partition just before it (the
/// system write unmounts it), or riding along in the image's boot partition
/// for a card that never started, as every fresh install is; a download is
/// removed after, with its folder.
#[allow(clippy::too_many_arguments)]
fn write(
    card_id: &str,
    card_size: u64,
    device: cards::Device,
    action: install::Action,
    local: Option<PathBuf>,
    zip: Option<PathBuf>,
    cache: Option<&Path>,
    progress: &mut dyn FnMut(writer::Progress),
    cancel: &AtomicBool,
) -> Result<(), String> {
    let card = find(card_id, card_size)?;
    if action == install::Action::Update && card.device != Some(device) {
        return Err(format!("This card doesn't have ZyperOS for {} on it to update.", device_name(device)));
    }
    let p8 = zip.as_deref().map(pico8::load).transpose()?;
    // A started card being updated keeps its games partition: PICO-8 goes
    // there. Everything else is a card whose first start is still to come
    let into_volume = p8.is_some() && action == install::Action::Update && card.started;
    if into_volume {
        games_volume(&card)?;
    }
    let closed = if device.on_brick_system() {
        let cancelled = || cancel.load(Ordering::Relaxed);
        let cache = cache.ok_or("This computer has no folder for the installer's own files. Nothing was written.")?;
        closed::fetch(
            closed::LIST,
            &cache.join("brick-closed"),
            closed::SDK,
            &closed::http_get,
            &mut |done, total| progress(writer::Progress::Fetching { done, total }),
            &cancelled,
        )?
    } else {
        Vec::new()
    };
    let (path, folder) = source(device, local, progress, cancel)?;
    // Found again after the download, which can take minutes: still there,
    // still the same card
    let result = find(card_id, card_size).and_then(|card| {
        let action = match action {
            install::Action::Update if device.on_brick_system() => install::Action::BrickUpdate,
            a => a,
        };
        let cancelled = || cancel.load(Ordering::Relaxed);
        let rides = match &p8 {
            Some(p) if into_volume => {
                pico8::copy_to(&games_volume(&card)?, p, progress, &cancelled)?;
                None
            }
            Some(_) => zip.as_deref(),
            None => None,
        };
        install::run(card_id, action, &path, card_size, &closed, rides, progress, cancel)
    });
    if let Some(dir) = folder {
        let _ = std::fs::remove_dir_all(dir);
    }
    result
}

/// Add PICO-8: the player's PICO-8 into Bios/PICO-8 on a started card's
/// mounted games partition, and nothing else. No system is written, so no
/// disk is opened and no password asked.
fn add_pico8(
    card_id: &str,
    card_size: u64,
    zip: &Path,
    progress: &mut dyn FnMut(writer::Progress),
    cancel: &AtomicBool,
) -> Result<(), String> {
    let card = find(card_id, card_size)?;
    if card.device.is_none() || !card.started {
        return Err("This card has no ZYPEROS volume for PICO-8 to go in yet. Nothing was written.".into());
    }
    let volume = games_volume(&card)?;
    let p8 = pico8::load(zip)?;
    pico8::copy_to(&volume, &p8, progress, &|| cancel.load(Ordering::Relaxed))
}

/// What start does: a write (update, fresh), or Add PICO-8 alone
#[derive(Clone, Copy, PartialEq)]
enum Job {
    Write(install::Action),
    AddPico8,
}

/// Starts a job; progress and the end arrive as "progress" and "finished"
/// events. `action` is update, fresh or pico8 (Add PICO-8); `size` is the
/// card's size as the player confirmed it; `image` a local card image, or
/// none to download the latest release; `pico8` the player's PICO-8 zip, to
/// go on the card too (Add PICO-8's own).
#[allow(clippy::too_many_arguments)]
#[tauri::command]
fn start(app: AppHandle, running: State<Running>, card: String, size: u64, device: String, action: String,
         image: Option<String>, pico8: Option<String>) -> Result<(), String> {
    let zip = pico8;
    let device = cards::Device::from_id(&device).ok_or("Unknown device")?;
    let job = match (action.as_str(), install::Action::parse(&action)) {
        ("pico8", _) => Job::AddPico8,
        (_, Some(a @ (install::Action::Update | install::Action::Fresh))) => Job::Write(a),
        _ => return Err("Unknown action".into()),
    };
    let local = image.filter(|p| !p.is_empty()).map(PathBuf::from);
    let zip = zip.filter(|p| !p.is_empty()).map(PathBuf::from);
    if job == Job::AddPico8 && zip.is_none() {
        return Err("Choose your PICO-8 zip first.".into());
    }
    let cancel = Arc::new(AtomicBool::new(false));
    {
        let mut slot = running.0.lock().unwrap();
        if slot.is_some() {
            return Err("A card is already being written.".into());
        }
        // The new installer going in place, just before it restarts
        if app.state::<selfupdate::Pending>().installing.load(Ordering::SeqCst) {
            return Err("The installer is updating itself, and opens again in a moment.".into());
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
                writer::Progress::Fetching { done, total } => ("fetching", done, total),
                writer::Progress::Writing { done, total } => ("writing", done, total),
                writer::Progress::Checking { done, total } => ("checking", done, total),
                writer::Progress::Pico8 { done, total } => ("pico8", done, total),
                writer::Progress::Pico8Check { done, total } => ("pico8check", done, total),
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
        // The Brick's closed files are kept between installs in the app's
        // own cache folder, so a second card needs no download; never a
        // shared one
        let cache = app.path().app_cache_dir().ok();
        let result = match (job, &zip) {
            (Job::AddPico8, Some(zip)) => add_pico8(&card, size, zip, &mut progress, &cancel),
            (Job::Write(action), _) => write(&card, size, device, action, local, zip, cache.as_deref(), &mut progress, &cancel),
            (Job::AddPico8, None) => Err("Choose your PICO-8 zip first.".into()),
        };
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
    // Read now: an update on Windows renames this exe before it restarts
    let exe = std::env::current_exe().unwrap_or_default();
    #[cfg(windows)]
    selfupdate::remove_kept_aside(&exe);
    let builder = tauri::Builder::default().plugin(tauri_plugin_dialog::init());
    #[cfg(any(target_os = "macos", windows))]
    let builder = builder.plugin(tauri_plugin_updater::Builder::new().build());
    builder
        .manage(Running::default())
        .manage(selfupdate::Pending::new(exe))
        .setup(|_app| {
            #[cfg(windows)]
            if let Some(Ok(hwnd)) = _app.get_webview_window("main").map(|w| w.hwnd()) {
                autoplay::watch(hwnd.0 as isize);
            }
            #[cfg(all(debug_assertions, any(target_os = "macos", windows)))]
            if let Some(log) = std::env::var_os("ZYPEROS_INSTALLER_SELFUPDATE_TEST") {
                tauri::async_runtime::spawn(selfupdate_test(_app.handle().clone(), log.into()));
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![list_cards, latest_zyperos, installer_update, download_update,
                                                 restart_to_update, open_installer_releases, check_image, pick_image,
                                                 check_pico8, pick_pico8, start, cancel])
        .run(tauri::generate_context!())
        .expect("ZyperOS Installer could not start");
}

#[cfg(test)]
mod tests {
    use super::cards::Device;

    /// A local image's device is offered beside the release's, in the page's
    /// order: a Brick image to try is offered when the release has the Pixel
    /// 2 only
    #[test]
    fn offers_a_local_images_device_too() {
        use super::with_local;
        assert_eq!(with_local(&[Device::Pixel2], &[]), vec![Device::Pixel2]);
        assert_eq!(with_local(&[Device::Pixel2], &[Device::Brick]), vec![Device::Pixel2, Device::Brick]);
        assert_eq!(with_local(&[], &[Device::Brick]), vec![Device::Brick], "GitHub not answering");
        assert_eq!(with_local(&[Device::Brick, Device::Pixel2], &[Device::Pixel2]), vec![Device::Pixel2, Device::Brick]);
    }

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
