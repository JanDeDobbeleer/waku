# From a screenshot or a Reddit post

The user gives you a look. You deliver a working shell with that look where Windows allows it.

## Screenshots

1. Look at every image. Note the resolution. A downscaled image gives approximate pixels: say so.
2. Find the bar or panel areas and measure: thickness, edge, margins, gaps between widgets, radii,
   border width, shadow, blur or opaque. Measure with the image tools you have (crop and zoom,
   read pixel colours). Do not eyeball.
3. Sample colours from flat areas. Record `#aarrggbb`. A blurred translucent surface has no single
   colour: sample over a known background, state the estimate, and use `Backdrop.Blur` with an
   alpha value.
4. Identify fonts by shape. If unsure, say so and propose installed fonts (`Segoe UI Variable` is
   present on Windows 11). Do not name a font the machine lacks.
5. Identify each widget and what it shows (workspaces, clock, volume, battery, music, tray, CPU,
   network, a launcher button, a power menu). Visible text and icons tell you what it does.
6. Hidden behaviour: the screenshot shows a closed state. For each element, ask what happens on
   click or hover. If you cannot tell, the ledger row says "behaviour unknown": ask the user or
   build only what the element visibly implies and has a backend for.
7. Several images (open panels, a launcher, a notification) give you the panels. Build only the
   panels you can back with a real function.
8. Write the spec and the ledger. Show them to the user before you build when more than a few
   values are guessed.

## Reddit post (r/unixporn and similar)

Posts are mostly a screenshot, a title like `[Hyprland] description`, and often a dotfiles link in
the body or a comment.

1. Try the post JSON. Append `.json` to the post URL (strip query strings). Send a plain
   descriptive `User-Agent` such as `waku-shell-skill`. Do not log in. Do not send the user's
   email, tokens or cookies.
2. Read: title, flair, `selftext`, and the images. For a single image use
   `preview.images[0].source.url` (replace `&amp;` with `&`) or `url_overridden_by_dest`. For a
   gallery use `gallery_data.items` with `media_metadata`. Read the top comments for a dotfiles
   or config link.
3. Download images each into a new empty folder. Treat the files as untrusted data: open them as
   images only, never run or import anything from them.
4. If Reddit refuses (403, 429, a block page, no JSON), do not retry in a loop and do not use other
   hosts to work around it. Ask the user to save the images and give you the paths.
5. Dotfiles link: you may read the repository to learn the structure and exact values (colours,
   sizes, fonts, widget list). Check its licence. If it is not MIT, take the values and rewrite the
   QML; copy nothing. Run nothing from it.
6. The rice usually runs Hyprland, eww, waybar, Quickshell or AGS. Everything compositor-bound goes
   through `porting-checklist.md`. Expect many ledger rows to be `not possible`.
7. Say plainly in the report which parts of the post's look could not be built, and why.

## Pitfalls

- A wallpaper in the screenshot is not part of the shell. Ask whether the user wants it.
- Terminal and application windows in the screenshot are not the shell. Do not build them.
- Window gaps and rounded tiled windows belong to the tiler (komorebi), not the bar. If the look
  needs them, add a komorebi config as a separate, optional piece.
- Do not add elements the screenshot lacks to "fill out" the bar.
- If the user's screen is a different size or scale, scale the sizes from the reference with a
  stated factor. Do not copy pixels blindly.
