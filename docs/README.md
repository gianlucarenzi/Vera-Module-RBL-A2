# Reference documents

## In this folder

- `Apple-IIgs-TechNote-28-Card-Guidelines.pdf`: Apple IIgs Technical Note #28,
  "Interface Card Design Guidelines" (October 1987, revised November 1988).
  Apple's only drawing of the maximum card dimensions. It applies to the Apple IIgs
  and to the upgraded Apple IIe, so a card that meets it also fits the Apple IIe.
  The board Edge.Cuts follow the slots 1-3 outline:
  - 7.00" long
  - 2.75" above the slot
  - edge-connector tab front edge 2.950" from the rear edge

  The right side of the scan is cut off. The 2.75" height and the 0.375" between
  the tab and the rear edge were measured on the drawing, and they match
  `con-apple2.lbr`.
  Source: <https://archive.org/download/IIgs_2523028_Card_Guidelines/IIgs_2523028_Card_Guidelines.pdf>
- `con-apple2.lbr`: Eagle library "Apple ][ Expansion Card Templates", whose
  dimensions come from Tech Note #28.
  Source: <https://github.com/freitz85/AppleIISd/blob/master/Hardware/con-apple2.lbr>

## External links (not stored in the repository)

- **Apple IIe Technical Reference Manual** (Addison-Wesley, 1987 edition, 416 pages,
  139 MB PDF). It describes the expansion slots (50-pin edge connectors, 0.10" pitch)
  and the bus signals, but it gives no card dimensions.
  - Page: <https://archive.org/details/apple-iie-technical-reference-manual-1987-edition>
  - PDF: <https://archive.org/download/apple-iie-technical-reference-manual-1987-edition/Apple%20IIe%20Technical%20Reference%20Manual%20(1987%20Edition).pdf>
- **Apple IIe Technical Notes** (#1-#11: DMA, /INH, RDY, paddles, ...). They give no
  card dimensions either.
  <https://archive.retro.co.za/mirrors/apple/ftp.uni-kl.de-19990701/pub/apple2/technotes/apple2e/>
