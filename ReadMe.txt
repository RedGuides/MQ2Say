Other Authors
    EQMule, Brainiac, Lax, Knightly 

Software Requirements
    Macroquest 2 

Server Type
    Live 

This plugin is intended to catch say messages in close proximity to your group that may otherwise be lost in the game spam, or hidden behind another window. This is important due to the fact that if a Gamemaster is checking to see if your group is AFK they will frequently pop in and say something to your group, and if you don't respond quick enough you can find your group kicked and/or banned from the game.

The plugin creates a dedicated window for Say Messages which originated from player characters, it will ignore NPC or item say messages. Say messages from a GM or Guide, should be flagged with a special (GM) tag.

A customizable alert command can be defined that includes any valid MQ2 or EQ command. The default is set to 3 beeps. This must be edited in the ini and the plugin reloaded.

Once a speaker has triggered an alert subsequent messages from them will still be displayed, no new alert is triggered for a customizable delay in seconds. The default is set to 5 minutes. Set the IgnoreDelay to 0 to always notify.

Commands

'/mqsay' is the only command for this plugin. It accepts the following options.

<on/off> - turns the plugin functionality on or off.
clear - clears the say window.

reset
- Resets the default position of the window

reload
- Reloads Plugin settings

alerts <on/off>
- toggles sounding an alert on a new say.

autoscroll <on/off>
- will scroll the window to keep the newest message visible.

IgnoreDelay <time in seconds>
- defines how long a new say from the player is ignored before a new alert is sounded.

timestamps <on/off>
- Toggles displaying of timestamps on new say messages.

title <New Window Title>
- Changes the title of the window from the default, "Say Detection" to a custom value.
