#ifndef __SUBCONTEXTS_H_
#define __SUBCONTEXTS_H_

struct subcontext {
  TODO: more fields here, of course

  list_head mappings;
};



struct subcontext_mapping;
struct subcontext_mapping_listelem {
  list_head listelem;
  struct subcontext_mapping *mapping;
};



typedef u8 subcontext_mapping_perm;
#define SC_MAP_READ  1
#define SC_MAP_WRITE 2
#define SC_MAP_EXEC  3

struct subcontext_mapping {
  /* If this is a subcontext mapping, then you have READ and/or WRITE
   * permissions in 'perm' and start_addr = (void*)-1
   *
   * If this is a point entry point, then perm equals exactly EXEC, and
   * start_addr = end_addr = the entry point
   *
   * If this is a range entry point, then perm equals exactly EXEC, and
   * start_addr < end_addr
   *
   * If this is a total entry point, then perm equals exactly EXEC, and
   * start_addr = (void*)-1
   *
   * If this is the combination of a mapping AND a total entry point, then
   * perm equals EXEC plus one or both of READ and WRITE, and
   * start_addr = (void*)-1
   */
  struct subcontext *mapped_sc;
  subcontext_mapping_perm perm;
  void *start_addr;
  void *end_addr;

  list_head child_mappings; // mappings that need to be expired if this is
                            // expired.
  struct subcontext_mapping_listelem parent_mappings[2];
};

#endif

/* This code dedicated to the memory of Jeshua */

