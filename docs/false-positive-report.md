# Windows Defender false positive: Trojan:Win32/Wacatac.B!ml

Microsoft Defender flags `VoxualSetup.exe` from the GitHub release as
`Trojan:Win32/Wacatac.B!ml` and removes it. This is a false positive; the file is our own
installer. This page records the evidence and holds the text to submit to Microsoft.

## What was reported

| | |
| --- | --- |
| Product | Voxual 2.0.3 |
| Artifact | `VoxualSetup.exe`, 2,845,696 bytes, SHA256 `3DC9200E981EBB8E05FFC22818D4AAE7CAD07D8AEAC1FF9F78E277F04C8D399A` |
| Detection | `Trojan:Win32/Wacatac.B!ml` |
| Reported | 7 October 2026, 11:05 |
| Action | **Removed** — "A threat or app was removed from this device" |
| Stage | On download, against the release asset URL. The file never ran. |

`!ml` marks a **machine-learning** verdict on the file itself, not a signature match and not a
behavioural detection. `Wacatac` is a generic family name Microsoft uses for that classifier and is
one of the most commonly reported false-positive families in the industry.

## Why the installer, and not the app

Both binaries are unsigned, but they are not shaped alike. Comparing them:

| Feature | `VoxualSetup.exe` | `Voxual.exe` |
| --- | --- | --- |
| Carried PE image inside the file | yes — at `0xD3798`, byte-identical to the app | no |
| `cmd.exe ... del /f /q` on itself | yes | no |
| Self-copy to `Uninstall.exe` | yes | no |
| `.lnk` creation | yes | no |
| `...\CurrentVersion\Uninstall\Voxual` | yes | no |
| Staged `.new` payload write | yes | no |

The installer is a carrier by construction: `res/setup.rc.in` embeds the app as RCDATA resource `2`,
`setup_main.cpp` writes it into the install folder and launches it. "Contains an executable, writes
it out, runs it" is the strongest dropper signal a static classifier can see, which is the most
likely reason only the installer was flagged.

There is a second reason it recurs: the binaries are unsigned and each release has a new hash, so
they carry no download reputation.

## Submit the false positive

Attach `VoxualSetup.exe` and paste the text below at
<https://www.microsoft.com/en-us/wdsi/filesubmission>, choosing *Software developer* and
*incorrectly detected as malware*. Microsoft usually answers within 24–72 hours, and because the
verdict is per file the existing v2.0.3 download starts working again — no re-release needed.

```
Voxual 2.0.3 is a per-user installer, with no administrator rights required, for an open-source
Minecraft server manager. Full source: https://github.com/eefamilyai/McLclSerMgr

Detected as: Trojan:Win32/Wacatac.B!ml (flagged on download; the file was never executed)
VoxualSetup.exe  SHA256 3DC9200E981EBB8E05FFC22818D4AAE7CAD07D8AEAC1FF9F78E277F04C8D399A  (2,845,696 bytes)
Voxual.exe       SHA256 3166E26A468D451F9AF8C357959876F188289B0BD0DE18A10BB46B370A6DCE40  (1,974,784 bytes)

This is a false positive. The installer is a self-contained package: resource RCDATA #2 is our own
application, which it writes to %LOCALAPPDATA%\Programs\Voxual and launches. It creates a Start
Menu shortcut and a desktop shortcut, writes the usual HKCU uninstall entry, and can run unattended
with /S. Its uninstaller removes itself with a shell helper, because a per-user install cannot use
MOVEFILE_DELAY_UNTIL_REBOOT (that API requires administrator rights). All of this is ordinary
installer behaviour and the source for every one of these steps is public at the link above.
```

## After submitting

1. Watch <https://www.microsoft.com/en-us/wdsi/threats> or simply re-download the asset, since the
   verdict is cached per file.
2. If it comes back clean, no action is needed for 2.0.3.
3. **Every future build has a new hash and no reputation, so an unsigned project will hit this
   again.** The durable fix is code signing — see "Code signing (optional)" in the README — which
   also clears the SmartScreen "unrecognized app" prompt. Failing that, the per-user installer's
   carrier shape can be removed altogether by making the app install itself.
