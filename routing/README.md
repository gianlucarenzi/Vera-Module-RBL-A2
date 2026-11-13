# Risultato dello sbroglio da zero (13/11/2026)

Prodotto da `tools/route_nolimits.sh` (fastroute 0.1.14, senza limiti di
tempo) sul piazzamento di `VERA-MODULE-RBL.kicad_pcb` di questo commit.

- `final.ses`: la sessione da importare in pcbnew 5 (File → Importa →
  Sessione Specctra), poi `B` (riempimento aree) e DRC.
- `board_pv.dsn`: il DSN usato (piani GND/3V3 e via verso i piani), da
  passare agli script di `tools/` che lavorano su `final.ses`.
- `report.json`: statistiche e collegamenti non sbrogliati dello stadio 2;
  `bus.json` dello stadio 1, `report_power.json` del tentativo su POWER.
- `*.log`, `route_nolimits.out`, `repair.out`: i log dei vari passi.

Esito:

- 604 collegamenti, **6 non sbrogliati**, 0 violazioni di clearance
  (misura di fastroute; il riferimento resta il DRC di KiCad):
  3V3 U7.46–U7.55 (piazzole senza spazio per il via al piano),
  mA4 U2.34–via, ~mVIRQ U26.2–U2.32, SPI_SSEL_N U9.6–U2.16,
  Net-(R71-Pad2) R71.2–U7.8, mPHI0 U6.17–U7.6;
- il piano POWER non è servito (sempre 6 mancanti): niente piste su POWER;
- 518 via, nessuna pista sotto 0,200 mm;
- coppia USB: accoppiata 13,24 mm su 39,77 mm, differenza di lunghezza
  0,05 mm (tolleranza 0,10 mm);
- 64 curve a 90° smussate, 30 senza spazio da sistemare a mano (elenco in
  `route_nolimits.out`); 3 sono sulla coppia USB vicino al connettore.
