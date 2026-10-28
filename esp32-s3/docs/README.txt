Customer-facing files for the Crescent City board page. The links on the board page
point to these EXACT filenames - keep the names if you ever replace a file:

  schematic.pdf     - full board schematic, V2.0
  user-guide.pdf    - setup, features & troubleshooting guide (UG-01 Rev D, board V2.0)
  pinout.pdf        - printable pin reference diagram (PIN-01 Rev D, board V2.0)
  3D.step           - 3D model for enclosure/mechanical design (V2.0)
  bom.csv           - bill of materials (V2.0): parts and manufacturer part numbers

All five are uploaded. The pinout is also rendered as ../assets/pinout.png for
the "Know your board" section of the board page - if pinout.pdf is ever
updated, re-render that PNG from page 1 so the two stay in sync.

Spec sheet and test data (linked from ../index.html and ../specs/):

  spec-sheet.pdf                       - printable spec sheet (printed from ../specs/)
  battery_full_discharge_revA_10s.csv  - V1.0 (Rev A) full battery discharge, 10-second averages
  battery_full_charge_revA_10s.csv     - V1.0 (Rev A) full charge from empty, 10-second averages
  battery_full_discharge_revB_10s.csv  - V2.0 (Rev B) full discharge, Weather Clock 2.1, 10-second averages
  battery_full_charge_revB_10s.csv     - V2.0 (Rev B) full charge from empty, 10-second averages

DVT firmware (linked from ../index.html and ../dvt/):

  DVT_Firmware_Guide.docx  - how to flash & run the DVT firmware pack (for the V1.0 test IDs)
  DVT_Helper.ino           - the serial-menu DVT/bring-up firmware sketch

How to upload on GitHub: open this folder in the repository, click
"Add file" -> "Upload files", drag the files in, then "Commit changes".
The site republishes automatically in about a minute.
