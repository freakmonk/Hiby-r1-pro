# HiBy R1 Pro Firmware Mod and Homebrew Applications

`r1.upt` is a modified firmware image for the HiBy R1 digital audio player.
It is based on the [HiBy R1 modification by bidhata](https://github.com/bidhata/Hiby-R1-Mod) and adds a boot menu with
support for launching applications from a MicroSD card. Base fw is <b>v1.8 b2</b>.

The project scans the card for installed programs and presents them in the
boot menu. Applications can be added or replaced without changing the main
firmware. Example test applications and their source code are kept in `src/`.

## Included applications

- **Audiobook** - an audiobook player forked and reworked from
    [Hiby-R1-Audiobook-Mod](https://github.com/yetisoldier/Hiby-R1-Audiobook-Mod).
- **Calculator** - a small calculator application.
- **DOOM** - a port of the game with sound support and a launcher that lets
    you choose the WAD to play.
- **Raw** - a port of the `Another World` 1991 game with sound support, based on [Another World Bytecode Interpreter](https://github.com/fabiensanglard/Another-World-Bytecode-Interpreter) by fabiensanglard.
    you choose the WAD to play.
- **GameBoy Emulator** - a modified version based on bidhata's emulator,
    with sound support.
- **InfoNES Emulator** - a ported version of NES/Famicom/Dendy emulator based on [InfoNES](https://github.com/jay-kumogata/InfoNES) by jay-kumogata,
    with sound support.
- **HiCommander** - a file manager for the player.
- **MPlayer** - a simple video player with sound support.
- **Open Hiby Player** - open source player from [Starnished66](https://github.com/Starnished66/R1-open-source-player/). 
- **Rockbox run script** - download and install on sd card [RockBox mod from bahusoid](https://github.com/bahusoid/rockbox/releases/tag/x1600_hiby_v1). Its might be `.rockbox` directory on the root of sd card.

The `src/` directory also serves as a starting point for writing and testing
your own applications for the HiBy R1. Each application has its own source
directory and build files where applicable.

## Installation

No compilation is required to use the pre-built firmware and applications.

Copy the following files to the root of a MicroSD card:

1. Copy the firmware file `r1.upt` to the card root.
2. Copy the contents of `sd_card/Apps/` to an `Apps/` directory on the card.
3. Download WAD Doom game files to `Apps/doom/` directory.
4. Create a `Apps/gb-emu/games/` directory in the card root and download Game Boy ROM files
    (`.gb` or `.gbc`) into it.
5. Create a `Audiobooks/` directory in the card root and download here some audiobooks with folder configuration of `Audiobooks/Arthur Conan Doyle/The Lost World/Some_sound_files.mp3`.

The resulting card structure should look similar to this:

```text
MicroSD Card/
├── r1.upt
├── .rockbox/
├── Apps/
│       ├── Audiobook/
│       ├── Rockbox/
│       ├── calc/
│       ├── doom/
│       │   ├── DOOM.WAD
│       │   ├── DOOM2.WAD
│       │   ├── THT.WAD
│       │   └── doom
│       ├── gb-emu/
│       │   └── games/
│       │       ├── game.gb
│       │       └── game.gbc
│       ├── InfoNES/
│       │   └── games/
│       │       ├── game1.nes
│       │       └── game2.nes
│       ├── hicmndr/
│       └── mplayer/
│       └── open-player/
│
└── Audiobooks/
        └── Author/
            └── Book/
                ├── 01.mp3
                └── 02.mp3
```

To flash the device:

1. Insert the card into the HiBy R1.
2. Open **System Settings -> System Update**.
3. Confirm the update and wait for the player to reboot.
4. Select an application from the boot menu.

## DOOM controls

```text
+------------------------------------+ (0,0)
|                                    |
|          DOOM GAME VIEW            |
|       (320x200 scaled 1.5x)        |
|             480x300                |
|                                    |
+------------------------------------+ (0,350)
|  [ESC]    [TAB]    [ENTER]   [YES] |  (Menu / Map / Confirm)
+------------------------------------+ (0,430)
|   [UP]   |   [WEAPON]  [USE]       |  (D-Pad Up / Switch Weapon / Use)
| [L]  [R] |   [ FIRE ]  [RUN]       |  (D-Pad Left/Right / Fire / Run)
|  [DOWN]  |                         |  (D-Pad Down)
+------------------------------------+ (480,800)
```

- **Volume +** (`KEY_VOLUMEUP`): Fire / Attack (`Ctrl`)
- **Volume -** (`KEY_VOLUMEDOWN`): Use / Open Door (`Space`)
- **Power** (`KEY_POWER`): Exit / Menu (`ESC`)

## Credits and licenses

- The firmware image and boot-menu integration are modifications based on the
    work by **bidhata**. Refer to the original project for its license and
    attribution requirements.
- The Audiobook application is derived from
    [Hiby-R1-Audiobook-Mod](https://github.com/yetisoldier/Hiby-R1-Audiobook-Mod);
    its original license and notices apply to the derived portions.
- The DOOM engine is based on the GPL-licensed DOOM source code and fbdoom
    work. DOOM WAD files are separate game data and are not covered by the GPL;
    use only WAD files that you are legally entitled to use.
- The Game Boy emulator, Calculator, HiCommander, MPlayer, and project glue
    code are provided under the GNU General Public License v2.0 unless a source
    file or upstream project states otherwise.
- Third-party libraries, fonts, build tools, and firmware components retain
    their own licenses and copyright notices.

This repository does not grant rights to redistribute HiBy firmware or game
data. Review the applicable upstream licenses before redistributing builds.