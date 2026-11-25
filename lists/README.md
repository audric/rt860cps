# Channel lists

Each `.csv` file here is one list offered by *Channels → Add ready-made channel lists…*.

To keep your own lists, put `.csv` files in your personal folder,
`~/.local/share/rt860cps/lists/` (the dialog has a button that opens it):

- a file with a new name adds a list;
- a file with the same name as one here replaces that list (copy it and edit the copy);
- a leading number and dash (`05-`) only sets the order and is not part of the name,
  so `pmr446.csv` replaces `04-pmr446.csv`.

Files in this folder are overwritten when the program is updated; files in your
personal folder are never touched.

## Format

```
# name: PMR446
# note: Licence-free 446 MHz in Europe, 16 analogue channels
name,rx,tx,tx_tone,rx_tone,mode,width,power
PMR 01,446.00625,,,,FM,Narrow,Low
R0 Somewhere,145.60000,145.00000,82.5,,FM,Wide,High
```

- `# name:` is the list name, also used for the zone (16 characters at most).
  `# note:` is the line shown under the lists. Other `#` lines are comments.
- The first row names the columns. Only `name` and `rx` are required; columns can be
  in any order, and separated by commas or semicolons.
- `rx`, `tx`: frequency in MHz. An empty `tx` means the same as `rx`.
- `tx_tone`, `rx_tone`: empty, a CTCSS tone such as `88.5`, or a DCS code such as
  `D023N` (normal) or `D023I` (inverted).
- `mode`: `FM`, `AM` or `SSB` (default `FM`). `width`: `Wide` or `Narrow` (default
  `Wide`). `power`: `Low` or `High` (default `Low`).
- Channel names are stored in 16 bytes; longer ones are cut.

Rows the program cannot use are left out, and the dialog says which and why.

## Regenerating the airport lists

`tools/gen_airports.py airports.csv airport-frequencies.csv lists` with the two files
from <https://ourairports.com/data/>.
