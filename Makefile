PYTHON ?= python3
CA65 ?= $(if $(wildcard .tools/cc65/usr/bin/ca65),.tools/cc65/usr/bin/ca65,ca65)
LD65 ?= $(if $(wildcard .tools/cc65/usr/bin/ld65),.tools/cc65/usr/bin/ld65,ld65)
DA65 ?= $(if $(wildcard .tools/cc65/usr/bin/da65),.tools/cc65/usr/bin/da65,da65)
ROM ?= rom/original.nes
MANIFEST := local/rom.json
BANKS := $(wildcard src/banks/*.s)

.PHONY: all identify extract verify progress test
all: verify progress

identify:
	$(PYTHON) tools/rom.py identify "$(ROM)" --manifest $(MANIFEST)

extract:
	$(PYTHON) tools/rom.py extract $(MANIFEST) build/extracted
	$(PYTHON) tools/disassemble.py --da65 $(DA65)

# Always reread and hash the owner ROM; no stale outputs can pass verification.
build/game.nes: extract $(BANKS) src/header.s config/mmc1.cfg
	$(CA65) -g -o build/header.o src/header.s
	@for bank in $(BANKS); do $(CA65) -I . -g -o "build/$$(basename $$bank .s).o" "$$bank" || exit $$?; done
	$(LD65) -C config/mmc1.cfg -m build/game.map -Ln build/game.lbl -o $@ build/header.o $(patsubst src/banks/%.s,build/%.o,$(BANKS))

verify: build/game.nes
	$(PYTHON) tools/rom.py verify $(MANIFEST) build/game.nes

progress:
	$(PYTHON) tools/progress.py

test:
	$(PYTHON) -m unittest discover -s tests -v
