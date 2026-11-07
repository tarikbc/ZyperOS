# Trademark policy

ZyperOS is free software under [GPL-3.0](LICENSE). Fork it, change it, sell it.
Build your own system on it. That is the point of the license and no part of this
page takes it back.

The code is the grant. The name, Zyper, and the artwork are not. [NOTICE](NOTICE)
states that as an additional term under GPL-3.0 section 7(e), and this page
explains what it means without the legal register.

## What the marks are

- The name **ZyperOS**, used for a firmware or operating system for handhelds, or
  anything much like one
- The name **ZyperOS Installer**
- **Zyper**, the miniature schnauzer, and his artwork: the sprites, the loading
  animation, the photo, and the boot animation
- The ZyperOS logo, wordmark, and lockup, the installer icons, and the site's
  favicon, app icons, and social preview image
- The website at <https://zyperos.com>

The exact files are listed in [NOTICE](NOTICE). Zyper is my dog, and ZyperOS is
named after him. That is why I ask for his name and his face to stay with this
project.

## You do not need to ask

- Say the name. Review it, blog about it, compare it to your own system, teach it,
  screenshot it, recommend it, or complain about it.
- Say your work derives from ZyperOS. "Based on ZyperOS", "a fork of ZyperOS", and
  "originally by Tarik Caramanico" are all accurate and all welcome.
- Redistribute the official build unchanged, with its name and art intact.
- Quote or reproduce [NOTICE](NOTICE). You are required to keep it anyway.
- Package the official build for a distro or a package manager, as long as it is
  the official build and you do not imply that I maintain your package.

## Rename before you ship

If you modify the code and distribute the result, ship it under your own name and
your own mascot and art. Concretely, please do not:

- Publish a modified build called "ZyperOS", or a name close enough to be mistaken
  for it
- Use Zyper, his artwork, or the ZyperOS logo, wordmark, or lockup in your build
  or its listing
- Suggest that your build is the official one, endorsed by me, or supported by me
- List a modified build in a store, a package index, or a directory under this
  name
- Register a domain, a social handle, or a package name that reads as the
  official channel for ZyperOS

## How to fork cleanly

1. Pick a new name, and your own mascot and art. Do not use Zyper.
2. Replace the branding: the files named in [NOTICE](NOTICE), the boot and
   loading animations, and the strings that carry the product name (the
   `res/strings/` files hold them for all eight languages).
3. Point the website, the update feed, and the issue links at your own
   infrastructure. Do not ship a build that checks my update feed or my site.
4. Keep `LICENSE` and `NOTICE` as they are, and add your own copyright line and
   your modification date below mine in `NOTICE`. GPL-3.0 section 5(a) requires
   the modification statement; section 7(b) requires the attribution.
5. Tell people where the code came from. A link back costs you nothing and is
   the whole social contract of the GPL.

Do that and you have my goodwill, not just my license.

## Getting permission anyway

If you want to use a mark in a way this page does not allow, ask. Open an issue
or reach me through the profile on <https://github.com/tarikbc>. The answer to a
reasonable request is usually yes.

## Enforcement

I would rather send a message than a letter. If something here is being used in a
way that confuses users about who built or supports a build, I will ask for a
rename first and expect that to be the end of it.

---

This policy covers trademarks only. Nothing here restricts any freedom GPL-3.0
grants you in the source code, and nothing here is a further restriction within
the meaning of GPL-3.0 section 10.
