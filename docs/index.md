---
tags:
  - plugin
resource_link: "https://www.redguides.com/community/resources/mq2say.1744/"
support_link: "https://www.redguides.com/community/threads/mq2say.74469/"
repository: "https://github.com/RedGuides/MQ2Say"
config: "MQ2Say.ini"
authors: "sl968, EQMule, Brainiac, Lax, Knightly, ChatWithThisName, Sic"
tagline: "A Say Detection and Alerting Plugin."
---

# MQ2Say

<!--desc-start-->
The plugin creates a dedicated window for Say Messages which originated from player characters, it will ignore NPC or item say messages. Say messages from a GM or Guide, should be flagged with a special (GM) tag.
<!--desc-end-->
A customizable alert command can be defined that includes any valid MQ or EQ command. The default is set to 3 beeps. This must be edited in the ini (default is MQ2Say.ini) and the plugin reloaded.

Once a speaker has triggered an alert subsequent messages from them will still be displayed, no new alert is triggered for a customizable delay in seconds. The default is set to 5 minutes. Set the IgnoreDelay to 0 to always notify.

This plugin is intended to catch say messages in close proximity to your group that may otherwise be lost in the game spam, or hidden behind another window. This is important due to the fact that if a Gamemaster is checking to see if your group is AFK they will frequently pop in and say something to your group, and if you don't respond quick enough you can find your group kicked and/or banned from the game.

Please Provide feedback and bug reports on the plugin's discussion thread, and if you like the plugin consider [rating it here](https://www.redguides.com/community/resources/mq2say.1744/rate)!


## Commands

<a href="cmd-mqsay/">
{% 
  include-markdown "projects/mq2say/cmd-mqsay.md" 
  start="<!--cmd-syntax-start-->" 
  end="<!--cmd-syntax-end-->" 
%}
</a>
:    {% include-markdown "projects/mq2say/cmd-mqsay.md" 
        start="<!--cmd-desc-start-->" 
        end="<!--cmd-desc-end-->" 
        trailing-newlines=false 
     %} {{ readMore('projects/mq2say/cmd-mqsay.md') }}

## Settings

example MQ2Say.ini,

```ini
[Settings]
SayStatus=on
SayDebug=off
AutoScroll=on
SaveByChar=on
IgnoreDelay=300
FilterNPC=on
[vox.Voxvox]
SayTop=261
SayBottom=461
SayLeft=203
SayRight=603
Locked=0
Fades=0
Delay=2000
Duration=500
Alpha=255
FadeToAlpha=255
BGType=1
BGTint.alpha=255
BGTint.red=0
BGTint.green=0
BGTint.blue=0
FontSize=4
WindowTitle=Say Detection
Alerts=on
AlertCommand=/multiline ; /beep ; /timed 1 /beep ; /timed 30 /burp
Timestamps=on
```

Explanations for these settings can be found on the [mqsay](cmd-mqsay.md) command page.

## Top-Level Objects

## [SayWnd](tlo-saywnd.md)
{% include-markdown "projects/mq2say/tlo-saywnd.md" start="<!--tlo-desc-start-->" end="<!--tlo-desc-end-->" trailing-newlines=false %} {{ readMore('projects/mq2say/tlo-saywnd.md') }}

## DataTypes

## [saywnd](datatype-saywnd.md)
{% include-markdown "projects/mq2say/datatype-saywnd.md" start="<!--dt-desc-start-->" end="<!--dt-desc-end-->" trailing-newlines=false %} {{ readMore('projects/mq2say/datatype-saywnd.md') }}

<h2>Members</h2>
{% include-markdown "projects/mq2say/datatype-saywnd.md" start="<!--dt-members-start-->" end="<!--dt-members-end-->" %}
{% include-markdown "projects/mq2say/datatype-saywnd.md" start="<!--dt-linkrefs-start-->" end="<!--dt-linkrefs-end-->" %}
