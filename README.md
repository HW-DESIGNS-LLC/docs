# HW Designs — Product Documentation Site

This repository powers the customer documentation site, published with
**GitHub Pages** at **https://docs.hwdesigns.us**.

Customers reach it by scanning the QR code inside each product's packaging.
Every board's QR points at that board's own page, so the codes stay valid
forever no matter how many products are added.

## Structure

| Path                          | URL                                   | Purpose                                  |
|-------------------------------|---------------------------------------|------------------------------------------|
| `index.html`                  | `docs.hwdesigns.us/`                  | Hub — lists every board                   |
| `assets/site.css`             | —                                     | **Shared theme** — colors & fonts defined once here, used by every page |
| `assets/site.js`              | —                                     | Small shared script: highlights the nav tab for the section on screen, and opens collapsed sections before printing |
| `assets/logo-white.svg`       | —                                     | White company logo shown in the black header of every page |
| `assets/logo.svg`             | —                                     | Original black company logo (kept for reference) |
| `assets/favicon.svg`          | —                                     | Browser-tab icon (gold gear on black)     |
| `assets/fonts/`               | —                                     | Nunito and Nunito Sans font files — stored here so pages never contact Google |
| `esp32-s3/index.html`         | `docs.hwdesigns.us/esp32-s3/`         | Crescent City board page (QR target)       |
| `esp32-s3/specs/index.html`   | `docs.hwdesigns.us/esp32-s3/specs/`   | V2.0 spec sheet (printable; `docs/spec-sheet.pdf` is printed from it) |
| `esp32-s3/esphome/`           | `docs.hwdesigns.us/esp32-s3/esphome/` | ESPHome / Home Assistant page and the browser-installer firmware |
| `esp32-s3/dvt/index.html`     | `docs.hwdesigns.us/esp32-s3/dvt/`     | V1.0 test report (results, power deep-dive, issues, test plan) |
| `esp32-s3/dvt/v2/index.html`  | `docs.hwdesigns.us/esp32-s3/dvt/v2/`  | V2.0 test report (in progress: battery life, charging, low-battery shutdown) |
| `esp32-s3/power-profile.html` | *(redirects to `…/esp32-s3/dvt/`)*    | Legacy link — keep so old bookmarks still work |
| `esp32-s3/assets/`            | —                                     | That board's images + DVT chart PNGs      |
| `esp32-s3/docs/`              | —                                     | That board's downloads: PDFs, 3D model, BOM, battery test data (CSV), DVT firmware guide and helper sketch |
| `esp32-s3/firmware/`          | —                                     | Public home of the board's example sketches and the ESPHome configuration (the board page's "View source" buttons point here) |
| `sitemap.xml`, `robots.txt`   | —                                     | Search-engine sitemap and crawler rules   |
| `LICENSE.md`                  | —                                     | Licenses: MIT for firmware and code, CC BY-SA 4.0 for the documentation |
| `CNAME`                       | —                                     | Custom-domain file GitHub manages — leave it alone |

Each future board repeats the `esp32-s3/` pattern under its own folder.

## How to edit anything

**With GitHub Desktop (recommended):** edit the files in your local clone,
then in Desktop review the change, **Commit to main**, and **Push origin**.
The site republishes automatically — refresh after ~1 minute.

**On the website:** open the file on GitHub, click the **pencil icon**,
make the change, **Commit changes**.

Colors and fonts for the whole site live in one place: the `:root { ... }`
block at the top of **`assets/site.css`**. Change a value there and every
page follows. Page files contain only content.

The theme matches the company website (hwdesigns.us): black header with a
gold line, gold buttons, copper as the hardware accent, Nunito fonts, and
automatic light/dark mode that follows the visitor's device.

## Product photos (versioned)

Each board page (e.g. `esp32-s3/index.html`) loads its photos through
`IMG_VERSION` (set near the top of the file). To post new photos: save them
as `top_view_V4.jpg` / `bottom_view_V4.jpg` in that board's `assets/`
folder, then bump that page's `IMG_VERSION` to `"V4"`.

> **Also update the hub:** each board card in the root `index.html`
> references the versioned filename directly (e.g.
> `esp32-s3/assets/top_view_V3.jpg`) —
> point it at the new file when you bump a version.

## Adding a new board

1. **Pick a short URL slug** (e.g. `oled-breakout`). It becomes the permanent
   address `docs.hwdesigns.us/oled-breakout/` — never change it after the
   QR code is printed.
2. **Create the page**: copy `esp32-s3/index.html` to
   `oled-breakout/index.html` in your local clone and rewrite the content.
   The copy already links the shared stylesheet (`../assets/site.css`), so
   the new page matches the site automatically.
3. **Add its files**: images into `oled-breakout/assets/`, PDFs and the
   3D model into `oled-breakout/docs/`.
4. **Add it to the hub**: in the root `index.html`, copy the existing
   `<a class="board-card">` block and update the link, image path, name,
   description, and tags.
5. **Commit and push** in GitHub Desktop, then check the live page.
6. **Generate its QR code** pointing at
   `https://docs.hwdesigns.us/oled-breakout/` and test-scan the live page
   before sending packaging to print.

## The test report (DVT) pages

- `esp32-s3/dvt/index.html` — **V1.0 test report** (120 of 122 tests
  passed). V1.0 testing is complete, so this page is a finished record; edit
  it by hand only to correct it.
- `esp32-s3/dvt/v2/index.html` — **V2.0 test report** (in progress),
  generated from the V2.0 DVT tracker. Don't edit it by hand: rebuild it from
  the tracker whenever results change, then copy it here.

Internal-only material is excluded by design; findings are described
neutrally.

## Launch checklist (ESP32-S3)

Open:

- [ ] Finish V2.0 testing and regenerate the V2.0 test report
      (`esp32-s3/dvt/v2/`) as tests close
- [ ] Make sure `support@hwdesigns.us` exists (or change it — it appears on
      both the hub and the board page)
- [ ] Test-scan the packaging QR code on a phone **after** the live page works

Done:

- [x] Upload `user-guide.pdf` and `pinout.pdf` into `esp32-s3/docs/`
      (`schematic.pdf` and `3D.step` are already there)
- [x] Confirm the product name in the hero (`<h1>`) and the page `<title>`
      ("Crescent City")
- [x] Verify steps 1–2 match the shipping firmware (Weather Clock 2.1.6:
      demo behavior, hotspot name)
- [x] Verify the RGB LED GPIO number in the step-4 sketch matches the board
      (GPIO48, confirmed by the pinout reference PIN-01)
- [x] Verify the board-tour details: Qwiic connector location, battery
      connector type (JST-PH), display-ribbon note, and the board code on
      the back (all confirmed against the user guide UG-01 and pinout PIN-01)
- [x] Point the "Demo firmware source" link and the three example "View source"
      buttons at the public firmware home (`esp32-s3/firmware/` in this repo)
- [x] After the RF tests wrap, refresh the V1.0 DVT report so the pending items
      close (done 2026-07-22 — report reconciled to DVT plan v1.2, 122 tests /
      0 pending)

## How publishing works

- This repository lives in the **HW-DESIGNS-LLC** organization. The site is
  also reachable at `https://hw-designs-llc.github.io/docs/`.
- GitHub Pages serves the `main` branch (**Settings → Pages** controls this).
- Every commit republishes the site within ~1 minute.
- This repository is public (required for free GitHub Pages), so only put
  files here that customers are meant to see.
