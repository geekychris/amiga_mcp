# Build, Deploy, and Run an Amiga Project

Build an example project via Docker cross-compilation, deploy to AmiKit, and launch on the Amiga.

## Arguments
- $ARGUMENTS: Project name (e.g., "uranus_lander", "rock_blaster"). If empty, ask which project.

## Steps

1. **Build** via Docker:
```bash
docker run --rm -v "$(pwd)":/work -w /work amigadev/crosstools:m68k-amigaos make -C examples/$ARGUMENTS clean all
```

2. **Deploy** binary + any .mod files to AmiKit shared folder:
```bash
DEPLOY_DIR="/Applications/AmiKit.app/Contents/SharedSupport/prefix/drive_c/AmiKit/Dropbox/Dev"
cp examples/$ARGUMENTS/$ARGUMENTS "$DEPLOY_DIR/"
# Also copy any .mod files
for f in examples/$ARGUMENTS/*.mod; do [ -f "$f" ] && cp "$f" "$DEPLOY_DIR/"; done
```

3. **Launch** on Amiga using MCP:
Use `mcp__amiga-dev__amiga_launch` with command `DH2:Dev/$ARGUMENTS`

4. **Screenshot** after 2 seconds to verify it's running:
Use `mcp__amiga-dev__amiga_screenshot` and display the result.

If the build fails, show errors and suggest fixes. If deploy path doesn't exist, check `devbench.toml` for the correct `deploy_dir`.
