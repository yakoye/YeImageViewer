# 7-Zip installer SFX module

`7zS2.sfx` is the small GUI installer module from the official LZMA SDK 26.02
package. It runs the bundled `setup.cmd`, which invokes `installLocal.ps1`
without requiring administrator elevation:

- Source: https://github.com/ip7z/7zip/releases/download/26.02/lzma2602.7z
- Download SHA-256: `2878C85F5F43A4A4E0952B1FD4E5FE097C1C143997A8047C7E1E788892AA9357`
- Embedded `bin/7zS2.sfx` SHA-256: `5844E4A1F78F309170B8A956DF9A24CAF932A6BA4CF1FCDE3E0066D850FBF5E3`
- License: public domain, as stated by the official LZMA SDK page.

`packageRelease.ps1` combines this module and the compressed runtime payload to
produce the one-click setup executable. The module automatically selects the
root-level `setup.cmd` and forwards command-line arguments to it.
