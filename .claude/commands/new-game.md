# Create a New Amiga Game Project

Scaffold a new game project with all the standard boilerplate: double-buffered screen, joystick+keyboard input, ptplayer music, bridge integration, and sound effects.

## Arguments
- $ARGUMENTS: Game name in snake_case (e.g., "space_invaders"). If empty, ask for a name.

## What to create

Create `examples/$ARGUMENTS/` with these files:

### Makefile
```makefile
CC = m68k-amigaos-gcc
AS = vasmm68k_mot
CFLAGS = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
ASFLAGS = -Fhunk -m68000 -no-opt -I/opt/m68k-amigaos/m68k-amigaos/ndk-include -I/opt/m68k-amigaos/m68k-amigaos/ndk-include/include_h
LDFLAGS = -noixemul -g -L../../amiga-bridge -lbridge -lamiga
TARGET = $ARGUMENTS
SRCS = main.c game.c draw.c input.c sound.c
OBJS = $(SRCS:.c=.o) ptplayer.o
all: $(TARGET)
$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<
ptplayer.o: ptplayer.asm
	$(AS) $(ASFLAGS) -o $@ $<
clean:
	rm -f $(OBJS) $(TARGET)
.PHONY: all clean
```

### File structure
- **game.h** — All structs, constants, Fixed-point macros, extern declarations
- **game.c** — Game logic, sin/cos table (copy from rock_blaster), state machine
- **draw.h/draw.c** — Rendering (5x7 bitmap font with A-Z + 0-9, draw helpers)
- **input.h/input.c** — Joystick port 2 + keyboard (WASD, arrows, space, ESC)
- **sound.h/sound.c** — Procedural SFX in chip RAM via ptplayer's mt_playfx
- **main.c** — Screen setup (320x256, 4 bitplanes, double-buffered), music loading, bridge registration, main loop, cleanup

### Copy from rock_blaster
- `ptplayer.asm` and `ptplayer.h` — ProTracker music player

### Critical Amiga C patterns to follow
- Use `-noixemul` (pure AmigaOS, no Unix emulation)
- `sprintf` returns `char*` not `int` — use `strlen()` after
- Use `%ld` with `(long)` cast for all integer printf
- Allocate audio samples with `MEMF_CHIP | MEMF_CLEAR`
- Open/close IntuitionBase and GfxBase
- Double buffer: `AllocScreenBuffer` + `ChangeScreenBuffer` + SafeMessage ports
- `WaitTOF()` for VSync before `swap_buffers()`
- SFX via `SfxStructure` + `mt_playfx()` (sfx_cha = -1 for auto channel)

After creating all files, build the project using the build-deploy-run skill.
