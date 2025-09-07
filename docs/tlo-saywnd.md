---
tags:
  - tlo
---
# `SayWnd`

<!--tlo-desc-start-->
Has a form that returns the current title of the say window, and holds a datatype that does the same thing.
<!--tlo-desc-end-->

## Forms
<!--tlo-forms-start-->
### {{ renderMember(type='saywnd', name='SayWnd') }}

:   Returns the current title of the say window

<!--tlo-forms-end-->

## Associated DataTypes
<!--tlo-datatypes-start-->
## [`saywnd`](datatype-saywnd.md)
{% include-markdown "projects/mq2say/datatype-saywnd.md" start="<!--dt-desc-start-->" end="<!--dt-desc-end-->" trailing-newlines=false %} {{ readMore('projects/mq2say/datatype-saywnd.md') }}
:    <h3>Members</h3>
    {% include-markdown "projects/mq2say/datatype-saywnd.md" start="<!--dt-members-start-->" end="<!--dt-members-end-->" %}
    {% include-markdown "projects/mq2say/datatype-saywnd.md" start="<!--dt-linkrefs-start-->" end="<!--dt-linkrefs-end-->" %}
    <!--tlo-datatypes-end-->

## Examples
<!--tlo-examples-start-->
/echo ${SayWnd}
<!--tlo-examples-end-->

<!--tlo-linkrefs-start-->
[saywnd]: datatype-saywnd.md
<!--tlo-linkrefs-end-->