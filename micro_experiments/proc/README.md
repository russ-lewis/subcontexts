# Description
This is a small library for parsing linux maps files.

# Usage
Parse a maps file with
```
MemMap *MemMap_parse(int pid);
```

Pass `pid = -1` to read `/proc/self/maps`.

To get the difference between two maps files use
```
MemMap *MemMap_diff(MemMap *a, MemMap *b);
```

And to print out a maps struct use
```
void MemMap_print(MemMap *map);
```
