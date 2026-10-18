# Stage 0 tester checklist

You have 30 minutes. The observer will not help or explain. If you are stuck, say so out loud and keep going.

## Before you start (observer)

1. Open Git Bash in the repository folder.
2. Run `bin/stage0 --session tester-NAME` (replace NAME). The first run builds the game and can take several minutes. Later runs start at once.
3. The game window opens. Results are written to `stage0-run/fights.csv`. Do not delete it.
4. Write down the start time in `docs/stage0-log.md` section 7 before the tester plays.

## What you are doing

You fight one opponent in a small arena. Win, or lose and try again, as many times as you like for 30 minutes.

## Controls

| Key | Action |
|---|---|
| W A S D | Move |
| J | Light attack |
| K | Heavy attack |
| L (hold) | Block |
| Space | Dodge |
| Enter | Try again after a fight ends |
| Esc | Quit |

The same hints are shown at the bottom of the screen. When you die, the screen says why. Press Enter to retry.

## While playing

- Play normally. Do not try to break the game.
- Say what you are thinking out loud, especially when something surprises you or feels unfair.
- You may stop at any time. Tell the observer when and why.

## After 30 minutes (observer)

Fill in the tester row in `docs/stage0-log.md` section 7:

- Deaths: the number of lost fights (see `stage0-run/fights.csv`).
- Retried immediately?: did they press Enter within a few seconds of each death, most of the time.
- Quit at: the minute they stopped, or "30 min" if they played to the end.

Then ask, and write down the answers word for word:

1. When you died, did you know why?
2. Was there a moment you felt the opponent cheated?
3. Did you want to play another fight?
4. What would you change first?
