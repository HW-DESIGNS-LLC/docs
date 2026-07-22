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
| `assets/site.js`              | —                                     | Small shared script: highlights the nav tab for the section on screen |
| `assets/logo.svg`             | —                                     | Company logo, shared by all pages         |
| `esp32-s3/index.html`         | `docs.hwdesigns.us/esp32-s3/`         | ESP32-S3 Dev Board page (QR target)       |
| `esp32-s3/dvt/index.html`     | `docs.hwdesigns.us/esp32-s3/dvt/`     | Public DVT report (results, power deep-dive, issues, test plan) |
| `esp32-s3/power-profile.html` | *(redirects to `…/esp32-s3/dvt/`)*    | Legacy link — keep so old bookmarks still work |
| `esp32-s3/assets/`            | —                                     | That board's images + DVT chart PNGs      |
| `esp32-s3/docs/`              | —                                     | That board's PDFs and 3D model            |
| `esp32-s3/firmware/`          | —                                     | Public home of the board's example sketches (the board page's "View source" buttons point here) |
| `CNAME`                       | —                                     | Custom-domain file GitHub manages — leave it alone |

Each future board repeats the `esp32-s3/` pattern under its own folder.

> **In progress:** the **nRF5340 BLE Dev Board** (`nrf5340/`) is being
> prepared on the **`In_Development`** branch. It stays off `main` — and
> off the live site — until it's ready, then merges in at launch.

## How to edit anything

**With GitHub Desktop (recommended):** edit the files in your local clone,
then in Desktop review the change, **Commit to main**, and **Push origin**.
The site republishes automatically — refresh after ~1 minute.

**On the website:** open the file on GitHub, click the **pencil icon**,
make the change, **Commit changes**.

Colors and fonts for the whole site live in one place: the `:root { ... }`
block at the top of **`assets/site.css`**. Change a value there and every
page follows. Page files contain only content.

## Product photos (versioned)

Each board page (e.g. `esp32-s3/index.html`) loads its photos through
`IMG_VERSION` (set near the top of the file). To post new photos: save them
as `top_view_V3.jpg` / `bottom_view_V3.jpg` in that board's `assets/`
folder, then bump that page's `IMG_VERSION` to `"V3"`.

> **Also update the hub:** each board card in the root `index.html`
> references the versioned filename directly (e.g.
> `esp32-s3/assets/top_view_V2.jpg`) —
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

## The DVT report pages

`esp32-s3/dvt/index.html` is a **generated snapshot** built from the DVT
tracker spreadsheet, the DVT plan document, and the PWR-02 power capture.
When the tracker changes (e.g. the RF tests finish), regenerate the page
from the updated files rather than hand-editing the numbers.
Internal-only material (supplier discussions) is excluded by design —
findings are described neutrally, e.g. "wrong part was populated."

The nRF5340 DVT page follows the same rule — see the `In_Development`
branch, where that board lives until launch.

## Launch checklist (ESP32-S3)

Search `esp32-s3/index.html` for **`TODO`** — every item below has a matching
comment marking the exact spot.

- [x] Upload `user-guide.pdf` and `pinout.pdf` into `esp32-s3/docs/`
      (`schematic.pdf` and `3D.step` are already there)
- [ ] Confirm the product name in the hero (`<h1>`) and the page `<title>`
- [ ] Verify steps 1–2 match the shipping firmware (demo behavior, hotspot name)
- [x] Verify the RGB LED GPIO number in the step-4 sketch matches the board
      (GPIO48, confirmed by the pinout reference PIN-01)
- [x] Verify the board-tour details: Qwiic connector location, battery
      connector type (JST-XH), display-ribbon note, and the board code on
      the back (all confirmed against the user guide UG-01 and pinout PIN-01)
- [x] Point the "Demo firmware source" link and the three example "View source"
      buttons at the public firmware home (`esp32-s3/firmware/` in this repo —
      upload the example sketches there when ready)
- [ ] Make sure `support@hwdesigns.us` exists (or change it — it appears on
      both the hub and the board page)
- [x] After the RF tests wrap, refresh the DVT report so the pending items close
      (done 2026-07-22 — report reconciled to DVT plan v1.2, 122 tests / 0 pending)
- [ ] Test-scan the packaging QR code on a phone **after** the live page works

## Launch checklist (nRF5340)

Lives in this file **on the `In_Development` branch**, alongside the
board's pages — it merges back here when the board launches.

## How publishing works

- This repository lives in the **HW-DESIGNS-LLC** organization. The site is
  also reachable at `https://hw-designs-llc.github.io/docs/`.
- GitHub Pages serves the `main` branch (**Settings → Pages** controls this).
- Every commit republishes the site within ~1 minute.
- This repository is public (required for free GitHub Pages), so only put
  files here that customers are meant to see.
