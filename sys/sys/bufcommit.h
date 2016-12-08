#ifndef _SYS_BUFCOMMIT_H_
#define _SYS_BUFCOMMIT_H_

//Possible unecessary
typdef enum bufTransType {
    type1,
    type2
} bufTransType;

/*
Do we even need a "begin transaction"

Using it might make later code simpler: never need implicitly check if a
transaction has begun then begin it. If VOP code detects it's on a thread
that doesn't have a live transaction, we know it's some non-syscall hackery
that we can ignore. That's nice.

possibly not. It'd probably introduce an extra memfence.

*BUT* we should design the interface that makes finishing fastest -- instead
of a slick implementation, go for a design that allows an okay one to get done
quickly.
*/

//returns 1 on error, 0 on success. id should be thread (proc) id
int beginBufTransaction(u_int64_t id, int TYPE);

//returns 1 on error, 0 on success. id should be thread (proc) id
int commitBufTransaction(u_int64_t id);


#endif /* _KERNEL */
#endif /* _SYS_BUFCOMMIT_H_ */
