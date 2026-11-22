Copied word for word on 2026-11-22 from section 4.3 ("The measures hold") of the paper `00-READ-FIRST.md` of the plan before 2026-11-20, which is no longer in the repository.

| Measure | Target |
|---|---|
| Game speed | 60.000 steps a second within 0.01%, except the 59.94 lock |
| Even pictures, PC, any offered rate, VSync on | Every present exactly the intended number of refreshes after the one before, over ten minutes in three galaxies. No missed refresh |
| Animation error (the game time a picture shows against the time it reaches the display) | 99 of 100 pictures within 1 ms |
| Delay with in-between pictures on | One step plus one refresh, plus the step's own work, or less |
| Delay with in-between pictures off | The step's own work plus one refresh, or less |
| A long step outside loading | None over 18 ms on the PC |
| Hitch at a scene's start, second run onward | None over one refresh |
| Tablet at 60 Hz | Every picture at its asked time; two refreshes from step to display, or better |
| Tablet at 120 Hz | **Held in every one of the 102 tour stops:** 99 of 100 pictures exactly one refresh after the last. Dynamic resolution may act to hold it |
| Render-thread work for an in-between picture | Does not grow with the number of objects drawn |
| The full-size scene | Opened once per picture when nothing reads it back; copied only when something visible reads it |

**If a target cannot be met,** you do not lower it and you do not go quiet. You write the measurements and the arithmetic that show where the floor is and why, add it to the ledger as a row for the owner, and go on with the rest.
