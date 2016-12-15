#include <sys/nvm.h>
#include <sys/malloc.h>
//#include <arch/amd64/include/cpufunc.h> //This probably won't work. How you fence is machine dependent though!!! Need to figure out how to expose the mfence functionality of current processor.

//TODO: PKMALLOC. PKFREE. MAKE SURE H AND C AGREE. PROOFREAD.
//Write about it. FINISH THESE BY NOON.


//Figure out how to build, then figure out experiment. FINISH BY BEFORE 5
//leaves: 7 hours for debug, rewrite, running experiment.



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

    pkmalloc_state.ppol_and_large_list_lock = malloc(sizeof(__mp_lock), M_TEMP, M_WAITOK);
    __mp_lock_init(&pkmalloc_state.ppol_and_large_list_lock);

    
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
void * pkmalloc(size_t size) {
    if (pkmalloc_active != 1) {
        return NULL;
    }
    
    //ummmm yeah. we're storing allocation type and size info in the same number.
    uint32_t takenMask = 1 << 31; //0b1000000.....
    uint32_t freeMask = ~takenMask;
    
    //Figure out allocation type:
    if (size >= PKMALLOC_POOL_SIZE) {
        
        void *res = NULL;
        
        //lock top-level lock
        uint32_t entries_to_reserve = (size / PKMALLOC_POOL_SIZE) + ((size % PKMALLOC_POOL_SIZE == 0) ? 0 : 1);
        __mp_lock(pkmalloc_state.pool_and_large_list_lock);
        
        //pool allocation. Find next free chunk of pool list. -- this is similar at each level!
        if entries_to_reserve <= (pkmalloc_state.pool_map_len - pkmalloc_state.next_free_pool_map_entry) {
            pkmalloc_state.pool_map[pkmalloc_state.next_free_pool_map_entry] = entries_to_reserve | takenMask;
            res = pkmalloc_state.next_free_pool_map_entry * PKMALLOC_POOL_SIZE + NVM_COREMAP_END;
            pkmalloc_state.next_free_pool_map_entry += entries_to_reserve; //once it's as large as map len, only scanning succeeds.
            
            //we don't worry about setting the new entry to be the proper free amount because we know everything to end of array is free.
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
                    if (entries_to_reserve != (freeMask & pkmalloc_state_pool[cur_index])) {
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
        __mp_unlock(pkmalloc_state.pool_and_large_list_lock);
        
        return res;
    }
    else if (size >= PKMALLOC_LARGE_SIZE) {
        //find current pool: if exists, acquire lock and check for space.
        
        
        //if none, alloc pool (acquire/release top level lock)
        //if full, release lock, (acquire lock when entering pool, release when leaving) scan past pools.
            //If no space, alloc pool (or fail) (acquire/release top level lock)
        
        
        //Once: pool with space found / location found. mark location as taken
        
        //Return pointer to location
    }
}

void pkfree(void *data) {
    if (pkmalloc_active != 1) {
        return
    }
    //DON'T FORGET TO LOG AND LOCK
    
    //TODO
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
    __mp_lock_release(&pkpersist_lock);
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

