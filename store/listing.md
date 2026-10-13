# gessetto — App Store listing (draft)

One app on the App Store for iPad and the Mac (universal purchase, the same
bundle ID on both). Fields as App Store Connect names them, with their
limits. English (U.S.) is the primary language.

## Name (30)

gessetto

## Subtitle (30)

Pixel art and sprites for games

## Promotional text (170)

Draw pixel art and animated sprites with Apple Pencil or a mouse, keep
them in plain-text files, and hand your game a C header. Nothing collected.

## Description (4000)

gessetto is a small pixel-art editor for making game assets: sprites,
tiles and short animations, on the iPad with Apple Pencil and on the Mac.

- No ads
- No subscriptions
- No accounts, no tracking
- Your files, in your iCloud Drive

DRAW
Pencil with pixel-perfect lines, eraser, fill, color picker, spray, line,
curve, rectangle with rounded corners, ellipse, polygon and text, a pen
from 1 to 16 pixels, square or round, and mirror symmetry. Select by
rectangle or lasso, then move, copy, flip and rotate. Every change undoes.

COLOR
Indexed color, up to 256 colors, with the palette inside the file. Start
from PICO-8, DawnBringer 16 or 32, Game Boy, or load a .hex, .gpl or
another image's palette; reorder and remove colors and the pixels follow.

ANIMATE
Frames with their own durations, tags that play forward, in reverse or
ping-pong, onion skin of the frames around, a live preview, and copy and
paste of frames between images.

MADE FOR APPLE PENCIL
The Pencil draws and your fingers move the view: pinch to zoom, two-finger
tap to undo, three to redo, touch and hold to pick a color. Hover shows
where the pen will land, and double tap or squeeze does what you set in
the system's Pencil settings.

EXPORT
PNG at any whole scale, sprite sheets, animated GIF, and a C header with
the palette, the frames and the tags, ready for a game to include. Import
PNG images and sprite sheets.

PLAIN-TEXT FILES
A .gesso file is readable text: open it in any editor, keep it in git,
and let your build turn it into C.

gessetto is open source (MIT): github.com/crgimenes/gessetto

## Keywords (100, comma-separated, no spaces after commas)

pixel art,sprite,pixel,animation,gamedev,palette,tiles,8-bit,retro,editor,gif,spritesheet,pencil

## URLs

- Support URL: https://github.com/crgimenes/gessetto/issues
- Marketing URL (optional): https://crg.eti.br/projects/gessetto/
- Privacy Policy URL: https://crg.eti.br/projects/gessetto/#privacy

The project page with the privacy section is drafted in the site repository
(crg.eti.br/content/projects/gessetto/, `draft = true`), not published yet.

## Category

- Primary: Graphics & Design
- Secondary: Developer Tools

## Copyright

2026 Cesar Gimenes

## App Privacy (nutrition label)

Data Not Collected. gessetto has no analytics, crash reporting,
advertising or accounts, and makes no network connections of its own.
Documents live where the person keeps them (the device, iCloud Drive) and
travel only through the system's document browser and share sheets.

## Age rating

Answer the questionnaire as it stands; the app has no content of its own,
no web access and nothing shared between people.

## Export compliance

No encryption of its own: `ITSAppUsesNonExemptEncryption` = false in both
Info.plists, so builds go through without the question. All territories
can be open (no ANSSI declaration needed for France).

## App Review notes

gessetto is a pixel-art editor. Create a document (it asks the canvas
size), draw with the toolbar's tools, add frames below the canvas and
press Play. Export (File > Export on the Mac, the … button on the iPad)
writes PNG, sprite sheets, GIF or a C header. No account, server or
network is involved.

## Screenshots

Still to make, from real artwork: iPad 13" (2064 × 2752) and Mac
(2880 × 1800 or 1280 × 800), opaque PNG.

## Versions

App Store Connect created version 1.0 for both platforms; the builds say
0.1.0. Before submitting, pick the version (fosforo went 1.0.0) and set
CFBundleShortVersionString in both plists to match.
