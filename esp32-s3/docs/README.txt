Customer-facing files for the ESP32-S3 board page. The links on the board page
point to these EXACT filenames - keep the names if you ever replace a file:

  schematic.pdf     - full board schematic (export from EasyEDA Pro)
  user-guide.pdf    - setup, features & troubleshooting guide (UG-01 Rev A)
  pinout.pdf        - printable pin reference diagram (PIN-01 Rev A)
  3D.step           - 3D model for enclosure/mechanical design

All four are uploaded. The pinout is also rendered as ../assets/pinout.png for
the "Know your board" section of the board page - if pinout.pdf is ever
updated, re-render that PNG from page 1 so the two stay in sync.

DVT source documents (already uploaded - the DVT report at ../dvt/ renders
these, and the download links on both ../index.html and ../dvt/ point to them
by these EXACT names):

  DVT_Plan_ESP32S3_Board_RevA.docx  - the Rev A design-verification test plan (v1.2)
  DVT_Firmware_Guide.docx           - how to flash & run the DVT firmware pack
  ESP32_RevA_DVT_Tracker.xlsx       - executed per-measurement results + issue log
  DVT_Helper.ino                    - the serial-menu DVT/bring-up firmware sketch

  Keep these filenames if you replace them: ESP32_RevA_DVT_Tracker.xlsx and
  DVT_Helper.ino are also referenced by name inside the documents themselves.

How to upload on GitHub: open this folder in the repository, click
"Add file" -> "Upload files", drag the files in, then "Commit changes".
The site republishes automatically in about a minute.
