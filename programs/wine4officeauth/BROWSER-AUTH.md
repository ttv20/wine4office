# Work or school sign-in in the system browser

Set `WINE4OFFICE_AUTH_MODE=browser` to use the system browser for the Office
work/school sign-in flow. This is an opt-in workaround for embedded sign-in pages
that go blank during federation or MFA. Personal Microsoft accounts keep the
existing embedded flow.

The helper opens the same authorization URL, with the existing Office client,
registered redirect, random state and S256 PKCE challenge. The browser returns
through `ms-appx-web` to a waiting helper in the selected prefix. Only a callback
with the expected redirect and matching active state is accepted. The existing
code exchange, account checks, licensing request and DPAPI cache remain in use.
This does not use device-code authentication or bypass organization policy.

## Set up the Linux callback handler

Use a runner built with this change. The prefix must also have the updated
`wine4officeauth.exe` in `C:\windows\system32` (and `syswow64` for WoW64).
The callback handler needs Python 3 and an existing Office prefix. It does not
install Office or initialize a new prefix.

Copy `programs/wine4officeauth/wine4office-browser-return` to a stable location,
for example `~/.local/bin/wine4office-browser-return`, and make it executable.
Create `~/.local/share/applications/wine4office-browser-return.desktop`:

```ini
[Desktop Entry]
Type=Application
Name=Wine4Office Sign-in Return
Exec="/absolute/path/to/wine4office-browser-return" --wine "/absolute/path/to/runner/bin/wine" --prefix "/absolute/path/to/office-prefix" %u
NoDisplay=true
Terminal=false
MimeType=x-scheme-handler/ms-appx-web;
StartupNotify=false
```

Replace all three paths with your actual absolute paths. Desktop entries do not
expand `~` or shell variables. Quote paths with spaces; desktop-entry escaping
rules also apply to literal backslashes, dollar signs, backticks and percent signs.

Check the current association before changing it and save its name for rollback:

```sh
xdg-mime query default x-scheme-handler/ms-appx-web
xdg-mime default wine4office-browser-return.desktop x-scheme-handler/ms-appx-web
```

This association is per desktop user and points to **one prefix and runner**.
Multiple waiting sign-ins within that prefix are matched by state. Signing in
from a different prefix requires changing the handler configuration first.
The `ms-appx-web` association can affect other applications that use that scheme;
this handler rejects callbacks outside the Office broker redirect.

Close Office normally, then launch it with the environment variable set, for example:

```sh
WINE4OFFICE_AUTH_MODE=browser Wine4OfficeManager --prefix /absolute/path/to/office-prefix --wine /absolute/path/to/runner/bin/wine word
```

Complete sign-in and MFA in your browser. If prompted, allow it to open
**Wine4Office Sign-in Return**. Leave the waiting window open; it closes when a
matching callback arrives. Cancel closes the attempt, and it expires after
15 minutes. If the browser cannot launch or the callback cannot be delivered,
the attempt fails without saving a new account.

The callback contains a short-lived authorization code. The bridge does not
print or persist it, and suppresses Wine output. As with other desktop URL
handlers, it is passed briefly in process arguments. Do not paste real callback
URLs into a terminal, issue, or diagnostic log.

To return to embedded sign-in, unset `WINE4OFFICE_AUTH_MODE` and restart Office.
Restore the previous `ms-appx-web` default with `xdg-mime default` if one existed.
If none existed, remove only the `wine4office-browser-return.desktop` association
from your user `mimeapps.list`, then remove the desktop file and copied handler.

## Tests

```sh
python3 -m unittest discover -s programs/wine4officeauth/tests -v
```

Build both helper architectures, then run the synthetic parser and callback
checks against a test prefix that has the updated helper installed:

```sh
python3 programs/wine4officeauth/tests/browser_callback_smoke.py \
  --wine /absolute/path/to/runner/bin/wine \
  --prefix /absolute/path/to/test-prefix \
  --helper /absolute/path/to/build/programs/wine4officeauth/x86_64-windows/wine4officeauth.exe \
  --helper /absolute/path/to/build/programs/wine4officeauth/i386-windows/wine4officeauth.exe
```

These checks do not contact Microsoft or write authentication tokens. The native
`--self-test-browser` validates callback parsing and state; the listener test
exercises native IPC and Linux bridge delivery. Real browser sign-in, MFA,
licensing and activation still need a manual account test.
