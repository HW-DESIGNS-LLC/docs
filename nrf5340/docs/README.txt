Customer-facing files for the nRF5340 BLE Dev Board page. The links on the
board page point to these EXACT filenames - keep the names if you ever
replace a file:

  schematic.pdf     - full board schematic (export from EasyEDA Pro)   [TO UPLOAD]
  user-guide.pdf    - setup, features & troubleshooting guide          [TO UPLOAD]
  pinout.pdf        - printable pin reference diagram                  [TO UPLOAD]
  3D.step           - 3D model for enclosure/mechanical design         [TO UPLOAD]

When pinout.pdf is uploaded, also render page 1 as ../assets/pinout.png for
the "Know your board" section of the board page - if pinout.pdf is ever
updated, re-render that PNG so the two stay in sync.

DVT source documents (already uploaded - the DVT report at ../dvt/ presents
these, and the download links on both ../index.html and ../dvt/ point to them
by these EXACT names):

  DVT_Plan_nRF5340_Board_RevB.docx  - the Rev B design-verification test plan (v2.0, 101 tests)
  nRF5340_RevB_DVT_Tracker.xlsx     - per-measurement results tracker + issue log (blank until the run)
  DVT_Firmware_Guide.docx           - how to flash the probe & the nRF5340 and run each shell test
  DVT_Helper_nRF5340.zip            - the Zephyr shell bring-up & self-test firmware (source)

  Keep these filenames if you replace them: they are also referenced by name
  inside the documents themselves, and the tracker/plan share test IDs.

How to upload on GitHub: open this folder in the repository, click
"Add file" -> "Upload files", drag the files in, then "Commit changes".
The site republishes automatically in about a minute.
