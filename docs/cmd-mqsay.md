---
tags:
  - command
---

# /mqsay

## Syntax

<!--cmd-syntax-start-->
```eqcommand
/mqsay [option] [value]
```
<!--cmd-syntax-end-->

## Description

<!--cmd-desc-start-->
Configures various settings, from ignoring certain messages to font size.
<!--cmd-desc-end-->

## Options

| Option | Description |
|--------|-------------|
| `help | (no option)` | Displays help text. |
| `on|off` | Turns the plugin on or off |
| `reload` | reload settings from the configuration file |
| `reset` | resets window to default position |
| `clear` | Clears the /say window |
| `debug` | turns on debug mode |
| `title  <New Window Title>` | Changes the title of the /say window. If no name is provided, it returns the current title. |
| `font <#>` | Changes the font size in the say window. e.g. `/mqsay font 5` |
| `IgnoreDelay <#>` | Defines how long a new say from the player is ignored before a new alert is sounded. Time is in seconds. |
| `group [on|off]` | Ignores say messages from members of your group. |
| `guild [on|off]` | Ignores say messages from members of your guild. |
| `fellowship [on|off]` | Ignores say messages from members of your fellowship. |
| `raid [on|off]` | Ignores say messages from members of your raid. |
| `alerts [on|off]` | toggles sounding an alert on a new say. |
| `timestamps [on|off]` | Toggles displaying of timestamps on new say messages. |
| `autoscroll [on|off]` | Toggles autoscroll |
| `SaveByChar [on|off]` | Turns SaveByChar off or on. If you prefer your settings to be universal, this should be off. |
| `Settings` | Display current settings |
| `FilterNPC [on|off]` | Toggle to filter non-GM NPCs using the PC Say channel from triggering the say alert |