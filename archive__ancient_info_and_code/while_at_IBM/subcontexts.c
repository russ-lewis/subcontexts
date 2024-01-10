void sc_unmap_mapping(subcontext_mapping *mapping) {
  list_head remaining_to_unmap;

  if(!list_empty(&mapping->parent_mappings[0].listelem)) {
    list_del(&mapping->parent_mappings[0].listelem);
    list_del(&mapping->parent_mappings[1].listelem);
  }

  TODO: check arg order here
  list_add(&remaining_to_unmap, &mapping->parent_mapping[0].listelem);

  do {
    subcontext_mapping *cur = list_entry(remaining_to_unmap.next,
                                         subcontext_mapping,
                                         parent_mapping[0].listelem);

    TODO: do unmapping work here

    while(!list_empty(&cur->child_mappings)) {
      subcontext_mapping_listelem *curchild =
                                list_entry(cur->child_mappings.next,
                                           subcontext_mapping_listelem,
                                           listelem);
      list_del(cur->child_mappings.next);

      list_del(&curchild->mapping->parent_mappings[0].listelem);
      list_del(&curchild->mapping->parent_mappings[1].listelem);

      TODO: chcek arg order here
      list_add(&remaining_to_unmap, &curchild->mapping->parent_mappings[0].listelem);
    };
  } while(!list_empty(&remaining_to_unmap);
}



/* This code dedicated to the memory of Jeshua */

