# Capture and Analyze Amiga Screenshot

Take a screenshot of the running Amiga emulator, display it, and optionally analyze or compare it.

## Arguments
- $ARGUMENTS: Optional analysis instruction (e.g., "check if the game is running", "compare with last screenshot", "describe what's on screen"). If empty, just capture and display.

## Steps

1. **Capture** using MCP tool:
   Use `mcp__amiga-dev__amiga_screenshot` to capture the current screen.

2. **Display** the screenshot:
   Use the `Read` tool on the returned PNG file path to view the image.

3. **Analyze** if requested:
   Describe what's visible on screen — identify UI elements, game state, text, errors, etc.

## Tips
- If screenshot returns "Timed out", the Amiga may be unresponsive or a program has the screen locked. Try `mcp__amiga-dev__amiga_ping` first.
- Screenshots are 320x256 (or 640x256 for hi-res Workbench) with 2-4 bitplanes.
- The screenshot captures the frontmost screen — if a game has its own screen, you'll see that instead of Workbench.
