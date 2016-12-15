#include <sys/nvm.h>
#include <sys/malloc.h>

//TODO: LOCKS. PKMALLOC. PKFREE. MAKING SURE .H AND .C AGREE. PROOFREAD. FIGURE OUT WHAT ELSE IS NEEDED TO INCORPORATE INTO PROJECT

void pkmalloc_init(void) {
    int err;
    //stuff
    pkmalloc_lock = ?;
    //init structure
    
    err = pkrecover();
    if (err) {
        panic("pkrecover error");
    }
}

void nvm_init(void) {
    
    //set globals: logging stuff, pkmalloc lock (if necessary), allocate pkmalloc data structures, pkpersist lock, persist list.
    

#ifdef NVMLOGGING
    nvm_access_count = 0;
    nvm_logging_on = 0; //Should be turned on as experiment begins. Off as it ends (Can't log to disk because we get in a log-loop!
    
    //TODO nvmLogging Lock
#endif
    
    /* kern_sysctl.c line 2048: this is what we want.
     // Allocate wired memory to not block.
    kve = malloc(oldlen, M_TEMP, M_WAITOK);
    */
    
    //So we should be able to treat NVM_START as a 1GB long array of 8 byte entries
    
    NVM_START = malloc(NVM_SIZE, M_TEMP, M_WAITOK); // so this isn't really temp, but 8GB buff doesn't really fit in any malloc types.
    
    if (!NVM_START) {
        panic("PKAMALLOC INIT: malloc of nvm failed");
    }
    //Set up persist table:
    pkpersist_count = (long long unsigned *) NVM_START;
    *pkpersist_count = 0;
    
    pkpersist_lock = ?;
    
    pkpersist_list = (pkpersist_struct *) (NVM_START + 1); //TODO: increments by 8?
    
    //calls pkrecover, which calls pkretrieve and pkregister
    pkmalloc_init();
}

void * pkmalloc(size_t size) {
    //TODO
}

void pkfree(void *data) {
    //TODO
}

int pkpersist(unsigned id, void *data, size_t size) {
    //TODO: locking
    int err = 0;
    if (id != *pkpersist_count + 1 || id > MAX_COREMAP_ENTRIES) {
        err = 1;
    }
    else {
        *pkpersist_count += 1;
        pkpersist_list[id].data = data;
        pkpersist_list[id].size = size;
        
    }
    //TODO: unlock
    return err;
}

pkpersist_struct *pkretrieve(unsigned id) {
    if (id > *pkpersist_count) {
        return NULL;
    }
    else {
        return &pkpersist_list[id];
    }
}

int pkrecover(void) {
    return 0;
}

int pkregister(void *dta, size_t size) {
    return 0;
}

