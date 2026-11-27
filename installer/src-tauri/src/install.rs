// Putting ZyperOS on a card: the steps between the button and the writer.
// Both devices' ZyperOS is a card image, written whole for a fresh install or
// its system part for an update (writer.rs).
//
// On macOS and Windows the card is opened here (device.rs). On Linux the
// window isn't root, so the same work runs in a helper the installer starts
// through pkexec: this program again, with `--write` and no window, printing
// its progress a line at a time for the window to read.

use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};

use crate::writer::Progress;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Action {
    /// The Pixel 2's update
    Update,
    BrickUpdate,
    /// Either device's fresh install: the image says which table it has
    Fresh,
}

impl Action {
    pub fn parse(s: &str) -> Option<Action> {
        match s {
            "update" => Some(Action::Update),
            "brick-update" => Some(Action::BrickUpdate),
            "fresh" => Some(Action::Fresh),
            _ => None,
        }
    }
    #[cfg_attr(not(target_os = "linux"), allow(dead_code))]
    fn word(self) -> &'static str {
        match self {
            Action::Update => "update",
            Action::BrickUpdate => "brick-update",
            Action::Fresh => "fresh",
        }
    }
}

/// Where an update starts, its bootloader (the table before it is the card's
/// own and stays), and the partition it ends with, the root: 32 KB on the
/// Pixel 2, 128 KB on the Brick, whose state partition after the root stays.
const PIXEL_SKIP: u64 = 32 * 1024;
const BRICK_SKIP: u64 = 128 * 1024;
const ROOT: usize = 2;

/// The write itself, with the card opened here. What the helper runs, and
/// what macOS and Windows run in the window's process. `source` is the card
/// image; `card_bytes` the card's size, which a GPT is fitted to; `closed`
/// the Brick's closed files (closed.rs), fetched and checked already, which
/// go into the image's boot partition on its way to the card (none for the
/// Pixel 2); `pico8` the player's PICO-8 zip, for a card that never started,
/// whose PICO-8 rides along in the same boot partition (pico8.rs); `panel`
/// the R36S's panel, picked by its board, which goes in that partition's
/// r36s-boot.txt (r36s.rs; None for every other device, and for an R36S
/// left to the card's own default).
#[allow(clippy::too_many_arguments)]
pub fn write_here(
    card_id: &str,
    action: Action,
    source: &Path,
    card_bytes: u64,
    closed: &[PathBuf],
    pico8: Option<&Path>,
    panel: Option<&str>,
    progress: &mut dyn FnMut(Progress),
    cancel: &AtomicBool,
) -> Result<(), String> {
    let cancelled = || cancel.load(Ordering::Relaxed);
    if !crate::cards::is_card(card_id, card_bytes) {
        return Err("That card isn't there anymore, or it changed. Choose it again.".into());
    }
    let len = crate::image::size(source).map_err(|e| format!("Reading the image: {e}"))?;
    let plain = || crate::image::open(source);
    // Checked again here, which is root on Linux: exactly these bytes go on
    // the card
    let checked = crate::closed::load_checked(crate::closed::LIST, closed)?;
    let p8 = pico8.map(crate::pico8::load).transpose()?;
    let mut folders: Vec<(&str, &crate::closed::Files)> = Vec::new();
    if !checked.is_empty() {
        folders.push((crate::closed::FOLDER, &checked));
    }
    if let Some(p) = &p8 {
        folders.push((crate::pico8::FOLDER, &p.files));
    }
    let over = if folders.is_empty() && panel.is_none() {
        None
    } else {
        if p8.is_some() {
            progress(Progress::Pico8 { done: 0, total: 2 });
        }
        let (at, mut boot) = crate::closed::boot_of(&plain)?;
        if !folders.is_empty() {
            boot = crate::closed::into_boot(boot, &folders)?;
        }
        if p8.is_some() {
            progress(Progress::Pico8 { done: 2, total: 2 });
        }
        // The R36S's panel in its r36s-boot.txt, read back from the FAT
        // before the card is opened; the writer then compares the card with
        // these bytes, as with every other
        if let Some(panel) = panel {
            boot = crate::r36s::into_boot(boot, panel)?;
        }
        Some((at, std::sync::Arc::new(boot)))
    };
    let open = || -> std::io::Result<Box<dyn std::io::Read + Send>> {
        let image = crate::image::open(source)?;
        Ok(match &over {
            Some((at, boot)) => Box::new(crate::closed::Overlay::new(image, *at, boot.clone())),
            None => image,
        })
    };
    let mut card = crate::device::open(card_id)?;
    let result = match action {
        Action::Update => crate::writer::update(&mut card, &open, len, PIXEL_SKIP, ROOT, progress, &cancelled).map(|_| ()),
        Action::BrickUpdate => crate::writer::update(&mut card, &open, len, BRICK_SKIP, ROOT, progress, &cancelled).map(|_| ()),
        Action::Fresh => crate::writer::fresh(&mut card, &open, len, card_bytes, progress, &cancelled),
    };
    crate::device::close(card, card_id);
    result
}

/// The write, wherever it has to run: here, or on Linux in the helper.
#[allow(clippy::too_many_arguments)]
pub fn run(
    card_id: &str,
    action: Action,
    source: &Path,
    card_bytes: u64,
    closed: &[PathBuf],
    pico8: Option<&Path>,
    panel: Option<&str>,
    progress: &mut dyn FnMut(Progress),
    cancel: &AtomicBool,
) -> Result<(), String> {
    #[cfg(target_os = "linux")]
    return run_helper(card_id, action, source, card_bytes, closed, pico8, panel, progress, cancel);
    #[cfg(not(target_os = "linux"))]
    return write_here(card_id, action, source, card_bytes, closed, pico8, panel, progress, cancel);
}

/// No PICO-8 zip, in the helper's arguments; and no panel
#[cfg_attr(not(target_os = "linux"), allow(dead_code))]
const NO_PICO8: &str = "-";
#[cfg_attr(not(target_os = "linux"), allow(dead_code))]
const NO_PANEL: &str = "-";

/// Linux: pkexec runs this program again as root with `--write`, which asks
/// for the password through the desktop's own prompt.
#[cfg(target_os = "linux")]
#[allow(clippy::too_many_arguments)]
fn run_helper(
    card_id: &str,
    action: Action,
    source: &Path,
    card_bytes: u64,
    closed: &[PathBuf],
    pico8: Option<&Path>,
    panel: Option<&str>,
    progress: &mut dyn FnMut(Progress),
    cancel: &AtomicBool,
) -> Result<(), String> {
    use std::io::{BufRead, BufReader};
    use std::process::{Command, Stdio};
    let me = std::env::current_exe().map_err(|e| format!("Finding the installer: {e}"))?;
    let mut child = Command::new("pkexec")
        .arg(&me)
        .args(["--write", action.word(), card_id])
        .arg(source)
        .arg(card_bytes.to_string())
        .arg(pico8.map_or_else(|| std::ffi::OsString::from(NO_PICO8), |p| p.as_os_str().to_owned()))
        .arg(panel.unwrap_or(NO_PANEL))
        .args(closed)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|e| format!("Couldn't run pkexec, which asks for the password: {e}"))?;
    let out = child.stdout.take().expect("piped");
    let mut last = None;
    for line in BufReader::new(out).lines().map_while(Result::ok) {
        if cancel.load(Ordering::Relaxed) {
            let _ = child.kill();
            let _ = child.wait();
            return Err("cancelled".into());
        }
        match parse_line(&line) {
            Some(Line::Progress(p)) => progress(p),
            Some(Line::Done(r)) => last = Some(r),
            None => {}
        }
    }
    let status = child.wait().map_err(|e| format!("The helper: {e}"))?;
    match (last, status.code()) {
        (Some(r), _) => r,
        // pkexec's own: 126 the password was dismissed, 127 not authorized
        (None, Some(126)) | (None, Some(127)) => {
            Err("The card wasn't opened: the password was cancelled or not accepted.".into())
        }
        (None, _) => Err("The helper stopped without saying why.".into()),
    }
}

#[cfg_attr(not(target_os = "linux"), allow(dead_code))]
/// The helper's lines: `pico8 DONE TOTAL` while PICO-8 goes into the
/// image's boot partition, `writing DONE TOTAL`, `checking DONE TOTAL`, then
/// `ok` or `error MESSAGE`.
enum Line {
    Progress(Progress),
    Done(Result<(), String>),
}

#[cfg_attr(not(target_os = "linux"), allow(dead_code))]
fn parse_line(line: &str) -> Option<Line> {
    let mut f = line.splitn(3, ' ');
    let word = f.next()?;
    let num = |s: Option<&str>| s.and_then(|x| x.parse::<u64>().ok());
    match word {
        "writing" | "checking" | "pico8" => {
            let (done, total) = (num(f.next())?, num(f.next())?);
            Some(Line::Progress(match word {
                "writing" => Progress::Writing { done, total },
                "checking" => Progress::Checking { done, total },
                _ => Progress::Pico8 { done, total },
            }))
        }
        "ok" => Some(Line::Done(Ok(()))),
        "error" => Some(Line::Done(Err(line["error ".len().min(line.len())..].to_string()))),
        _ => None,
    }
}

/// The helper: `--write update|brick-update|fresh CARD SOURCE CARD_BYTES
/// PICO8 PANEL [CLOSED...]`, run as root on Linux; PICO8 the player's PICO-8
/// zip for a card that never started, or `-`, PANEL the R36S's panel for its
/// r36s-boot.txt (one of r36s::PANELS), or `-`, and CLOSED the Brick's closed
/// files the window fetched and checked. It checks them again itself.
/// Prints progress at most every few hundred milliseconds.
pub fn helper(args: &[String]) -> i32 {
    let usage = "usage: zyperos-installer --write update|brick-update|fresh CARD SOURCE CARD_BYTES PICO8|- PANEL|- [CLOSED...]";
    let (Some(action), Some(card), Some(source), Some(card_bytes), Some(pico8), Some(panel)) = (
        args.first().and_then(|a| Action::parse(a)),
        args.get(1),
        args.get(2).map(PathBuf::from),
        args.get(3).and_then(|n| n.parse::<u64>().ok()),
        args.get(4),
        args.get(5).map(String::as_str),
    ) else {
        eprintln!("{usage}");
        return 2;
    };
    let pico8 = (pico8 != NO_PICO8).then(|| PathBuf::from(pico8));
    let panel = (panel != NO_PANEL).then_some(panel);
    if panel.is_some_and(|p| !crate::r36s::PANELS.contains(&p)) {
        eprintln!("{usage}");
        return 2;
    }
    let closed: Vec<PathBuf> = args.iter().skip(6).map(PathBuf::from).collect();
    let mut last = std::time::Instant::now();
    let mut last_word = "";
    let mut progress = |p: Progress| {
        let (word, done, total) = match p {
            // The window downloads, and checks a local image, before the
            // helper starts, and copies PICO-8 to a started card itself
            Progress::Downloading { .. }
            | Progress::Verifying { .. }
            | Progress::Fetching { .. }
            | Progress::Pico8Check { .. } => return,
            Progress::Pico8 { done, total } => ("pico8", done, total),
            Progress::Writing { done, total } => ("writing", done, total),
            Progress::Checking { done, total } => ("checking", done, total),
        };
        if word != last_word || done == total || last.elapsed().as_millis() >= 200 {
            println!("{word} {done} {total}");
            last = std::time::Instant::now();
            last_word = word;
        }
    };
    let never = AtomicBool::new(false);
    match write_here(card, action, &source, card_bytes, &closed, pico8.as_deref(), panel, &mut progress, &never) {
        Ok(()) => {
            println!("ok");
            0
        }
        Err(e) => {
            println!("error {e}");
            1
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_the_helpers_lines() {
        assert!(matches!(parse_line("writing 10 20"), Some(Line::Progress(Progress::Writing { done: 10, total: 20 }))));
        assert!(matches!(parse_line("checking 20 20"), Some(Line::Progress(Progress::Checking { done: 20, total: 20 }))));
        assert!(matches!(parse_line("pico8 0 2"), Some(Line::Progress(Progress::Pico8 { done: 0, total: 2 }))));
        assert!(matches!(parse_line("ok"), Some(Line::Done(Ok(())))));
        match parse_line("error This card's partitions aren't where") {
            Some(Line::Done(Err(e))) => assert_eq!(e, "This card's partitions aren't where"),
            _ => panic!(),
        }
        assert!(parse_line("something else").is_none());
        assert_eq!(Action::parse("fresh").map(Action::word), Some("fresh"));
        assert_eq!(Action::parse("brick-update").map(Action::word), Some("brick-update"));
    }
}
