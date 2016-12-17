#include <sys/nvm.h>
#include <sys/malloc.h>
//#include <arch/amd64/include/cpufunc.h> //This probably won't work. How you fence is machine dependent though!!! Need to figure out how to expose the mfence functionality of current processor.

//TODO: PKMALLOC. PKFREE. MAKE SURE H AND C AGREE. PROOFREAD.
//Write about it. FINISH THESE BY NOON.


//Figure out how to build, then figure out experiment. FINISH BY BEFORE 5
//leaves: 7 hours for debug, rewrite, running experiment.

//TODO: check that all locks do in fact get init-ed

//TODO: LONG LONG UNSIGNED to uint64_t


#define PKMALLOC_POOL_SIZE  (1 << 20) //1 MB
#define PKMALLOC_LARGE_SIZE (1 << 9) //1kB
#define PKMALLOC_SMALL_SIZE (64) // 64 Bytes
#define PKMALLOC_POOL_NUM   (NVM_SIZE - NVM_COREMAP_SIZE) / PKMALLOC_POOL_SIZE)

//TODO: we may want to add another check "should log" -- bascially checks if proc
//is part of experiment.
#ifdef NVMLOGGING
//Handles "is logging dynamically on logic" as well
void nvm_log_atomic_increment(unsigned amount){
    __mp_lock(&nvm_log_lock);
    if (nvm_logging_on) {
        *nvm_access_count += amount;
    }
    __mp_unlock(&nvm_log_lock);
}
#endif

void pkmalloc_init(void) {
    int err;

    //pool list stuff
    __mp_lock_init(&pkmalloc_state.pool_and_large_list_lock);
    
    pkmalloc_state.pool_map = malloc(4 * PKMALLOC_POOL_NUM, M_TEMP, M_WAITOK);
    if (!pkmalloc_state.pool_map) {
        panic("could not alloc pool map!\n");
    }
    pkmalloc_state.pool_map_len = PKMALLOC_POOL_NUM;
    pkmalloc_state.next_Free_pool_map_entry = 0;
    
    //large alloc stuff
    pkmalloc_state.large_pools = malloc(sizeof(subpool) * PKMALLOC_POOL_NUM, M_TEMP, M_WAITOK);
    if (!pkmalloc_state.large_pools) {
        panic("could not alloc large pools list! \n");
    }
    __mp_lock_init(&pkmalloc_state.large_pools_lock);
    pkmalloc_state.num_large_pools = 0;
    pkmalloc_state.next_free_large_entry = 0;
    
    
    //small alloc stuff
    pkmalloc_state.small_pools = malloc(sizeof(subpool) * PKMALLOC_POOL_NUM, M_TEMP, M_WAITOK);
    if (!pkmalloc_state.small_pools) {
        panic("could not alloc small pools list! \n");
    }
    __mp_lock_init(&pkmalloc_state.small_pools_lock);
    pkmalloc_state.num_small_pools = 0;
    pkmalloc_state.next_free_small_entry = 0;
    
    
    err = pkrecover();
    if (err) {
        panic("pkrecover error");
    }
}

void nvm_init(void) {
    //For now, don't log mem accesses. may need to for recovery measuring
    
    //set globals: logging stuff, pkmalloc lock (if necessary), allocate pkmalloc data structures, pkpersist lock, persist list.
    

#ifdef NVMLOGGING
    nvm_access_count = malloc(sizeof(long long unsigned), M_TEMP, M_WAITOK);
    if (!nvm_access_count) {
        panic("NVM_INIT: Could not allocate nvm_access_count!\n");
    }
    nvm_logging_on = 0; //Should be turned on as experiment begins. Off as it ends (Can't log to disk because we get in a log-loop!
    __mp_lock_init(&nvm_log_lock);
#endif
    
    /* kern_sysctl.c line 2048: this is what we want.
     // Allocate wired memory to not block.
    kve = malloc(oldlen, M_TEMP, M_WAITOK);
    */
    //So we should be able to treat NVM_START as a .5 GB long array of 16 byte entries
    
    //could fail, in which case we need to be clever at startup. But don't attempt to grab fixed V/P addrs because that'll take too long. <24 hours left.
    NVM_START = malloc(NVM_SIZE, M_TEMP, M_WAITOK); // so this isn't really temp, but 8GB buff doesn't really fit in any malloc types.
    
    if (!NVM_START) {
        panic("NVM_INIT: malloc of nvm failed!\n");
    }
    //Set up persist table:
    pkpersist_count = (long long unsigned *) NVM_START;
    *pkpersist_count = 0;
    
    __mp_lock_init(&pkpersist_lock);
    
    pkpersist_list = (pkpersist_struct *) (pkpersist_count + 1); //TODO: increments by 8?
    
    //calls pkrecover, which calls pkretrieve and pkregister
    pkmalloc_init();
    mfence();// (ensure people can't pkmalloc until persist table is indeend set up)
    pkmalloc_active = 1;
    
}

//optimizations: if we store distance to neighbors for a sequence of <1 block sized allocations right next to the size/taken marker, frees can be done in constant time! Leaves only the "scan for space" malloc as slow op.

//ASSUMES nvm is contiguous in kernel vm. Otherwise everything is slow and sucks.

//figure out how to handle free blocks of size one vs taken blocks of size 1 now!

//Coarse locking scheme so implementation stays sane multithreaded

//TODO: make sure "block numbers" are = to index into top level pool map:
void * pkmalloc(size_t size) {
    if (pkmalloc_active != 1) {
        return NULL;
    }
    
    //ummmm yeah. we're storing allocation type and size info in the same number.
    uint32_t takenMask = 1 << 31; //0b1000000.....
    uint32_t freeMask = ~takenMask;
    void *res = NULL;
    
    //Figure out allocation type:
    if (size >= PKMALLOC_POOL_SIZE) {
        
        //lock top-level lock
        uint32_t entries_to_reserve = (size / PKMALLOC_POOL_SIZE) + ((size % PKMALLOC_POOL_SIZE == 0) ? 0 : 1);
        __mp_lock(&pkmalloc_state.pool_and_large_list_lock);
        
        //pool allocation. Find next free chunk of pool list. -- this is similar at each level!
        if entries_to_reserve <= (pkmalloc_state.pool_map_len - pkmalloc_state.next_free_pool_map_entry) {
            pkmalloc_state.pool_map[pkmalloc_state.next_free_pool_map_entry] = entries_to_reserve | takenMask;
            res = pkmalloc_state.next_free_pool_map_entry * PKMALLOC_POOL_SIZE + NVM_COREMAP_END;
            pkmalloc_state.next_free_pool_map_entry += entries_to_reserve; //once it's as large as map len, only scanning succeeds.
            
            //we set the new entry with proper free amount to make "free" easier
            if (pkmalloc_state.next_free_pool_map_entry < pkmalloc_state.pool_map_len) {
                pkmalloc_state->pool_map[pkmalloc_state.next_free_pool_map_entry] = (pkmalloc_state.pool_map_len - pkmalloc_state.next_free_pool_map_entry) | freeMask;
            }
        }
        
        //if no next chunk, scan for open space.
        else {
            int cur_index = 0;
            //assumes: free space is mapped as "[free_entries, 0, 0....., taken_entries, 0,...,];
            //this means the [free_entries, 0,... taken, 0..., free, 0...] case of free is slow but whatevs
            while (cur_index <= pkmalloc_state.pool_map_len - entries_to_reserve) {
                //enough free space and not taken
                if ((pkmalloc_state.pool_map[cur_index] & freeMask) >= entries_to_reserve && !(pkmalloc_state.pool_map[cur_index] & takenMask)) {
                    
                    //Can take. Make new free marker, then update location as taken
                    if (entries_to_reserve != (freeMask & pkmalloc_state.pool_map[cur_index])) {
                        //add free entry after allocation
                        int index_to_edit = cur_index + entries_to_reserve;
                        pkmalloc_state.pool_map[index_to_edit] = (pkmalloc_state.pool_map[cur_index] - entries_to_reserve) | freeMask;
                    }
                    pkmalloc_state.pool_map[cur_index] = entries_to_reserve | takenMask;
                    res = cur_index * PKMALLOC_POOL_SIZE + NVM_COREMAP_END;
                    break;
                }
                //increment cur_index:
                cur_index += (pkmalloc_state.pool_map[cur_index] & freeMask);
            }
        }
        
        //unlock top-level lock
        __mp_unlock(&pkmalloc_state.pool_and_large_list_lock);
        
    }
    else if (size >= PKMALLOC_LARGE_SIZE) {
        
        uint32_t entries_to_reserve = (size / PKMALLOC_LARGE_SIZE) + ((size % PKMALLOC_LARGE_SIZE == 0) ? 0 : 1);
        __mp_lock(&pkmalloc_state.large_pools_lock);
        
        uint32_t large_subpool_entry_count = (PKMALLOC_POOL_SIZE / PKMALLOC_LARGE_SIZE);
        
        //Find current subpool.
        if (pkmalloc_state.num_large_pools > 0) {
            subpool *cur_subpool = &pkmalloc_state.large_pools[pkmalloc_state.num_large_pools - 1];
            uint32_t *cur_subpool_map = cur_subpool->subpool_map;
            
            //If there's enough space, great
            if (entries_to_reserve <= large_subpool_entry_count - pkmalloc_state.next_free_large_entry) {
                
                cur_subpool_map[pkmalloc_state.next_free_large_entry] = entries_to_reserve | takenMask;
                res = cur_subpool->pool_number * PKMALLOC_POOL_SIZE + pkmalloc_state.next_free_large_entry * PKMALLOC_LARGE_SIZE + NVM_COREMAP_END;
                pkmalloc_state.next_free_large_entry += entries_to_reserve; //once it's as large as map len, only scanning succeeds.
                
                //we set the new entry with proper free amount to make "free" easier
                if (pkmalloc_state.next_free_large_entry < large_subpool_entry_count) {
                    cur_subpool->subpool_map[pkmalloc_state.next_free_large_entry] = (large_pool_entry_count - pkmalloc_state.next_free_large_entry) | freeMask;
                }
            }
            //Otherwise, scan all "large" pools
            //per pool
            for(unsigned i = 0; i < pkmalloc_state.num_large_pools; i ++) {
                //do the scan
                subpool *cursubpool = &pkmalloc_state.large_pools[i];
                uint32_t *cursubpoolmap = cursubpool->subpool_map;
                unsigned curindex = 0;
                while (curindex <= large_subpool_entry_count - entries_to_reserve) {
                    //enough free space and not taken
                    if ((cursubpoolmap[curindex] & freeMask) >= entries_to_reserve && !(cursubpoolmap[curindex] & takenMask)) {
                        
                        //Can take. Make new free marker, then update location as taken
                        if (entries_to_reserve != (freeMask & cursubpool[curindex])) {
                            //add free entry after allocation
                            unsigned index_to_edit = curindex + entries_to_reserve;
                            curpoolmap[index_to_edit] = (curpoolmap[curindex] - entries_to_reserve) | freeMask;
                        }
                        //claim spot
                        cursubpool[curindex] = entries_to_reserve | takenMask;
                        res = cursubpool->pool_number * PKMALLOC_POOL_SIZE + curindex * PKMALLOC_LARGE_SIZE + NVM_COREMAP_END;
                        break;
                    }
                    //increment curindex:
                    curindex += (cursubpoolmap[curindex] & freeMask);
                }
            }
            
        }
        //todo: check that this copy paste logic is fine
        if (res == NULL) { //checks that we didn't succeed previous step
            
            //Attempt to reserve a pool at top level: (won't be freed ever)
            res = pkmalloc(PKMALLOC_POOL_SIZE);
            
            //there's enough space, great
            //Res is correct value
            //check all this logic
            if (res != NULL) {
                //GOTTA ALLOCATE SOME STRUCTURES.
                //FIND
                
                //reverse engineer block num:
                uint32_t pool_num = (((uint32) res) - NVM_COREMAP_END) / PKMALLOC_POOL_SIZE;
                pkmalloc_state.large_pools[pkmalloc_state.num_large_pools].pool_number = pool_num;
                pkmalloc_state.large_pools[pkmalloc_state.num_large_pools].subpool_map = malloc(large_subpool_entry_count * 4, M_TEMP, M_WAITOK);
                if (!pkmalloc_state.large_pools[num_large_pools].subpool_map) {
                    panic("couldn't alloc a large subpool map \n");
                }
                
                //set free entry
                //we set the new entry with proper free amount to make "free" easier
                pkmalloc_state.large_pools[pkmalloc_state.num_large_pools].subpool_map[entries_to_reserve] = (large_pool_entry_count - entries_to_reserve) | freeMask;
                
                
                pkmalloc_state.num_large_pools += 1;
                pkmalloc_state.next_free_large_entry = entries_to_reserve;
                
                
            }
        }
        
        __mp_unlock(&pkmalloc_state.large_pools_lock);
    }
    
    else { //small allocation. Exactly the same as PKMALLOC_LARGE_SIZE, but different locks and arrays.
        uint32_t entries_to_reserve = (size / PKMALLOC_SMALL_SIZE) + ((size % PKMALLOC_SMALL_SIZE == 0) ? 0 : 1);
        __mp_lock(&pkmalloc_state.small_pools_lock);
        
        uint32_t small_subpool_entry_count = (PKMALLOC_POOL_SIZE / PKMALLOC_SMALL_SIZE);
        
        //Find current subpool.
        if (pkmalloc_state.num_small_pools > 0) {
            subpool *cur_subpool = &pkmalloc_state.small_pools[pkmalloc_state.num_small_pools - 1];
            uint32_t *cur_subpool_map = cur_subpool->subpool_map;
        
            //If there's enough space, great
            if (entries_to_reserve <= small_subpool_entry_count - pkmalloc_state.next_free_small_entry) {
                
                cur_subpool_map[pkmalloc_state.next_free_small_entry] = entries_to_reserve | takenMask;
                res = cur_subpool->pool_number * PKMALLOC_POOL_SIZE + pkmalloc_state.next_free_small_entry * PKMALLOC_SMALL_SIZE + NVM_COREMAP_END;
                pkmalloc_state.next_free_small_entry += entries_to_reserve; //once it's as large as map len, only scanning succeeds.
                
                //we set the new entry with proper free amount to make "free" easier
                if (pkmalloc_state.next_free_small_entry < small_subpool_entry_count) {
                    cur_subpool->subpool_map[pkmalloc_state.next_free_small_entry] = (small_pool_entry_count - pkmalloc_state.next_free_small_entry) | freeMask;
                }
            }
        
            //Otherwise, scan all "small" pools
            //per pool
            for(unsigned i = 0; i < pkmalloc_state.num_small_pools; i ++) {
                //do the scan
                subpool *cursubpool = &pkmalloc_state.small_pools[i];
                uint32_t *cursubpoolmap = cursubpool->subpool_map;
                unsigned curindex = 0;
                while (curindex <= small_subpool_entry_count - entries_to_reserve) {
                    //enough free space and not taken
                    if ((cursubpoolmap[curindex] & freeMask) >= entries_to_reserve && !(cursubpoolmap[curindex] & takenMask)) {

                        //Can take. Make new free marker, then update location as taken
                        if (entries_to_reserve != (freeMask & cursubpool[curindex])) {
                            //add free entry after allocation
                            unsigned index_to_edit = curindex + entries_to_reserve;
                            curpoolmap[index_to_edit] = (curpoolmap[curindex] - entries_to_reserve) | freeMask;
                        }
                        //claim spot
                        cursubpool[curindex] = entries_to_reserve | takenMask;
                        res = cursubpool->pool_number * PKMALLOC_POOL_SIZE + curindex * PKMALLOC_SMALL_SIZE + NVM_COREMAP_END;
                        break;
                    }
                    //increment curindex:
                    curindex += (cursubpoolmap[curindex] & freeMask);
                }
            }
        
        }
        if (res == NULL) { //checks that we didn't succeed previous step
        
            //Attempt to reserve a pool at top level: (won't be freed ever)
            res = pkmalloc(PKMALLOC_POOL_SIZE);
        
            //there's enough space, great
            //Res is correct value
            //check all this logic
            if (res != NULL) {
                //GOTTA ALLOCATE SOME STRUCTURES.
                //FIND
                
                //reverse engineer block num:
                uint32_t pool_num = (((uint32) res) - NVM_COREMAP_END) / PKMALLOC_POOL_SIZE;
                pkmalloc_state.small_pools[pkmalloc_state.num_small_pools].pool_number = pool_num;
                pkmalloc_state.small_pools[pkmalloc_state.num_small_pools].subpool_map = malloc(small_subpool_entry_count * 4, M_TEMP, M_WAITOK);
                if (!pkmalloc_state.small_pools[num_small_pools].subpool_map) {
                    panic("couldn't alloc a small subpool map \n");
                }
                
                
                //set free entry
                //we set the new entry with proper free amount to make "free" easier
                pkmalloc_state.small_pools[pkmalloc_state.num_small_pools].subpool_map[entries_to_reserve] = (small_pool_entry_count - entries_to_reserve) | freeMask;
                
                
                pkmalloc_state.num_small_pools += 1;
                pkmalloc_state.next_free_small_entry = entries_to_reserve;
                
            }
        }
        
        __mp_unlock(&pkmalloc_state.small_pools_lock);
    }
    
    return res;
}


//Cases: beginning of list (no left traverse)
//       between two allocations (left and right traverse)
//       Most recent allocation in incomplete pool (can stack with first case)

//For things under "pkmalloc_large_size"
void pkfree_small(void *data) {
    
    
    uint32_t takenMask = 1 << 31; //0b1000000.....
    uint32_t freeMask = ~takenMask;
    
    uint32_t small_subpool_entry_count = (PKMALLOC_POOL_SIZE / PKMALLOC_SMALL_SIZE);
    uint32_t pool_small_index = 0;

    
    if (pkmalloc_active != 1) {
        return;
    }
    //Grab "small" lock
    __mp_lock(&pkmalloc_state.small_pools_lock);
    
    //arithmetic to calculate pool number, entry
    uint32_t pool_number = (data - NVM_COREMAP_END) / PKMALLOC_POOL_SIZE;
    uint32_t submap_entry = (data - NVM_COREMAP_END - pool_number * PKMALLOC_POOL_SIZE) / PKMALLOC_SMALL_SIZE;
    subpool *cur_subpool;
    
    //scan for pool:
    for (unsigned i = 0; i < pkmalloc_state.num_small_pools; i++) {
        if (pkmalloc_state.small_pools[i].pool_number == pool_number) {
            cur_subpool = &pkmalloc_state.small_pools[i];
            pool_small_index = i;
            break;
        }
        if (i == pkmalloc_state.num_small_pools) {
            panic("invalid small free\n");
        }
    }
    //jump to offset
    uint32_t curEntryValue = cur_subpool->subpool_map[submap_entry];
    uint32_t curEntry = submap_entry;
    if (curEntryValue == 0) {
        panic("invalid small free\n");
    }
    
    //Check to the right (if not last entry):
    if (curEntry + (curEntryValue & freeMask) < small_subpool_entry_count) {
        uint32_t nextEntryIndex = curEntry + (curEntryValue & takenMask);
        uint32_t nextEntryValue = cur_subpool->subpool_map[nextEntryIndex];
        
        //Free:
        if (!(nextEntryValue & takenMask)) {
            //Merge entries into current location
            cur_subpool->subpool_map[curEntry] += nextEntryValue;
            cur_subpool->subpool_map[curEntry] &= freeMask;
            cur_subpool->subpool_map[nextEntryIndex] = 0;
            
            
            //Was cur-entry most recent allocation in most recent pool?
            if (nextEntryIndex == pkmalloc_state.next_free_small_entry && pool_small_index + 1 == pkmalloc_state.num_small_pools) {
                //Update next_free_X_entry
                pkmalloc_state.next_free_small_entry = curEntry;
            }
        }
        //Alloced:
            //nothing to do
    }
    if (curEntry > 0) {
        //traverse backwards to find preceeding entry
        uint32_t traverse = curEntry - 1;
        
        //should not overflow
        while(cur_subpool->subpool_map[traverse] == 0) {
            traverse -= 1;
        }
        uint32_t prevEntryIndex = traverse;
        uint32_t prevEntryValue = cur_subpool->subpool_map[prevEntryIndex];
        

        //Free
        if (!(prevEntryValue & takenMask)) {
            //Merge entries into left location
            cur_subpool->subpool_map[prevEntry] = prevEntryValue + (curEntryValue & freeMask);
            cur_subpool->subpool_map[curEntry] = 0;
            
            
            //Was cur-entry most recent allocation in most recent pool?
            if (curEntry == pkmalloc_state.next_free_small_entry && pool_small_index + 1 == pkmalloc_state.num_small_pools) {
                //Update next_free_X_entry
                pkmalloc_state.next_free_small_entry = prevEntry;
            }
        }
        //Alloced
            //Nothing to do.
    }
    
    __mp_unlock(&pkmalloc_state.small_pools_lock);
}

//For things "pkmalloc_large_size" to "pkmalloc_pool_size" (exclusive of upper bound)
void pkfree_medium(void * data) {
    
    
    uint32_t takenMask = 1 << 31; //0b1000000.....
    uint32_t freeMask = ~takenMask;
    
    uint32_t large_subpool_entry_count = (PKMALLOC_POOL_SIZE / PKMALLOC_LARGE_SIZE);
    uint32_t pool_large_index = 0;
    
    
    if (pkmalloc_active != 1) {
        return;
    }
    //Grab "large" lock
    __mp_lock(&pkmalloc_state.large_pools_lock);
    
    //arithmetic to calculate pool number, entry
    uint32_t pool_number = (data - NVM_COREMAP_END) / PKMALLOC_POOL_SIZE;
    uint32_t submap_entry = (data - NVM_COREMAP_END - pool_number * PKMALLOC_POOL_SIZE) / PKMALLOC_LARGE_SIZE;
    subpool *cur_subpool;
    
    //scan for pool:
    for (unsigned i = 0; i < pkmalloc_state.num_large_pools; i++) {
        if (pkmalloc_state.large_pools[i].pool_number == pool_number) {
            cur_subpool = &pkmalloc_state.large_pools[i];
            pool_large_index = i;
            break;
        }
        if (i == pkmalloc_state.num_large_pools) {
            panic("invalid large free (pkfree_medium) \n");
        }
    }
    //jump to offset
    uint32_t curEntryValue = cur_subpool->subpool_map[submap_entry];
    uint32_t curEntry = submap_entry;
    if (curEntryValue == 0) {
        panic("invalid large free (pkfree_medium) \n");
    }
    
    //Check to the right (if not last entry):
    if (curEntry + (curEntryValue & freeMask) < large_subpool_entry_count) {
        uint32_t nextEntryIndex = curEntry + (curEntryValue & takenMask);
        uint32_t nextEntryValue = cur_subpool->subpool_map[nextEntryIndex];
        
        //Free:
        if (!(nextEntryValue & takenMask)) {
            //Merge entries into current location
            cur_subpool->subpool_map[curEntry] += nextEntryValue;
            cur_subpool->subpool_map[curEntry] &= freeMask;
            cur_subpool->subpool_map[nextEntryIndex] = 0;
            
            
            //Was cur-entry most recent allocation in most recent pool?
            if (nextEntryIndex == pkmalloc_state.next_free_large_entry && pool_large_index + 1 == pkmalloc_state.num_large_pools) {
                //Update next_free_X_entry
                pkmalloc_state.next_free_large_entry = curEntry;
            }
        }
        //Alloced:
        //nothing to do
    }
    if (curEntry > 0) {
        //traverse backwards to find preceeding entry
        uint32_t traverse = curEntry - 1;
        
        //should not overflow
        while(cur_subpool->subpool_map[traverse] == 0) {
            traverse -= 1;
        }
        uint32_t prevEntryIndex = traverse;
        uint32_t prevEntryValue = cur_subpool->subpool_map[prevEntryIndex];
        
        
        //Free
        if (!(prevEntryValue & takenMask)) {
            //Merge entries into left location
            cur_subpool->subpool_map[prevEntry] = prevEntryValue + (curEntryValue & freeMask);
            cur_subpool->subpool_map[curEntry] = 0;
            
            
            //Was cur-entry most recent allocation in most recent pool?
            if (curEntry == pkmalloc_state.next_free_large_entry && pool_large_index + 1 == pkmalloc_state.num_large_pools) {
                //Update next_free_X_entry
                pkmalloc_state.next_free_large_entry = prevEntry;
            }
        }
        //Alloced
        //Nothing to do.
    }
    
    __mp_unlock(&pkmalloc_state.large_pools_lock);
    
    
}

//use when freeing things "pkmalloc_pool_size" or larger
void pkfree_large(void * data) {
    uint32_t takenMask = 1 << 31; //0b1000000.....
    uint32_t freeMask = ~takenMask;
    
    uint32_t poolmap_entry_count = PKMALLOC_POOL_NUM;
    uint32_t pool_top_index = 0;
    
    
    if (pkmalloc_active != 1) {
        return;
    }
    //Grab "large" lock
    __mp_lock(&pkmalloc_state.pool_and_large_list_lock);
    
    //arithmetic to calculate pool number, entry
    uint32_t pool_number = (data - NVM_COREMAP_END) / PKMALLOC_POOL_SIZE;
    if ((data - NVM_COREMAP_END) % PKMALLOC_POOL_SIZE != 0) {
        panic("invalid largest free! \n")
    }
    

    //jump to offset
    uint32_t curEntryValue = pkmalloc_state.pool_map[pool_number];
    uint32_t curEntry = pool_number;
    if (curEntryValue == 0) {
        panic("invalid largest free! \n");
    }
    
    //Check to the right (if not last entry):
    if (curEntry + (curEntryValue & freeMask) < poolmap_entry_count) {
        uint32_t nextEntryIndex = curEntry + (curEntryValue & takenMask);
        uint32_t nextEntryValue = pkmalloc_state.pool_map[nextEntryIndex];
        
        //Free:
        if (!(nextEntryValue & takenMask)) {
            //Merge entries into current location
            pkmalloc_state.pool_map[curEntry] += nextEntryValue;
            pkmalloc_state.pool_map[curEntry] &= freeMask;
            pkmalloc_state.pool_map[nextEntryIndex] = 0;
            
            
            //Was cur-entry most recent allocation?
            if (nextEntryIndex == pkmalloc_state.next_free_pool_map_entry) {
                //Update next_free_X_entry
                pkmalloc_state.next_free_pool_map_entry = curEntry;
            }
        }
        //Alloced:
        //nothing to do
    }
    if (curEntry > 0) {
        //traverse backwards to find preceeding entry
        uint32_t traverse = curEntry - 1;
        
        //should not overflow
        while(pkmalloc_state.pool_map[traverse] == 0) {
            traverse -= 1;
        }
        uint32_t prevEntryIndex = traverse;
        uint32_t prevEntryValue = pkmalloc_state.pool_map[prevEntryIndex];
        
        
        //Free
        if (!(prevEntryValue & takenMask)) {
            //Merge entries into left location
            pkmalloc_state.pool_map[prevEntry] = prevEntryValue + (curEntryValue & freeMask);
            pkmalloc_state.pool_map[curEntry] = 0;
            
            
            //Was cur-entry most recent allocation?
            if (curEntry == pkmalloc_state.next_free_pool_map_entry) {
                //Update next_free_X_entry
                pkmalloc_state.next_free_pool_map_entry = prevEntry;
            }
        }
        //Alloced
        //Nothing to do.
    }
    
    __mp_unlock(&pkmalloc_state.pool_and_large_list_lock);
}
int pkpersist(unsigned id, void *data, size_t size) {

    int err = 0;
    
    __mp_lock(&pkpersist_lock);
    
#ifdef NVMLOGGING
    //shouldn't deadlock since no locks are acquired while holding the log lock.
    nvm_log_atomic_increment(1);
#endif
    
    if (id != *pkpersist_count + 1 || id > MAX_COREMAP_ENTRIES) {
        err = 1;
    }
    else {
#ifdef NVMLOGGING
        nvm_log_atomic_increment(3);
#endif
        pkpersist_list[id].data = data;
        pkpersist_list[id].size = size;
        mfence();
        *pkpersist_count += 1;
        
    }
    __mp_unlock(&pkpersist_lock);
    return err;
}
//Does not lock -- assumes somewhat sane, single threaded recovery.
//Can add synchro but shouldn't matter as long as threads aren't persisting
//things while we are still de-crufting the nvm.
pkpersist_struct *pkretrieve(unsigned id) {
#ifdef NVMLOGGING
    nvm_log_atomic_increment(1);
#endif
    if (id > *pkpersist_count) {
        return NULL;
    }
    else {
#ifdef NVMLOGGING
        nvm_log_atomic_increment(1);
#endif
        return &pkpersist_list[id];
    }
}

int pkrecover(void) {
    return 0;
}

int pkregister(void *dta, size_t size) {
    return 0;
}

