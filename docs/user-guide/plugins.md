# Plugins

Plugins add features to OpenTalk, such as a dice roller or a command that tells you who is on the server. Each plugin is a separate program that OpenTalk starts for you. OpenTalk lets it see and do only what you allow.

**Only install plugins from people you trust.** OpenTalk controls what a plugin can do *in OpenTalk*, but a plugin is still a program on your computer: a harmful one could damage your computer like any other downloaded program.

## Installing a plugin

1. Unpack the plugin you downloaded into a folder. The folder contains a file called `plugin.json`.
2. In OpenTalk, choose **Plugins → Manage plugins** (Alt+P, then Enter).
3. Press **Install from folder** and choose that folder. OpenTalk copies it, so you can delete the download afterwards.
4. The plugin is installed but off. Select it and press **Turn on**.
5. OpenTalk lists what the plugin will be able to do, for example "Sensitive: Send messages in your name." Untick anything you do not want it to do (Space ticks and unticks), then press **Turn on**.

OpenTalk comes with two example plugins in its `examples\plugins` folder: **Dice roller** (adds "Roll a dice" to the Plugins menu, and answers `!dice` in your channel) and **Who is here** (needs Python).

## Using plugins

- A plugin's own commands appear in the **Plugins** menu, followed by the plugin's name in brackets.
- When a plugin tells you something, it appears in your history as "Plugin name: text", and your screen reader reads it if the plugin asks.
- Turned-on plugins start each time OpenTalk starts.

## The plugin manager

| Button | Does |
|---|---|
| Install from folder | Installs a plugin, or updates one you already have. |
| Turn on | Asks your permission, then starts it. Also used to review a plugin that changed. |
| Turn off | Stops it and keeps it off. |
| Restart | Starts it again, for example after it was stopped because of a problem. |
| Show log | What the plugin wrote about itself; useful when reporting a problem to its author. |
| Remove | Deletes it. You can keep or delete the settings it saved. |

The details under the list show who made the plugin, its licence, what it asks for and what you allowed.

## When something goes wrong

- **A plugin that changed** (for example after you installed a new version) does not run until you review it with **Turn on**. Permissions it did not ask for before start unticked.
- **A plugin that misbehaves** (sends too much, asks for things you did not allow, or stops responding) is turned off automatically, and your history says why. One that crashes is restarted up to three times, then turned off.
- **If OpenTalk does not work properly** after you install a plugin, start it with `OpenTalk.exe --no-plugins`, then turn the plugin off or remove it.

Plugin files are kept in your OpenTalk data folder: the plugins in `plugins\`, the settings they save in `plugin-data\`, your decisions in `config\plugins.json`, and their logs in `logs\plugins\`.
