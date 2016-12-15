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
    //stuff
    __mp_lock_init(&pkmalloc_lock);
    //init structure
    
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

void * pkmalloc(size_t size) {
    if (pkmalloc_active != 1) {
        return NULL;
    }
    
    
    //DON'T FORGET TO LOG AND LOCK
    //TODO
    
    //hey look! doesn't ever touch nvm!!
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

