# Community Shaders for Fallout 4

A port of Skyrim Community Shaders to Fallout 4, as an F4SE plugin.

## Requirements

-   Fallout 4 **1.11.240** exactly. The plugin refuses to load on any other version and says so in
    its log rather than risking a crash against relocated addresses.
-   F4SE 0.7.9 or newer, started through `f4se_loader.exe`. Launching the game through Steam loads
    no F4SE plugin at all.

## Installing

Install the archive with a mod manager, or unpack it into the game's `Data` folder. The archive is
already `Data`-relative: `F4SE/` and `Shaders/` belong directly under `Data`.

The all-in-one archive contains everything. The base archive is the plugin alone; each addon
archive carries one feature's shaders and needs the base.

## Settings

Settings live next to the log, in
`Documents/My Games/Fallout4/F4SE/CommunityShadersFO4.json`, with one block per feature. The file
is written with its defaults on first run. Edit it while the game runs and the change takes effect
within a second — there is no menu yet.

## Log

`Documents/My Games/Fallout4/F4SE/CommunityShadersFO4.log`, with the previous five runs kept
alongside it.

## Licence

GPL-3.0-or-later, with the modding exception in `EXCEPTIONS.md`. See `COPYING`.
