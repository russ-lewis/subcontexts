# Description
This is a small library for parsing linux maps files.

# Usage
Parse a maps file with
```
Map *Map_parse(int pid);
```

Pass `pid = -1` to read `/proc/self/maps`.

To get the difference between two maps files use
```
Map *Map_diff(Map *a, Map *b);
```

And to print out a maps struct use
```
void Map_print(Map *map);
```
