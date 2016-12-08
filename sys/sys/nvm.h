#ifndef _SYS_NVM_H_
#define _SYS_NVM_H_

#define NVM_SIZE            (4 << 9) //4GB
#define NVM_START           (PAGE_SIZE * 10) //10th physical page. Placeholder. Need to interact with startup code and memory management: this swath of pages needs to be mapped as in use in kernel pagetable at startup
#define NVM_END             (NVM_SIZE + NVM_START) //4 GB of NVM
#define NVM_PERMANENT_END   (NVM_START + PAGE_SIZE) //one page for lookup table


/*
TODO: Convincingly fake NVM with limited support for fixed-location permanent
structures and basic memory management.

Reserve some swath of physical address space at startup for NVM.

Copy kernel malloc such that it works solely in NVM region

Define special fixed-place values/objects. (reserve some locations that
will contain pointers to permanent structures at system startup).

ex: NVM_START = 0x400
    NVM_END = 0x800
    NVM_PERMANENT_END = 0x500
    *NVM_START = pkmalloc(sizeof(permanent structure one))
    *(NVM_START + 8) = ...

Thus, recovering permanent structures only requires scanning this special region
for pointers. Perhaps make first entry "length", then other entries the actual
pointers. Dynamically adding entries is simply first allocating, then incrementing.

This is not an ideal programming model, in particular, pkmalloc in "real" nvm
would likely make objects permanent and recoverable by default. This scheme
is enough for our purposes however.

Side note: Only support one mounted filesystem at a time for now. Don't need to worry
about annoying but inconsequential bookkeeping to differentiate structures.

Comments I messaged eric about interface:

NVM_START: beginning of physical address swath reserved for "NVM" hackery.

NVM_PERMANENT_END: max address of an element in the permanent lookup table (more on this later)

NVM_END: end of physical address swath reserved for our "NVM"

pkmalloc(size_t): kernel malloc, operates within our NVM swath entirely (allocated memory *and* metadata -- in real life that may be a paradigm decision. We simply need something good enough for our limited scope project). It operates only between NVM_PERMANENT_END and NVM_END

NVM_START to NVM_PERMANENT_END is meant to act as a lookup table for structures that *must* be easily located after power loss. NVM_START is hardcoded, but the length of this lookup table can be dynamic by storing a length at NVM_START. Use would be something like:
*(NVM_START + sizeof(pointer)*(*NVM_START + 1)) = pkmalloc(sizeof(payload));

This should be enough to allow:

Allocating our "journal" in a way that can be instantly found on reset. This requires the journal retain reference to all buffs that haven't made it to disk, but we kinda need that anyway.

Plug and chug nvm -- once a decent (or even stupid since we're using it in a limited way) malloc clone works in the restricted physical space, we don't really need to think about nvm itself.

Instrumenting the "restart" should be easy, since the swath of RAM to check for goodies is in a fixed location.

Now that I think of it, we may want a reference to some pkmalloc data structures in the permanent lookup table. Unless you want to just not implement pkfree. But anything that can be *found* via chasing pointers in the permanent lookup table needs to have its pkmalloc metadata accessible or it can't be freed.

We may manage to punt or fake a solution to that, since again we aren't trying to come up with a complete, perfect paradigm.

*/


//when are the extra endifs necessary (see some other .h files)
#endif /* _KERNEL */
#endif /* _SYS_NVM_H_ */
