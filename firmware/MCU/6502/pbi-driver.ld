# pbi_driver.cfg Configuration for ROM PBI @ $D800

MEMORY {
    PBI_ROM: start = $D800, size = $1000, type = ro, fill = yes;
}

SEGMENTS {
    CODE:    load = PBI_ROM, type = ro;
    RODATA:  load = PBI_ROM, type = ro;
}
