// The R36S's board, picked on the page: which panel its card drives.
//
// R36S units come with different panels, and no firmware found tells which
// one a unit has (ZyperOS's docs/devices/r36s.md, "Picking a panel on the
// card"). The card's boot partition (R36SBOOT) holds an overlay for each
// known panel, overlays/panel-NAME.dtbo, and the line zy_panel=NAME in its
// r36s-boot.txt picks one (plastron's board/r36s/boot-prelude.cmd). With no
// line the card drives panel 4. The player picks the name printed on the
// board, under the back cover, as dArkOSen-R36S has them pick it, and the
// installer writes the line for that board's panel into the image's boot
// partition on its way to the card, in memory as the Brick's closed files go
// (closed.rs): no card is mounted. The writer then reads the whole card back
// and compares it, that line included.

use serde::Serialize;
use std::io::{self, Cursor, Read, Seek, SeekFrom, Write};

/// The file the boot script reads, at the boot partition's top
pub const BOOT_TXT: &str = "r36s-boot.txt";

/// The panels the card has an overlay for (plastron's
/// board/r36s/overlays/README says where each comes from): the only values
/// zy_panel= is ever given here
pub const PANELS: [&str; 6] = ["0", "1", "2", "3", "4", "4v22"];

/// The card's own panel when r36s-boot.txt names none (boot-prelude.cmd's
/// zy_panel_default): the page's "Not sure", checked against the card's
/// boot script in the tests
#[cfg_attr(not(test), allow(dead_code))]
pub const DEFAULT_PANEL: &str = "4";

#[derive(Serialize, Clone, Copy, Debug, PartialEq)]
pub struct Board {
    /// The page's value for it
    pub id: &'static str,
    /// What the page shows: the name printed on the board
    pub name: &'static str,
    /// Where the page lists it: "common" (first), "other", or "panel" for a
    /// panel picked by its own name
    pub group: &'static str,
    /// The zy_panel= value it gets, or None for the card's own default,
    /// with nothing written
    pub panel: Option<&'static str>,
}

const fn board(id: &'static str, name: &'static str, group: &'static str, panel: &'static str) -> Board {
    Board { id, name, group, panel: Some(panel) }
}

/// The default: nothing written, so the card drives DEFAULT_PANEL, panel 4
pub const NOT_SURE: &str = "not-sure";

/// The boards, in the page's order: the default first, then the most common
/// boards (V12, V21, V22, V30), the rest, and each panel by its own name.
///
/// Each board's panel is the overlay whose init sequence its stock device
/// tree has, command by command (the waits aside): its panel-init-sequence
/// as plastron/scripts/r36s-panel.py reads it, against the I lines of
/// plastron's board/r36s/overlays/panel-*.dts. The trees are dArkOSen-R36S's
/// (https://github.com/djparentx/dArkOSen-R36S at
/// 28f5427f7ae953ee6e813618705baf569fef0f27, boot/dtb/r36s/<board>/
/// rk3326-r36s-linux.dtb, rk3326-r36h-linux.dtb for the HL-R36H), its
/// "r36s" variant in boot/dtb/r36_devices.ini: the genuine boards. Read as
/// data, nothing copied from them. Left out: its clones (variant "clone"
/// and "soysauce", which need their own system), R36S-Plus-V20 (a 720x720
/// panel no overlay drives), and its other handhelds (HL-R40PRO-MAX,
/// HL-R45H, HL-R46H, HL-R50H, HL-R50S).
pub const BOARDS: [Board; 18] = [
    Board { id: NOT_SURE, name: "Not sure", group: "common", panel: None },
    // R36S-V12 2023-08-18 Panel 4: panel 4, as on Sylver's unit, the board
    // ZyperOS's R36S card was first started on (docs/devices/r36s.md)
    board("v12-panel4", "R36S-V12, panel 4", "common", "4"),
    // R36S-V12 2023-08-18 Panel 0: panel 0, the ST7703
    board("v12-panel0", "R36S-V12, panel 0", "common", "0"),
    // R36S-V21 2024-12-18 2503, 2529, 2544, 2550, 2551, 2552, 2602 and 2604:
    // panel 4v22 (8 of its 10 trees)
    board("v21", "R36S-V21", "common", "4v22"),
    // R36S-V21 2024-12-18 2537 and 2547: panel 4
    board("v21-2537", "R36S-V21 2537 or 2547", "common", "4"),
    // R36S-V22 2024-12-18, and its 2534, 2535, 2545 and 2548: panel 4v22
    board("v22", "R36S-V22", "common", "4v22"),
    // R36S-V30 2025-18-11 2603: panel 4v22
    board("v30", "R36S-V30", "common", "4v22"),
    // HL-R36H-V20 2024-05-18: panel 4
    board("hl-r36h-v20", "HL-R36H-V20", "other", "4"),
    // HL-R36H-V21 2024-11-18: panel 4v22
    board("hl-r36h-v21", "HL-R36H-V21", "other", "4v22"),
    // R36S-Y02 2024-12-18: none exactly. Panel 0 is the nearest: the same
    // ST7703 commands and timings, with two values of its own (B6, its VCOM,
    // 82 82 for 7F 7F; and C1's power settings)
    board("y02", "R36S-Y02", "other", "0"),
    // R36XX-V21 2024-12-18: panel 4v22
    board("r36xx-v21", "R36XX-V21", "other", "4v22"),
    // RK3326-D3: panel 4
    board("rk3326-d3", "RK3326-D3", "other", "4"),
    // Each overlay by its own name, for a player who knows the panel, or is
    // trying them in r36s-boot.txt's order (0, 4v22, 2, 3, 1). Where each
    // one's init sequence comes from: overlays/README (lcdyk0517/arkos4clone
    // at 94d5431f, boot/dArkOS/consoles/)
    board("panel-0", "Panel 0", "panel", "0"),
    board("panel-1", "Panel 1", "panel", "1"),
    board("panel-2", "Panel 2", "panel", "2"),
    board("panel-3", "Panel 3", "panel", "3"),
    board("panel-4", "Panel 4", "panel", "4"),
    board("panel-4v22", "Panel 4v22", "panel", "4v22"),
];

/// A board by the page's value for it
pub fn find(id: &str) -> Option<&'static Board> {
    BOARDS.iter().find(|b| b.id == id)
}

/// Whether a line sets the panel: zy_panel= at its start, not after a #
fn sets_panel(line: &str) -> bool {
    line.trim_start().starts_with("zy_panel=")
}

/// The panel r36s-boot.txt picks, as U-Boot's env import reads it: the last
/// zy_panel= line not commented out, or None
pub fn panel_in(text: &str) -> Option<&str> {
    text.lines()
        .rfind(|l| sets_panel(l))
        .map(|l| l.trim_start()["zy_panel=".len()..].trim())
}

/// r36s-boot.txt picking `panel`: its own zy_panel= lines taken out, and
/// one with `panel` put after its commented-out example (#zy_panel=), where
/// its help says to set it, or at its end. Everything else as it was, the
/// file's own line ends kept.
pub fn with_panel(text: &str, panel: &str) -> String {
    let mut out = String::with_capacity(text.len() + 32);
    let mut placed = false;
    for line in text.split_inclusive('\n') {
        let bare = line.trim_end_matches(['\n', '\r']);
        if sets_panel(bare) {
            continue;
        }
        out.push_str(line);
        if !placed && bare.trim_start().starts_with("#zy_panel=") {
            if !line.ends_with('\n') {
                out.push('\n');
            }
            let end = if line.ends_with("\r\n") { "\r\n" } else { "\n" };
            out.push_str(&format!("zy_panel={panel}{end}"));
            placed = true;
        }
    }
    if !placed {
        if !out.is_empty() && !out.ends_with('\n') {
            out.push('\n');
        }
        out.push_str(&format!("zy_panel={panel}\n"));
    }
    out
}

fn fat(e: impl std::fmt::Display) -> String {
    format!("Reading the image's boot partition: {e}")
}

/// r36s-boot.txt from a boot partition's FAT (its whole bytes; fatfs opens
/// only what it may write, so a copy)
pub fn boot_txt(boot: &[u8]) -> Result<String, String> {
    let mut disk = Cursor::new(boot.to_vec());
    let fs = fatfs::FileSystem::new(&mut disk, fatfs::FsOptions::new()).map_err(fat)?;
    let mut file = fs
        .root_dir()
        .open_file(BOOT_TXT)
        .map_err(|_| format!("This image's boot partition has no {BOOT_TXT}: it isn't the R36S's. Nothing was written."))?;
    let mut text = String::new();
    file.read_to_string(&mut text).map_err(fat)?;
    Ok(text)
}

/// A boot partition's FAT (its whole bytes) with r36s-boot.txt picking
/// `panel`, then read back from the FAT written: refused, with nothing
/// written, when the image has no overlay for that panel, no r36s-boot.txt,
/// or the file doesn't read back as written.
pub fn into_boot(boot: Vec<u8>, panel: &str) -> Result<Vec<u8>, String> {
    if !PANELS.contains(&panel) {
        return Err(format!("There is no panel {panel}. Nothing was written."));
    }
    let want = with_panel(&boot_txt(&boot)?, panel);
    let mut disk = Cursor::new(boot);
    {
        let fs = fatfs::FileSystem::new(&mut disk, fatfs::FsOptions::new()).map_err(fat)?;
        {
            let root = fs.root_dir();
            let overlay = format!("overlays/panel-{panel}.dtbo");
            root.open_file(&overlay)
                .map_err(|_| format!("This image has no {overlay} for panel {panel}. Nothing was written."))?;
            let put = |e: io::Error| format!("Putting the panel in {BOOT_TXT}: {e}");
            let mut file = root.open_file(BOOT_TXT).map_err(put)?;
            file.seek(SeekFrom::Start(0)).map_err(put)?;
            file.truncate().map_err(put)?;
            file.write_all(want.as_bytes()).map_err(put)?;
            file.flush().map_err(put)?;
        }
        fs.unmount().map_err(|e| format!("Finishing the boot partition: {e}"))?;
    }
    let boot = disk.into_inner();
    // Read back from the FAT just written, before it goes on the card
    let back = boot_txt(&boot)?;
    if back != want || panel_in(&back) != Some(panel) {
        return Err(format!("{BOOT_TXT} didn't read back as written. Nothing was written."));
    }
    Ok(boot)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The card's own r36s-boot.txt, as plastron's build puts it on R36SBOOT
    const CARDS: &str = include_str!("../../../plastron/buildroot/board/r36s/r36s-boot.txt");

    #[test]
    fn each_board_has_a_panel_the_card_has() {
        let mut ids = std::collections::HashSet::new();
        for b in BOARDS {
            assert!(ids.insert(b.id), "{} twice", b.id);
            assert!(matches!(b.group, "common" | "other" | "panel"), "{}", b.id);
            if let Some(p) = b.panel {
                assert!(PANELS.contains(&p), "{}: panel {p}", b.id);
            }
        }
        // The default first, and it is the card's own
        assert_eq!(BOARDS[0].id, NOT_SURE);
        assert_eq!(BOARDS[0].panel, None);
        // The common boards before the rest, and the panels last
        let groups: Vec<&str> = BOARDS.iter().map(|b| b.group).collect();
        let mut sorted = groups.clone();
        sorted.sort_by_key(|g| ["common", "other", "panel"].iter().position(|x| x == g));
        assert_eq!(groups, sorted);
        // Every overlay the card has can be picked by its name
        for p in PANELS {
            assert!(BOARDS.iter().any(|b| b.group == "panel" && b.panel == Some(p)), "panel {p}");
        }
        // And every overlay the build puts on the card is one of PANELS
        let cfg = include_str!("../../../plastron/buildroot/board/r36s/genimage.cfg");
        let built: Vec<&str> = cfg
            .split("file overlays/panel-")
            .skip(1)
            .map(|s| s.split(".dtbo").next().unwrap())
            .collect();
        assert_eq!(built, PANELS.to_vec());
        // The card's default is panel 4, as boot-prelude.cmd says
        let prelude = include_str!("../../../plastron/buildroot/board/r36s/boot-prelude.cmd");
        assert!(prelude.contains(&format!("setenv zy_panel_default {DEFAULT_PANEL}\n")));
    }

    #[test]
    fn maps_the_boards_as_their_stock_trees_say() {
        let panel = |id: &str| find(id).unwrap().panel;
        assert_eq!(panel(NOT_SURE), None);
        assert_eq!(panel("v12-panel4"), Some("4"));
        assert_eq!(panel("v12-panel0"), Some("0"));
        assert_eq!(panel("v21"), Some("4v22"));
        assert_eq!(panel("v21-2537"), Some("4"));
        assert_eq!(panel("v22"), Some("4v22"));
        assert_eq!(panel("v30"), Some("4v22"));
        assert_eq!(panel("hl-r36h-v20"), Some("4"));
        assert_eq!(panel("hl-r36h-v21"), Some("4v22"));
        assert_eq!(panel("y02"), Some("0"));
        assert_eq!(panel("r36xx-v21"), Some("4v22"));
        assert_eq!(panel("rk3326-d3"), Some("4"));
        assert_eq!(panel("panel-1"), Some("1"));
        assert_eq!(panel("panel-4v22"), Some("4v22"));
        assert!(find("g80ca").is_none() && find("").is_none());
    }

    /// The page's demo (a plain browser, no app) lists the same boards
    #[test]
    fn the_pages_demo_has_the_same_boards() {
        let js = include_str!("../../ui/app.js");
        let start = js.find("/* r36s-boards */").expect("the demo's boards") + "/* r36s-boards */".len();
        let end = js.find("/* /r36s-boards */").expect("their end");
        let demo: serde_json::Value = serde_json::from_str(&js[start..end]).unwrap();
        assert_eq!(demo, serde_json::to_value(BOARDS).unwrap());
    }

    #[test]
    fn writes_the_panel_where_the_help_says() {
        assert_eq!(panel_in(CARDS), None, "the card as built picks none");
        let text = with_panel(CARDS, "4v22");
        assert_eq!(panel_in(&text), Some("4v22"));
        // One line more, right under the example, and nothing else changed
        let lines: Vec<&str> = text.lines().collect();
        let at = lines.iter().position(|l| *l == "#zy_panel=0").unwrap();
        assert_eq!(lines[at + 1], "zy_panel=4v22");
        let mut without = lines.clone();
        without.remove(at + 1);
        assert_eq!(without, CARDS.lines().collect::<Vec<_>>());
        // Picked again: the line replaced, never two
        let again = with_panel(&text, "0");
        assert_eq!(panel_in(&again), Some("0"));
        assert_eq!(again.matches("\nzy_panel=").count(), 1);
        assert_eq!(again, with_panel(CARDS, "0"));
    }

    #[test]
    fn writes_the_panel_in_any_file() {
        assert_eq!(with_panel("", "2"), "zy_panel=2\n");
        assert_eq!(with_panel("zy_dtb=x.dtb", "2"), "zy_dtb=x.dtb\nzy_panel=2\n");
        assert_eq!(with_panel("zy_panel=1\nzy_audio=SPK\n", "3"), "zy_audio=SPK\nzy_panel=3\n");
        assert_eq!(with_panel("# a\r\n#zy_panel=0\r\nzy_panel=1\r\n", "4"), "# a\r\n#zy_panel=0\r\nzy_panel=4\r\n");
        assert_eq!(with_panel("#zy_panel=0", "4"), "#zy_panel=0\nzy_panel=4\n");
        assert_eq!(panel_in("zy_panel=1\n  zy_panel=4v22 \n#zy_panel=0\n"), Some("4v22"), "the last one, as env import");
        assert_eq!(panel_in("#zy_panel=1\n # zy_panel=2\n"), None);
    }

    /// A FAT boot partition as the R36S's image has it: r36s-boot.txt at the
    /// top and the panels' overlays in overlays/
    pub(crate) fn r36s_boot(panels: &[&str]) -> Vec<u8> {
        let mut disk = Cursor::new(vec![0u8; 8 << 20]);
        fatfs::format_volume(&mut disk, fatfs::FormatVolumeOptions::new().volume_label(*b"R36SBOOT   ")).unwrap();
        {
            let fs = fatfs::FileSystem::new(&mut disk, fatfs::FsOptions::new()).unwrap();
            let root = fs.root_dir();
            root.create_file(BOOT_TXT).unwrap().write_all(CARDS.as_bytes()).unwrap();
            let dir = root.create_dir("overlays").unwrap();
            for p in panels {
                dir.create_file(&format!("panel-{p}.dtbo")).unwrap().write_all(p.as_bytes()).unwrap();
            }
            root.create_file("boot.scr").unwrap().write_all(b"script").unwrap();
        }
        disk.into_inner()
    }

    #[test]
    fn writes_the_panel_into_the_boot_partition_and_reads_it_back() {
        let boot = r36s_boot(&PANELS);
        let size = boot.len();
        let out = into_boot(boot, "4v22").unwrap();
        assert_eq!(out.len(), size, "the partition keeps its size");
        let text = boot_txt(&out).unwrap();
        assert_eq!(text, with_panel(CARDS, "4v22"));
        assert_eq!(panel_in(&text), Some("4v22"));
        // The rest of the partition as it was
        let fs = fatfs::FileSystem::new(Cursor::new(out.clone()), fatfs::FsOptions::new()).unwrap();
        let mut s = String::new();
        fs.root_dir().open_file("boot.scr").unwrap().read_to_string(&mut s).unwrap();
        assert_eq!(s, "script");
        s.clear();
        fs.root_dir().open_file("overlays/panel-4v22.dtbo").unwrap().read_to_string(&mut s).unwrap();
        assert_eq!(s, "4v22");
        // Picked again over a card image that has one already: still one line
        drop(fs);
        let twice = into_boot(out, "0").unwrap();
        assert_eq!(boot_txt(&twice).unwrap(), with_panel(CARDS, "0"));
    }

    /// The write's own path (install::write_here): the image's boot
    /// partition read out, the panel put in, and the image as the writer
    /// reads it, twice (writing, then comparing the card), with the new
    /// boot partition in place and the rest the image's own
    #[test]
    fn the_image_goes_on_the_card_with_the_panel() {
        let boot = r36s_boot(&PANELS);
        let mut image = vec![0u8; (1 << 20) + boot.len() + (1 << 20)];
        let e = 446;
        image[e + 4] = 0x0C;
        image[e + 8..e + 12].copy_from_slice(&2048u32.to_le_bytes());
        image[e + 12..e + 16].copy_from_slice(&((boot.len() / 512) as u32).to_le_bytes());
        image[510] = 0x55;
        image[511] = 0xAA;
        image[1 << 20..(1 << 20) + boot.len()].copy_from_slice(&boot);
        let tail = image.len() - 512;
        image[tail..].copy_from_slice(&[0xA5; 512]);
        let img = image.clone();
        let open = move || Ok(Box::new(Cursor::new(img.clone())) as Box<dyn Read + Send>);
        let (at, part) = crate::closed::boot_of(&open).unwrap();
        assert_eq!((at, part.len()), (1 << 20, boot.len()));
        let part = std::sync::Arc::new(into_boot(part, "4v22").unwrap());
        let read = || {
            let mut v = Vec::new();
            crate::closed::Overlay::new(Cursor::new(image.clone()), at, part.clone()).read_to_end(&mut v).unwrap();
            v
        };
        let written = read();
        assert_eq!(written, read(), "the check reads what the write wrote");
        assert_eq!(written[..1 << 20], image[..1 << 20]);
        assert_eq!(written[(1 << 20) + boot.len()..], image[(1 << 20) + boot.len()..]);
        let on_card = boot_txt(&written[1 << 20..(1 << 20) + boot.len()]).unwrap();
        assert_eq!(panel_in(&on_card), Some("4v22"));
        let dir = std::env::temp_dir().join(format!("zyperos-r36s-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        std::fs::write(dir.join("written.img"), &written).unwrap();
        assert_eq!(crate::image::device_of(&dir.join("written.img")).unwrap(), Some(crate::cards::Device::R36s));
        std::fs::remove_dir_all(&dir).unwrap();
    }

    #[test]
    fn refuses_a_panel_the_image_has_no_overlay_for() {
        let e = into_boot(r36s_boot(&["0", "4"]), "4v22").unwrap_err();
        assert!(e.contains("overlays/panel-4v22.dtbo"), "{e}");
        let e = into_boot(r36s_boot(&PANELS), "5").unwrap_err();
        assert!(e.contains("no panel 5"), "{e}");
        // A boot partition that isn't the R36S's
        let mut disk = Cursor::new(vec![0u8; 8 << 20]);
        fatfs::format_volume(&mut disk, fatfs::FormatVolumeOptions::new()).unwrap();
        let e = into_boot(disk.into_inner(), "4").unwrap_err();
        assert!(e.contains("no r36s-boot.txt"), "{e}");
    }
}
