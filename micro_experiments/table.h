typedef struct TableEntry 
{
    struct TableEntry *next; // pointer to the next entry
    char *page;              // pointer to a page in memory
} TableEntry;

typedef struct Table
{
    TableEntry *data;        // linked list of table entries
    int length;
} Table;
