Customer-facing files for the Crescent City board page. The links on the board page
point to these EXACT filenames - keep the names if you ever replace a file:

  schematic.pdf     - full board schematic, V2.0 (export from EasyEDA Pro)
  user-guide.pdf    - setup, features & troubleshooting guide (UG-01 Rev B, board V2.0)
  pinout.pdf        - printable pin reference diagram (PIN-01 Rev B, board V2.0)
  3D.step           - 3D model for enclosure/mechanical design (V2.0)
  bom.csv           - bill of materials (V2.0): parts and manufacturer part numbers

All four are uploaded. The pinout is also rendered as ../assets/pinout.png for
the "Know your board" section of the board page - if pinout.pdf is ever
updated, re-render that PNG from page 1 so the two stay in sync.

Spec sheet and test data (linked from ../index.html and ../specs/):

  spec-sheet.pdf                       - printable spec sheet (printed from ../specs/)
  battery_full_discharge_revA_10s.csv  - full battery discharge, 10-second averages
  battery_full_charge_revA_10s.csv     - full charge from empty, 10-second averages

DVT firmware (linked from ../index.html and ../dvt/):

  DVT_Firmware_Guide.docx  - how to flash & run the DVT firmware pack
  DVT_Helper.ino           - the serial-menu DVT/bring-up firmware sketch

How to upload on GitHub: open this folder in the repository, click
"Add file" -> "Upload files", drag the files in, then "Commit changes".
The site republishes automatically in about a minute.
