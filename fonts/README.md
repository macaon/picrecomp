# System fonts

The game draws its text with the Switch's built-in system fonts, which are not part of the game files and are not
included with PicRecomp. Without them PicRecomp uses DejaVu Sans, from the system or from `third_party/fonts/DejaVu`.

To use the console's own fonts, put your own copies here:

- `nintendo_udsg-r_std_003.bfttf`: the main font (a `.ttf` with the same name, or spelled `nintendo_udsgr_std_003`, also works)
- `nintendo_ext_003.bfttf`: button glyphs (optional)

`PICRECOMP_FONT=<file>` overrides everything (.ttf / .otf / .ttc / .bfttf).
