# Find and Download a ProTracker MOD File

Search modarchive.org for a 4-channel ProTracker .mod file matching a description, download it, and optionally deploy to the Amiga.

## Arguments
- $ARGUMENTS: Description of the music style wanted (e.g., "uptempo space techno", "chill ambient", "chiptune"). If empty, ask what style.

## Steps

1. **Search** modarchive.org by fetching search result pages. Try multiple search terms derived from the description. Use WebFetch on URLs like:
   - `https://modarchive.org/index.php?request=search&query=TERM&type=filename_or_songtitle&search_type=search_modules`

2. **Download candidates** using curl with module IDs:
   ```bash
   curl -sL -o /tmp/candidate.mod "https://api.modarchive.org/downloads.php?moduleid=XXXXX"
   ```

3. **Validate format** — must be 4-channel ProTracker (not XM, IT, S3M):
   ```bash
   file /tmp/candidate.mod
   # Must say "4-channel Protracker module sound data"
   ```
   Reject files that say "Impulse Tracker", "Fasttracker II", "Screamtracker", or "Startracker".

4. **Report** the title and ask the user if they want to use it.

5. **Deploy** if requested — copy to the project's example directory and to AmiKit shared folder:
   ```bash
   DEPLOY_DIR="/Applications/AmiKit.app/Contents/SharedSupport/prefix/drive_c/AmiKit/Dropbox/Dev"
   cp /tmp/candidate.mod "$DEPLOY_DIR/FILENAME.mod"
   ```

## Tips for searching
- Try genre keywords: techno, trance, chip, funk, rock, ambient, demo, game
- Try theme keywords: space, galaxy, cosmic, cyber, retro, action
- Well-known module IDs for reference:
  - 57925: "space_debris" (chill classic)
  - 41529: "elekfunk !" (funky electronic)
  - 67230: "monty on the run 2" (game tune)
  - 59344: "stardust memories" (ambient classic)
  - 104486: "genetic-research" (electronic)
  - 130609: "sky diving" (action)
- Try IDs in ranges around known good ones (±100)
