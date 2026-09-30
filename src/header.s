; Preserve the owner's exact iNES header, including reserved bytes.
.segment "HEADER"
.incbin "build/extracted/header.bin"
