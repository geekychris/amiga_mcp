# Tune Game Parameters via Bridge

Adjust game tunable variables in real-time through the AmigaBridge connection. This lets you tweak physics, difficulty, and other parameters without rebuilding.

## Arguments
- $ARGUMENTS: What to tune (e.g., "make gravity weaker", "increase lives to 5", "show all variables"). If empty, list available variables.

## Steps

1. **List clients** to find the running game:
   Use `mcp__amiga-dev__amiga_list_clients`

2. **List variables** for the game:
   Use `mcp__amiga-dev__amiga_get_var` to read current values, or list all registered variables.

3. **Set variables** based on the request:
   Use `mcp__amiga-dev__amiga_set_var` with the client name, variable name, and new value.

4. **Call hooks** if needed:
   Use `mcp__amiga-dev__amiga_call_hook` for actions like "reset", "next_level", "add_life", "add_fuel".

5. **Verify** by taking a screenshot to see the effect.

## Common game tunables
Most games register these variables:
- `gravity` — Physics gravity strength
- `thrust` — Thrust/movement power
- `lives` — Current lives
- `score` — Current score
- `level` — Current level
- `fuel_max` — Maximum fuel

## Common hooks
- `reset` — Reset game to initial state
- `next_level` — Skip to next level
- `add_life` — Add an extra life
- `add_fuel` — Refill fuel
