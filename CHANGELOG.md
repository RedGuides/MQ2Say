Jun 4, 2021
- Addded the "AlertPerSpeaker" option which is on by default.  This only matters if you have alerts turned on.
  When On:  The Ignore Timer ignores repeated says from the same speaker for whatever time interval you have set.
            New people speaking will alert and have their own timer.
  When Off:  The Ignore Timer is global for all speakers.  Any "say" alerts will be ignored until the timer is up.

May 15, 2021
- Added `${SayWnd.LastSpeaker}` for the last person to speak (and show up in the say window)
- Added `${SayWnd.LastSay}` for the last text that was parsed out
- These can be used in actions to relay information as needed
