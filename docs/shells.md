## Notes for shell authors

Short notes on what differs from Quickshell when you write a shell for wabar. The design is in
`docs/architecture.md`.

### Input regions (`mask`)

`PanelWindow` and `FloatingWindow` take a `mask: Region`, as in Quickshell.

```qml
PanelWindow {
    id: win
    anchors { top: true; bottom: true; left: true; right: true }
    mask: Region {
        x: 40; y: 0; width: win.width - 40; height: win.height
        intersection: Intersection.Xor
        regions: panelRegions.instances        // an Instantiator of Region { ... }
    }
}
```

- `Region` has `x`, `y`, `width`, `height` (window coordinates), `item` (the item's bounds, which
  follow the item), `shape` (`RegionShape.Rect` or `Ellipse`), `intersection` (`Combine`,
  `Intersect`, `Xor`, `Subtract`) and child regions (the default property, `regions`). Children are
  folded into their parent in order.
- `mask: Region {}` makes the window fully click-through. No `mask` means the whole window takes
  input. The `intersection` of the mask itself combines it with the window rectangle, as in
  Quickshell: `Combine` (the default) uses the mask alone; `Intersect`, `Xor` and `Subtract` give
  `window <op> mask`. In the example above the window takes input everywhere except inside the
  inner area (minus the open panels), which is how a bar plus its borders plus open drawers is
  masked.
- `PopupWindow` has no `mask`.
- The mask is enforced by polling the cursor at 120 Hz and making the window transparent for input
  outside the region, padded by 6 px. Keep two things in mind:
  - A click within 8 ms of the cursor reaching a region you did not pad can hit the window below.
    Give targets a few pixels of slack.
  - Hover and drag effects outside the region do not run: the window gets no mouse events there.
- Nothing is enforced on the offscreen platform or in tests.

### Variable fonts (`--font-engine`)

wabar starts with Qt's GDI font engine, which keeps memory low but ignores variable-font axes
(`font.variableAxes`). A shell that needs axes starts wabar with `--font-engine=directwrite`, at
the cost of about 2 MB more working set. A `QT_QPA_PLATFORM` set by the caller wins over the flag.
