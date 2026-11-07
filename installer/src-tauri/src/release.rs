// What to put on the card: the latest ZyperOS release from GitHub, the
// Brick's or the Pixel's card image out of it, checked against the release's
// SHA256SUMS before anything is written; or an image file the player chose,
// checked against a SHA256SUMS beside it when there is one. And whether a
// newer installer is out.
//
// The installer lives in ZyperOS's repository (installer/) and has ZyperOS's
// version: every release, tagged v<version>, carries both the card images
// and the installer's apps (tools/release.sh and .github/workflows/
// release.yml make it). The newest published one is found from the list of
// releases: a draft or a pre-release never counts, as GitHub's "latest"
// never names one, so a beta stays out of reach until it is published.
// ZyperOS has no release yet: until it does, the screen offers an image file
// instead.

use std::io::{Read, Write};
use std::path::{Path, PathBuf};

use sha2::{Digest, Sha256};

use crate::writer::Progress;

/// Where the releases are: each has ZyperOS-v<version>-<device>.img.xz (from
/// plastron's release build), ZyperOS-Installer-<version>-<system> files,
/// and SHA256SUMS over all of them.
const REPO: &str = "tarikbc/ZyperOS";
/// The installer's files in a release start so
const INSTALLER_FILES: &str = "ZyperOS-Installer-";
pub const INSTALLER_RELEASES: &str = "https://github.com/tarikbc/ZyperOS/releases/latest";
const SUMS: &str = "SHA256SUMS";

#[derive(serde::Deserialize, Clone, Debug)]
pub struct Asset {
    pub name: String,
    pub size: u64,
    browser_download_url: String,
}

#[derive(serde::Deserialize, Clone, Debug)]
pub struct Release {
    tag_name: String,
    assets: Vec<Asset>,
    #[serde(default)]
    html_url: String,
    #[serde(default)]
    draft: bool,
    #[serde(default)]
    prerelease: bool,
}

impl Release {
    /// "0.1.0", from the tag "v0.1.0"
    pub fn version(&self) -> &str {
        self.tag_name.trim_start_matches('v')
    }

    /// Whether it is a published version: tagged v and a number, neither a
    /// draft nor a pre-release
    fn published(&self) -> bool {
        let rest = self.tag_name.strip_prefix('v');
        !self.draft && !self.prerelease && rest.is_some_and(|r| r.starts_with(|c: char| c.is_ascii_digit()))
    }

    /// Whether it carries the installer's apps
    fn has_installer(&self) -> bool {
        self.assets.iter().any(|a| a.name.starts_with(INSTALLER_FILES))
    }

    /// The device's card image: ZyperOS-v0.1.0-pixel2.img.xz or
    /// ZyperOS-v0.1.0-brick.img.xz
    pub fn asset(&self, brick: bool) -> Result<&Asset, String> {
        let want = format!("ZyperOS-v{}-{}.img.xz", self.version(), if brick { "brick" } else { "pixel2" });
        self.assets
            .iter()
            .find(|a| a.name == want)
            .ok_or_else(|| format!("ZyperOS {} has no {want} to download.", self.version()))
    }
}

fn agent() -> ureq::Agent {
    ureq::Agent::config_builder()
        .user_agent(concat!("zyperos-installer/", env!("CARGO_PKG_VERSION")))
        .timeout_connect(Some(std::time::Duration::from_secs(15)))
        .build()
        .into()
}

fn get(url: &str) -> Result<ureq::http::Response<ureq::Body>, ureq::Error> {
    agent().get(url).header("Accept", "application/vnd.github+json").call()
}

/// The releases API, or another server for the tests: ZYPEROS_INSTALLER_API
/// names a base like http://127.0.0.1:8000, which answers
/// /repos/<owner>/<name>/releases as GitHub does. Debug builds only.
fn api() -> String {
    #[cfg(debug_assertions)]
    if let Ok(base) = std::env::var("ZYPEROS_INSTALLER_API") {
        return base.trim_end_matches('/').to_string();
    }
    "https://api.github.com".into()
}

/// The repository's published releases, newest first.
fn published() -> Result<Vec<Release>, ureq::Error> {
    let mut response = get(&format!("{}/repos/{REPO}/releases?per_page=50", api()))?;
    let text = response.body_mut().read_to_string()?;
    let all: Vec<Release> = serde_json::from_str(&text).map_err(|e| ureq::Error::Io(std::io::Error::other(e)))?;
    Ok(all.into_iter().filter(Release::published).collect())
}

/// What the screen says when there is no release to download yet
pub const NO_RELEASE: &str = "ZyperOS has no release to download yet. Choose an image file instead.";

fn unreachable(e: ureq::Error) -> String {
    match e {
        ureq::Error::StatusCode(404) => NO_RELEASE.into(),
        ureq::Error::StatusCode(403) | ureq::Error::StatusCode(429) => {
            "GitHub is asking for a break: too many checks from this network for now. Try again in a while.".into()
        }
        ureq::Error::StatusCode(code) => format!("GitHub answered {code}. Try again in a while."),
        e => format!("Couldn't reach GitHub, where ZyperOS is downloaded from: {e}"),
    }
}

/// ZyperOS's latest release.
pub fn latest() -> Result<Release, String> {
    published().map_err(unreachable)?.into_iter().next().ok_or_else(|| NO_RELEASE.to_string())
}

/// A newer installer's version, if one is out. Says nothing when GitHub
/// can't be asked: this is only ever a notice.
pub fn newer_installer() -> Option<String> {
    let release = published().ok()?.into_iter().find(Release::has_installer)?;
    // Only a release of this repository counts, by its page's address, as
    // GitHub answers a renamed repository's old name too. Its names ignore
    // case, so the comparison does as well
    let page = release.html_url.to_ascii_lowercase();
    if !page.starts_with(&format!("https://github.com/{}/", REPO.to_ascii_lowercase())) {
        return None;
    }
    let theirs = release.version().to_string();
    newer(&theirs, env!("CARGO_PKG_VERSION")).then_some(theirs)
}

/// Whether version `a` is after `b`: numbers compared part by part, so 1.10
/// is after 1.9
fn newer(a: &str, b: &str) -> bool {
    let parts = |v: &str| -> Vec<u64> { v.split('.').map(|p| p.parse().unwrap_or(0)).collect() };
    let (a, b) = (parts(a), parts(b));
    for i in 0..a.len().max(b.len()) {
        let (x, y) = (a.get(i).copied().unwrap_or(0), b.get(i).copied().unwrap_or(0));
        if x != y {
            return x > y;
        }
    }
    false
}

/// The checksum SHA256SUMS gives for `name`
fn sum_for(sums: &str, name: &str) -> Option<String> {
    sums.lines().find_map(|line| {
        let (sum, file) = line.split_once(char::is_whitespace)?;
        // "*name" is sha256sum's binary mode
        (file.trim().trim_start_matches('*') == name).then(|| sum.to_ascii_lowercase())
    })
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

/// Downloads the asset into `dir` and checks it. The file is the caller's to
/// remove when it is done with it.
pub fn download(
    release: &Release,
    asset: &Asset,
    dir: &Path,
    progress: &mut dyn FnMut(Progress),
    cancelled: &dyn Fn() -> bool,
) -> Result<PathBuf, String> {
    let sums_asset = release
        .assets
        .iter()
        .find(|a| a.name == SUMS)
        .ok_or_else(|| format!("ZyperOS {} has no checksums to check the download against.", release.version()))?;
    let sums = get(&sums_asset.browser_download_url)
        .and_then(|mut r| r.body_mut().read_to_string())
        .map_err(unreachable)?;
    let want = sum_for(&sums, &asset.name)
        .ok_or_else(|| format!("ZyperOS {}'s checksums don't name {}.", release.version(), asset.name))?;

    std::fs::create_dir_all(dir).map_err(|e| format!("Making {}: {e}", dir.display()))?;
    let path = dir.join(&asset.name);
    let result = (|| {
        let response = get(&asset.browser_download_url).map_err(unreachable)?;
        let mut body = response.into_body().into_reader();
        let mut file = std::fs::File::create(&path).map_err(|e| format!("Saving the download: {e}"))?;
        let mut hash = Sha256::new();
        let mut buf = vec![0u8; 256 * 1024];
        let mut done = 0u64;
        loop {
            if cancelled() {
                return Err("cancelled".to_string());
            }
            let n = body.read(&mut buf).map_err(|e| format!("Downloading {}: {e}", asset.name))?;
            if n == 0 {
                break;
            }
            hash.update(&buf[..n]);
            file.write_all(&buf[..n]).map_err(|e| format!("Saving the download: {e}"))?;
            done += n as u64;
            progress(Progress::Downloading { done, total: asset.size.max(done) });
        }
        file.sync_all().map_err(|e| format!("Saving the download: {e}"))?;
        if done != asset.size {
            return Err(format!("The download ended early, at {done} of {} bytes. Try again.", asset.size));
        }
        if hex(&hash.finalize()) != want {
            return Err(format!("{} didn't match its checksum, so it wasn't used. Try again.", asset.name));
        }
        Ok(())
    })();
    match result {
        Ok(()) => Ok(path),
        Err(e) => {
            let _ = std::fs::remove_file(&path);
            Err(e)
        }
    }
}

/// The checksum a SHA256SUMS beside a local image gives for it, if there is
/// such a file and it names the image: what plastron's `make release` writes
/// next to the images in out/release.
pub fn local_sum(image: &Path) -> Option<String> {
    let name = image.file_name()?.to_str()?;
    let sums = std::fs::read_to_string(image.with_file_name(SUMS)).ok()?;
    sum_for(&sums, name)
}

/// A local image checked against `want`, its SHA256SUMS line, reading the
/// whole file once.
pub fn check_local(
    image: &Path,
    want: &str,
    progress: &mut dyn FnMut(Progress),
    cancelled: &dyn Fn() -> bool,
) -> Result<(), String> {
    let mut file = std::fs::File::open(image).map_err(|e| format!("Opening the image: {e}"))?;
    let total = file.metadata().map(|m| m.len()).unwrap_or(0);
    let mut hash = Sha256::new();
    let mut buf = vec![0u8; 1 << 20];
    let mut done = 0u64;
    progress(Progress::Verifying { done, total });
    loop {
        if cancelled() {
            return Err("cancelled".into());
        }
        let n = file.read(&mut buf).map_err(|e| format!("Reading the image: {e}"))?;
        if n == 0 {
            break;
        }
        hash.update(&buf[..n]);
        done += n as u64;
        progress(Progress::Verifying { done, total });
    }
    if hex(&hash.finalize()) != want {
        let name = image.file_name().unwrap_or_default().to_string_lossy();
        return Err(format!(
            "{name} doesn't match its line in the SHA256SUMS beside it, so it wasn't written. \
             The file may be damaged: download it again."
        ));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn release(tag: &str, names: &[&str]) -> Release {
        Release {
            tag_name: tag.into(),
            assets: names
                .iter()
                .map(|n| Asset { name: n.to_string(), size: 1, browser_download_url: String::new() })
                .collect(),
            html_url: String::new(),
            draft: false,
            prerelease: false,
        }
    }

    #[test]
    fn counts_only_published_versions() {
        let v = release("v0.2.0", &["ZyperOS-Installer-0.2.0-macOS.zip"]);
        assert!(v.published() && v.has_installer());
        assert_eq!(v.version(), "0.2.0");
        let beta = Release { prerelease: true, ..release("v0.3.0", &[]) };
        let draft = Release { draft: true, ..release("v0.3.0", &[]) };
        assert!(!beta.published() && !draft.published(), "never a pre-release or a draft");
        assert!(!release("vNext", &[]).published(), "a version, not any tag starting with v");
        assert!(!release("installer-v1.0.0", &[]).published(), "the old installer tags are not versions");
        assert!(!release("v0.1.0", &["ZyperOS-v0.1.0-pixel2.img.xz"]).has_installer());
    }

    #[test]
    fn picks_each_devices_file() {
        // What plastron's make release puts in out/release
        let r = release(
            "v0.1.0",
            &["SHA256SUMS", "ZyperOS-v0.1.0-pixel2-sources.tar", "ZyperOS-v0.1.0-pixel2.img.xz",
              "ZyperOS-v0.1.0-brick-sources.tar", "ZyperOS-v0.1.0-brick.img.xz"],
        );
        assert_eq!(r.version(), "0.1.0");
        assert_eq!(r.asset(true).unwrap().name, "ZyperOS-v0.1.0-brick.img.xz");
        assert_eq!(r.asset(false).unwrap().name, "ZyperOS-v0.1.0-pixel2.img.xz");
        assert!(release("v0.2.0", &["ZyperOS-v0.2.0-pixel2.img.xz"]).asset(true).is_err());
    }

    #[test]
    fn no_release_yet_says_so() {
        assert_eq!(unreachable(ureq::Error::StatusCode(404)), NO_RELEASE);
    }

    #[test]
    fn compares_versions() {
        assert!(newer("1.10.0", "1.9.0"));
        assert!(newer("1.0.1", "1.0"));
        assert!(!newer("1.0.0", "1.0.0"));
        assert!(!newer("0.9", "1.0.0"));
    }

    #[test]
    fn reads_the_checksums() {
        let sums = "007a44e2  ZyperOS-v0.1.0-brick.img.xz\n084b6541  ZyperOS-v0.1.0-pixel2.img.xz\nABCDEF *other.bin\n";
        assert_eq!(sum_for(sums, "ZyperOS-v0.1.0-brick.img.xz").as_deref(), Some("007a44e2"));
        assert_eq!(sum_for(sums, "ZyperOS-v0.1.0-pixel2.img.xz").as_deref(), Some("084b6541"));
        assert_eq!(sum_for(sums, "other.bin").as_deref(), Some("abcdef"));
        assert_eq!(sum_for(sums, "ZyperOS-v0.1.0"), None);
    }

    #[test]
    fn checks_a_local_image_against_the_sums_beside_it() {
        let dir = std::env::temp_dir().join(format!("zyperos-installer-sums-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let img = dir.join("ZyperOS-v0.1.0-pixel2.img.xz");
        std::fs::write(&img, b"not really an image").unwrap();
        assert_eq!(local_sum(&img), None, "no SHA256SUMS yet");
        let good = hex(&Sha256::digest(b"not really an image"));
        std::fs::write(dir.join("SHA256SUMS"), format!("{good}  ZyperOS-v0.1.0-pixel2.img.xz\n")).unwrap();
        let want = local_sum(&img).unwrap();
        assert_eq!(want, good);
        let mut seen = 0;
        check_local(&img, &want, &mut |_| seen += 1, &|| false).unwrap();
        assert!(seen > 0);
        std::fs::write(&img, b"not really an image, changed").unwrap();
        assert!(check_local(&img, &want, &mut |_| {}, &|| false).unwrap_err().contains("doesn't match"));
        std::fs::remove_dir_all(&dir).unwrap();
    }

    /// A server on this computer answering as GitHub does: the releases list,
    /// SHA256SUMS and the image, at most `requests` requests (an empty image:
    /// no releases at all). Returns its address.
    fn fake_github(image: Vec<u8>, sums: String, requests: usize) -> String {
        use std::io::{BufRead, BufReader};
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        let base = format!("http://{}", listener.local_addr().unwrap());
        let api = base.clone();
        std::thread::spawn(move || {
            for stream in listener.incoming().take(requests) {
                let mut stream = stream.unwrap();
                let mut reader = BufReader::new(stream.try_clone().unwrap());
                let mut line = String::new();
                reader.read_line(&mut line).unwrap();
                let path = line.split_whitespace().nth(1).unwrap_or("").to_string();
                loop {
                    let mut h = String::new();
                    if reader.read_line(&mut h).unwrap() <= 2 {
                        break;
                    }
                }
                // Newest first, as GitHub lists them: a draft and a beta
                // before the published one
                let json = format!(
                    r#"[{{"tag_name":"v0.3.0","draft":true,"html_url":"","assets":[]}},
                        {{"tag_name":"v0.2.0","prerelease":true,"html_url":"","assets":[]}},
                        {{"tag_name":"v0.1.0","html_url":"https://github.com/tarikbc/ZyperOS/releases/tag/v0.1.0",
                        "assets":[{{"name":"SHA256SUMS","size":{},"browser_download_url":"{api}/dl/SHA256SUMS"}},
                                  {{"name":"ZyperOS-v0.1.0-pixel2.img.xz","size":{},"browser_download_url":"{api}/dl/img"}}]}}]"#,
                    sums.len(),
                    image.len()
                );
                let (status, body): (&str, Vec<u8>) = match path.as_str() {
                    "/repos/tarikbc/ZyperOS/releases?per_page=50" if !image.is_empty() => ("200 OK", json.into_bytes()),
                    "/repos/tarikbc/ZyperOS/releases?per_page=50" => ("200 OK", b"[]".to_vec()),
                    "/dl/SHA256SUMS" => ("200 OK", sums.clone().into_bytes()),
                    "/dl/img" => ("200 OK", image.clone()),
                    _ => ("404 Not Found", b"{}".to_vec()),
                };
                let head = format!("HTTP/1.1 {status}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n", body.len());
                stream.write_all(head.as_bytes()).unwrap();
                stream.write_all(&body).unwrap();
            }
        });
        base
    }

    /// The download path end to end, against fake_github: the release found,
    /// the image downloaded and checked, and a damaged one refused and
    /// removed. Uses ZYPEROS_INSTALLER_API, which only debug builds read.
    #[test]
    fn downloads_and_checks_a_release() {
        let image = b"a card image, as far as this test cares".to_vec();
        let good = hex(&Sha256::digest(&image));
        let dir = std::env::temp_dir().join(format!("zyperos-installer-dl-{}", std::process::id()));

        let base = fake_github(image.clone(), format!("{good}  ZyperOS-v0.1.0-pixel2.img.xz\n"), 3);
        std::env::set_var("ZYPEROS_INSTALLER_API", &base);
        let r = latest().unwrap();
        assert_eq!(r.version(), "0.1.0");
        let mut last = None;
        let path = download(&r, r.asset(false).unwrap(), &dir, &mut |p| last = Some(p), &|| false).unwrap();
        assert_eq!(std::fs::read(&path).unwrap(), image);
        assert_eq!(last, Some(Progress::Downloading { done: image.len() as u64, total: image.len() as u64 }));
        assert!(r.asset(true).is_err(), "no Brick image in this release");

        // The same release with a checksum that doesn't match
        let base = fake_github(image.clone(), format!("{}  ZyperOS-v0.1.0-pixel2.img.xz\n", "0".repeat(64)), 3);
        std::env::set_var("ZYPEROS_INSTALLER_API", &base);
        let r = latest().unwrap();
        let e = download(&r, r.asset(false).unwrap(), &dir, &mut |_| {}, &|| false).unwrap_err();
        assert!(e.contains("didn't match its checksum"), "{e}");
        assert!(!dir.join("ZyperOS-v0.1.0-pixel2.img.xz").exists(), "a bad download is removed");

        // No release at all yet, and a repository that isn't there
        let base = fake_github(Vec::new(), String::new(), 1);
        std::env::set_var("ZYPEROS_INSTALLER_API", &base);
        assert_eq!(latest().unwrap_err(), NO_RELEASE);
        let base = fake_github(Vec::new(), String::new(), 1);
        std::env::set_var("ZYPEROS_INSTALLER_API", base + "/nothing");
        assert_eq!(latest().unwrap_err(), NO_RELEASE);
        std::env::remove_var("ZYPEROS_INSTALLER_API");
        let _ = std::fs::remove_dir_all(&dir);
    }

    /// The real thing, over the network: ZyperOS's latest release, both files
    /// downloaded and checked. Fails while ZyperOS has no release.
    #[test]
    #[ignore]
    fn downloads_the_latest() {
        let r = latest().unwrap();
        let dir = std::env::temp_dir().join("zyperos-installer-test");
        for brick in [true, false] {
            let asset = r.asset(brick).unwrap();
            let started = std::time::Instant::now();
            let path = download(&r, asset, &dir, &mut |_| {}, &|| false).unwrap();
            println!("{} {} bytes in {:?}", asset.name, asset.size, started.elapsed());
            std::fs::remove_file(path).unwrap();
        }
        println!("newer installer: {:?}", newer_installer());
    }
}
