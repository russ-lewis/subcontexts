
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <memory.h>
#include <sys/mman.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ucontext.h>

#include "subcontexts.h"
#include "subcontexts_control.h"
#include "subcontexts_registry.h"

#include "russ_common_code/c/russ_todo.h"
#include "russ_common_code/c/russ_int_typedefs_linux.h"
#include "russ_common_code/c/russ_locks.h"
#include "russ_common_code/c/russ_dumplocalmaps.h"
#include "russ_common_code/c/russ_dumplocalfilelist.h"



int   registry_post(char *name, void *sym);
void *registry_find(char *name);

sc_registry_funcs funcs = {
	post: registry_post,
	find: registry_find,
};



sc *registry_sc;

typedef struct registry_entry {
	struct registry_entry *next;
	char *name;
	void *sym;
} registry_entry;

russ_lock       lock;
registry_entry *list;



int subcontext_main(sc *mySc, int argc, char **argv)
{
	int rc;
	int i;


	/* we have to save this to a global because registry_post() needs
	 * to call sc_malloc().
	 */
	registry_sc = mySc;


	russ_lock_init(&lock);
	list = NULL;

	registry_post("subcontexts_registry:post(char*,void*)", &registry_post);
	registry_post("subcontexts_registry:find(char*)",       &registry_find);

	mySc->descriptive_name = "subcontexts registry";
	mySc->user_root        = &funcs;


	/* sanity check that it's all working OK */
	assert(subcontexts_registry_bootstrap() == &funcs);
	assert(funcs.find("subcontexts_registry:post(char*,void*)") == &registry_post);
	assert(funcs.find("subcontexts_registry:find(char*)"      ) == &registry_find);


	printf("Entering the infinite spin loop...\n");
	while (1)
	{
		sleep(2);

		printf("\n");

		printf("Locking the registry...");
		russ_lock_shared(&lock);
		printf("ok, lock.val=0x%08x\n", lock.val);

		printf("Our funcs:\n"
		       "  &registry_post = %p\n"
		       "  &registry_find = %p\n", registry_post, registry_find);

		printf("Walking the list:\n");

		registry_entry *entry = list;
		while (entry != NULL)
		{
			printf("  name='%s' sym=%p\n", entry->name, entry->sym);
			fflush(NULL);

			entry = entry->next;
		}

		printf("Unlocking the struct...");
		russ_unlock_shared(&lock);
		printf("ok, lock.val=0x%08x\n", lock.val);

		if (lock.val != 0)
			printf("    -- WARNING -- Nonzero lock value!\n");
	}


	/* we never get here */
	assert(false);
}



int registry_post(char *name, void *sym)
{
	if(registry_find(name) != NULL)
		return EINVAL;

	int name_len = strlen(name);

	registry_entry *newEntry = sc_malloc(registry_sc, sizeof(*newEntry) + name_len+1);
	  assert(newEntry != NULL);

	char *name_copy = (char*)(newEntry+1);
	  strcpy(name_copy, name);

	newEntry->name = name_copy;
	newEntry->sym  = sym;

	russ_lock_exclusive(&lock);
	  newEntry->next = list;
	  list           = newEntry;
	russ_unlock_exclusive(&lock);

	return 0;
}



void *registry_find(char *name)
{
	void *retval = NULL;

	russ_lock_shared(&lock);

	registry_entry *entry = list;
	while (entry != NULL)
	{
		if (strcmp(entry->name, name) == 0)
		{
			retval = entry->sym;
			break;
		}

		entry = entry->next;
	}

	russ_unlock_shared(&lock);

	return retval;
}


