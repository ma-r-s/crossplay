[Install it](https://crossplay.ma-r-s.com/#get) · [which file, and by hand](https://github.com/ma-r-s/crossplay/blob/xteink/docs/install.md) · [earlier releases](https://github.com/ma-r-s/crossplay/blob/xteink/docs/release-notes.md)

### What is new in 1.13.5

- The app and game shelves are back to their proper row height, and every icon sits on its own row again. 1.13.4 made the rows shorter and walked each icon down into its neighbour until the last one dropped off the list.
- Mario, on v1.13.4: *"you really messed up the UI. Seems like you made each row on the app or game selector shorter (without reason) and broke all the icons and such."*
- Both halves are one bug, and the icons are a consequence of the rows.
- | | intended | v1.13.4 resolved |
- |---|---|---|
- | row height | `theme.rowHeight` = **62** | **56** |
- | row gap | `listRowGap` = **4** | **6** |
- | pitch | 66 | 62 |
- Wikipedia: show that a long article is opening, without adding to the wait

