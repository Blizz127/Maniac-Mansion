# Owner ROM identification

Checked 2026-09-29 (CDT). The owner authorized copying
`Downloads/Maniac Mansion (USA).zip` from their Bazzite PC. Key-only SCP succeeded
via its already-trusted Tailscale IP. No host keys were added. The ZIP and ROM
remain under ignored `rom/`; archive metadata and the pinned identity remain
under ignored `local/`.

| Property | Result |
| --- | --- |
| Region | USA / English / NTSC |
| Container | NES 2.0, 16-byte header |
| Header hex | `4e45531a100012080000700700000001` |
| Mapper | 1 / MMC1; submapper 0 |
| File size | 262,160 bytes |
| PRG ROM | 262,144 bytes, sixteen 16 KiB banks |
| CHR ROM | 0 bytes; 8 KiB CHR RAM declared by NES 2.0 |
| Save memory | Battery flag; 8 KiB PRG NVRAM declared by NES 2.0 |
| Trainer | Absent |

| Scope | CRC32 | SHA-1 | SHA-256 |
| --- | --- | --- | --- |
| Full ROM | `4207c1f9` | `a421c563742f5f33bcf922b065ad5ed3d969977a` | `e59f95a80497779b861daa26e1b890929fd2f9939e78003898d9bff3ea3f6db2` |
| Headerless PRG | `0d9f5bd1` | `8a8bbecc77fdf59826257754f357d38a7f825971` | `84f5377980d2fd44d71faec42f858b1e83540c2f55aba9236c3279d6dde8592a` |
| Source ZIP | — | — | `e835e1c27d3adb0d6a070f79f60e533bee053d369e903e812323bb995f6cfc36` |

All three ROM hashes, size, and all three headerless hashes match the primary
[No-Intro USA record 1360](https://datomatic.no-intro.org/index.php?page=show_record&s=45&n=1360),
which marks both forms Trusted (4), Verified. This confirms a clean retail payload.
The record explains that its headered representation was made using NES 2.0 XML
version 2021-12-25 and NES Header Repair Tool commit 77920fc. The container header
is catalog metadata; the cartridge payload is the dumped game.

The complete DAT download endpoint returned a temporary-unavailability message.
Primary record confirmation succeeded through the public search and record page;
no ROM download was used. Regional metadata initially came from the pinned
[libretro No-Intro mirror](https://github.com/libretro/libretro-database/blob/d5bae90b22018ce3c9c5a8eff4141a4768c8dd5f/metadat/no-intro/Nintendo%20-%20Nintendo%20Entertainment%20System.dat),
DAT version 2026.08.01. USA entries now include primary record provenance and
SHA-256; other region entries remain mirror evidence only.

The matching target is the owner's full-file SHA-1 above. The initial opaque-bank
build and all subsequent reviewed-range builds matched every byte, CRC32, SHA-1
and SHA-256. The current 23-symbol build preserves the same hashes. Future builds
must continue to match this same target.
