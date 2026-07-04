# HW Designs — Product Documentation Site

This repository powers the customer documentation site, published with
**GitHub Pages** at **https://docs.hwdesigns.us**.

Customers reach it by scanning the QR code inside each product's packaging.
Every board's QR points at that board's own page, so the codes stay valid
forever no matter how many products are added.

## Structure

| Path                       | URL                                   | Purpose                                  |
|----------------------------|---------------------------------------|------------------------------------------|
| `index.html`               | `docs.hwdesigns.us/`                  | Hub — lists every board                   |
| `assets/logo.svg`          | —                                     | Company logo, shared by all pages         |
| `esp32-s3/index.html`      | `docs.hwdesigns.us/esp32-s3/`         | ESP32-S3 Dev Board page (QR target)       |
| `esp32-s3/assets/`         | —                                     | That board's images                       |
| `esp32-s3/docs/`           | —                                     | That board's PDFs and 3D model            |
| `CNAME`                    | —                                     | Custom-domain file GitHub manages — leave it alone |

Each future board repeats the `esp32-s3/` pattern under its own folder.

## How to edit anything

1. Open the file on GitHub and click the **pencil icon**.
2. Make your change, then **Commit changes**.
3. The site republishes automatically — refresh after ~1 minute.

Colors and fonts are defined once in the `:root { ... }` block at the top of
each HTML file.

## Adding a new board

1. **Pick a short URL slug** (e.g. `oled-breakout`). It becomes the permanent
   address `docs.hwdesigns.us/oled-breakout/` — never change it after the
   QR code is printed.
2. **Create the page**: open `esp32-s3/index.html`, copy its contents, then in
   the repo choose **Add file → Create new file** and name it
   `oled-breakout/index.html` (typing the `/` creates the folder). Paste and
   rewrite the content for the new board.
3. **Upload its files**: images into `oled-breakout/assets/`, PDFs and the
   3D model into `oled-breakout/docs/` (Add file → Upload files inside those
   folders).
4. **Add it to the hub**: in the root `index.html`, copy the existing
   `<a class="board-card">` block, and update the link, image path, name,
   description, and tags.
5. **Generate its QR code** pointing at
   `https://docs.hwdesigns.us/oled-breakout/` and test-scan the live page
   before sending packaging to print.

## Launch checklist (ESP32-S3)

Search `esp32-s3/index.html` for **`TODO`** — every item below has a matching
comment marking the exact spot.

- [ ] Upload `schematic.pdf`, `user-guide.pdf`, `pinout.pdf`, and `3D.step`
      into `esp32-s3/docs/`
- [ ] Confirm the product name in the hero (`<h1>`) and the page `<title>`
- [ ] Verify steps 1–2 match the shipping firmware (demo behavior, hotspot name)
- [ ] Verify the RGB LED GPIO number in the step-4 sketch matches the board
- [ ] Verify the board-tour details: Qwiic connector location, battery
      connector type, display-ribbon note, and the board code on the back
- [ ] Point the "Demo firmware source" link and the three example "View source"
      buttons at the public firmware repository
      (`https://github.com/HW-DESIGNS-LLC/...`)
- [ ] Make sure `support@hwdesigns.us` exists (or change it — it appears on
      both the hub and the board page)
- [ ] Test-scan the packaging QR code on a phone **after** the live page works

## How publishing works

- This repository lives in the **HW-DESIGNS-LLC** organization. Until the
  custom domain connects, the site is also reachable at
  `https://hw-designs-llc.github.io/docs/`.
- GitHub Pages serves the `main` branch (**Settings → Pages** controls this).
- Every commit republishes the site within ~1 minute.
- This repository is public (required for free GitHub Pages), so only put
  files here that customers are meant to see.
