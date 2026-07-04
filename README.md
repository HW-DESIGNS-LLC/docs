# HW Designs — Product Documentation Site

This repository powers the customer documentation site for the HW Designs
ESP32-S3 development board, published with **GitHub Pages** at:

**https://docs.hwdesigns.us** (once the custom domain is connected)

Customers reach it by scanning the QR code inside the product packaging.

## What's in here

| File / folder      | Purpose                                                                 |
|--------------------|-------------------------------------------------------------------------|
| `index.html`       | The entire website — content **and** styling live in this one file      |
| `assets/logo.svg`  | Company logo shown in the header (placeholder — replace with yours)     |
| `docs/`            | Customer-facing PDFs (schematic, user guide, pinout) — see its README   |
| `README.md`        | This file (not shown on the website)                                    |

## How to edit anything

1. Open the file on GitHub and click the **pencil icon** (top right of the file view).
2. Make your change, then click **Commit changes**.
3. GitHub Pages republishes automatically — refresh the live site after ~1 minute.

All page text is in `index.html`. It's plain HTML: find the words you want to
change and edit them. The colors and fonts are defined once at the top of the
file in the `:root { ... }` block if you ever want to match new brand colors.

## Launch checklist

Search `index.html` for **`TODO`** — every item below has a matching comment
marking the exact spot.

- [ ] Replace `assets/logo.svg` with your real logo (same filename = no code change)
- [ ] Confirm the product name in the hero (`<h1>`) and the page `<title>`
- [ ] Upload `schematic.pdf`, `user-guide.pdf`, and `pinout.pdf` into `docs/`
- [ ] Verify steps 1–2 match the shipping firmware (demo behavior, hotspot name)
- [ ] Verify the RGB LED GPIO number in the step-4 sketch matches your board
- [ ] Add the battery connector type + polarity note in the Support FAQ
- [ ] Point the "Demo firmware source" link and the three example "View source"
      buttons at your public firmware repository (it will live under
      `https://github.com/HW-DESIGNS-LLC/...`)
- [ ] Make sure `support@hwdesigns.us` exists (or change the address)
- [ ] Confirm the render filenames in `index.html` match your uploads
      (`assets/top_view.jpg` and `assets/bottom_view.jpg`)
- [ ] Verify the board-tour details: Qwiic connector location, battery
      connector type, display-ribbon note, and the board code on the back
- [ ] Test-scan the packaging QR code on a phone **after** HTTPS is live

## How publishing works

- This repository lives in the **HW-DESIGNS-LLC** organization. Until the
  custom domain connects, the site is also reachable at
  `https://hw-designs-llc.github.io/docs/`.
- GitHub Pages serves this repository from the `main` branch
  (**Settings → Pages** controls this).
- Every commit republishes the site automatically within ~1 minute.
- The custom domain (`docs.hwdesigns.us`) is set under **Settings → Pages →
  Custom domain**; GitHub stores it in a `CNAME` file in this repo — leave
  that file alone once it appears.
- This repository is public (required for free GitHub Pages), so only put
  files here that customers are meant to see.
