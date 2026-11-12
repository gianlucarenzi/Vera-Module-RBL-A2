# slinky_rom.ld -- $Cn00-$CnFF slot ROM (position independent: linked at $C000,
# it runs in any slot)

MEMORY {
    SLOT_ROM: start = $C000, size = $0100, type = ro, fill = yes, fillval = $FF;
}

SEGMENTS {
    CODE:    load = SLOT_ROM, type = ro;
}
