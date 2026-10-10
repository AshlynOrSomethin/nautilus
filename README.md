# nautilus
[![Pipeline status](https://gitlab.gnome.org/GNOME/nautilus/badges/main/pipeline.svg)](https://gitlab.gnome.org/GNOME/nautilus/commits/main)
[![coverage report](https://gitlab.gnome.org/GNOME/nautilus/badges/main/coverage.svg)](https://gitlab.gnome.org/GNOME/nautilus/commits/main) 

This is the project of the [Files](https://apps.gnome.org/Nautilus/) app, a file browser for
GNOME, internally known by its historical name `nautilus`.

## Supported version
Only the latest version of Files as provided upstream is supported. Try out the [Flatpak nightly](https://welcome.gnome.org/en/app/Nautilus/#installing-a-nightly-build) installation before filling issues to ensure the installation is reproducible and doesn't have downstream changes on it. In case you cannot reproduce in the nightly installation, don't hesitate to file an issue in your distribution. This is to ensure the issue is well triaged and reaches the proper people.

## Runtime dependencies
- [Bubblewrap](https://github.com/containers/bubblewrap) installed. Used for security reasons.
- [LocalSearch](https://gitlab.gnome.org/GNOME/localsearch) properly set up and with all features enabled. Used for fast search and metadata extraction, starred files and batch renaming.
- [xdg-user-dirs-gtk](https://gitlab.gnome.org/GNOME/xdg-user-dirs-gtk) installed.  Used to create the default bookmarks and update localization.

## Inline terminal

**F4** or **View Options → Show Terminal** toggles a resizable terminal panel
below the file view. By default this embeds actual Ptyxis tabs and its session
agent, built from a pinned upstream revision with a small embedding adapter.
It does not reparent a separate Ptyxis window or spoof terminal identification.

The panel starts your user shell in the current local folder. Navigating while
the panel is visible starts a new session without changing directories in, or
interrupting, earlier shells. Use the terminal tabs to return to those shells,
or **New Session** to start another shell in the current folder. Hiding the panel
keeps sessions alive; navigation while hidden is applied when it is shown again.
Remote and virtual locations display an explanation rather than starting a shell.
Sessions belong to each file-browser tab and end when that tab closes, so finish
any important terminal work before closing it.

Use **Ctrl+Shift+C** and **Ctrl+Shift+V** to copy and paste in the terminal;
**Ctrl+C** remains available to interrupt commands.

The Ptyxis panel includes its real terminal widget, palettes, links, paste
handling, regex search, zoom, profile settings, and host/container session agent.
Select a profile and a container for new sessions in the panel. The preferences
button opens Ptyxis's preferences, sharing your `org.gnome.Ptyxis` settings.
The overall application theme remains controlled by Nautilus.
The terminal context menu supports read-only mode, changing the session title
and profile, and resetting the terminal. Standalone Ptyxis window management
and fullscreen controls are not used inside Nautilus. If the session agent
disconnects, the panel reports the error without closing the file manager;
restart Nautilus to reconnect.
Use **Ctrl+Shift+T** for a new session, **Ctrl+Shift+F** to search, and
**Ctrl+Shift+W** to close the selected session. Closing a terminal session with a
foreground command asks for confirmation; closing its enclosing Nautilus tab
still ends all of that tab's terminal sessions.

The Meson `terminal` feature defaults to `auto`: install the `vte-2.91-gtk4`
development dependency to include the panel, use `-Dterminal=enabled` to require
it, or `-Dterminal=disabled` to omit it. `-Dterminal-backend=ptyxis` is the default
and also requires JSON-GLib and Ptyxis's GTK/Adwaita/VTE minimum versions.
Meson downloads Ptyxis through [its wrap](subprojects/ptyxis.wrap); packagers
must install the built embedding library, agent, and schemas alongside Nautilus.
`-Dterminal-backend=vte` selects the simpler VTE-only implementation without
Ptyxis profiles or its agent. The panel is not available in file-picker dialogs.

## Discourse

For more informal discussion we use [GNOME Discourse](https://discourse.gnome.org/tags/nautilus) in the Applications category with the `nautilus` tag. Feel free to open a topic there.

## Extensions

Documentation for the libnautilus-extension API is available [here](https://gnome.pages.gitlab.gnome.org/nautilus/).  Also, if you are interested in developing a Nautilus extension in Python you should refer to the [nautilus-python](https://gnome.pages.gitlab.gnome.org/nautilus-python/) documentation.

## How to report issues

Report issues to the GNOME [issue tracking system](https://gitlab.gnome.org/GNOME/nautilus/issues).

## Feature requests

Files is a core compoment of the GNOME desktop experience. As such, any changes in behavior or appearance only happen in accordance with the [GNOME design team][design-team].

For major changes, it is best to start a discussion on [discourse] and reach out on the [#gnome-design matrix room][design-room], and only involve the issue tracker once agreement has been reached.

In particular mockups must be approved by the design team to be considered for implementation.

For enhancements that are limited in scope and well-defined, it is acceptable to directly open an issue using the shortcoming template.

[design-team]: https://gitlab.gnome.org/Teams/Design
[discourse]: https://discourse.gnome.org/tag/nautilus
[design-room]: https://matrix.to/#/#gnome-design:gnome.org
