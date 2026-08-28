# 7-Zip installer SFX module

`7zSD.sfx` is the installer module from the official LZMA SDK 26.02 package:

- Source: https://github.com/ip7z/7zip/releases/download/26.02/lzma2602.7z
- Download SHA-256: `2878C85F5F43A4A4E0952B1FD4E5FE097C1C143997A8047C7E1E788892AA9357`
- Embedded `bin/7zSD.sfx` SHA-256: `0FC21D175A0E4C7E4F10521F35F6E6EFFA9DBD3C0EA80895CC79E72A9FDDE088`
- License: public domain, as stated by the official LZMA SDK page.

`packageRelease.ps1` combines this module, a UTF-8 installer directive, and the
compressed runtime payload to produce the one-click setup executable.
