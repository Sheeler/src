#ifndef _SYS_NVM_H_
#define _SYS_NVM_H_

#include <sys/mp_lock.h>

//Everything is assumed to be 64 bit because YOLO!!! :'D

typdef struct pkpersist_struct {
    size_t data_size; //4bytes or 8bytes.
    void *data;
} pkpersist_struct;

typedef struct pkmalloc_state_struct {
    __mp_lock *pool_and_large_list_lock;
    pool_map; //array of uint32_t's
    next_free_pool_map_entry; //unsigned
    pool_map_len; //unsigned
    
    
} pkmalloc_state;


//These will likely change depending on exact ram stealing scheme.
#define NVM_SIZE            (8 << 9) //8 GB
#define NVM_PAGES           (NVM_SIZE / PAGE_SIZE) //If 48B is not a multiple of page size your system is fucked up already.
//#define NVM_START           (4 << 9) //Beginning of 5th physical GB
void * NVM_START;
#define NVM_START_PN        (NVM_START / PAGE_SIZE) //zero-indexed cause computers

#define NVM_END             (NVM_SIZE + NVM_START) //8 GB of NVM. THIS ADDRESS IS *NOT* IN NVM
#define NVM_FINAL_PAGE      (NVM_SIZE - PAGE_SIZE) //Address of last page
#define NVM_FINAL_PN        (NVM_FINAL_PA / PAGE_SIZE) //again, zero indexed

#define NVM_COREMAP_ADDRESS (NVM_START)
#define NVM_COREMAP_PN      (NVM_COREMAP_ADDRESS / PAGE_SIZE) //ZERO INDEXING YAY
#define NVM_COREMAP_SIZE    (PAGE_SIZE) //We can add some fancy "final page" math for this if  this grows to be more than one page but it won't in this toy implementation and also I'm not sure those defines are even that useful but here we are it's 1am what am I doing
#define NVM_COREMAP_END     (NVM_COREMAP_ADDRESS + PAGE_SIZE) //one page for lookup table
                            //This may change to be a map/bitmap of this chunk of memory divided into
                            //some N units (min journal record size, for example)
#define MAX_COREMAP_ENTRIES ((NVM_COREMAP_SIZE - sizeof(long long unsigned)) / sizeof(pkpersist_struct)) //subtract one long long int.
#ifdef NVMLOGGING
long long unsigned * nvm_access_count;
int nvm_logging_on;
struct __mp_lock nvm_log_lock;
void nvm_log_atomic_increment(unsigned amount);
#endif


//WE ASSUME #ifdef MULTIPROCESSOR
struct __mp_lock pkpersist_lock;
pkpersist_struct *pkpersist_list;
long long unsigned *pkpersist_count;

struct __mp_lock pkmalloc_lock;
int pkmalloc_active;
pkmalloc_state_struct pkmalloc_state;



void nvm_init(void);

/*
Allocates memory in NVM. "Recovering" data after crash is left
To caller using pkpersist/pkretrieve/pkregister interface. Returns NULL on failure.
*/
void * pkmalloc(size_t size);

/*
Free memory in NVM. Memory not accessible from a pkpersist'ed pointer
Is considered free after a power loss or crash.
Simple implementation. Don't abuse it.
*/
void pkfree(void *data);

/*
Stores data pointer in a fixed-location table indexed by ID.
Currently: id is index into an array. Better schemes or simple hashes
may exist but are unecessary. Data cannot be "de-persisted" currently.
Returns 1 on failure (id too big, table full), 0 on success.
Note id needs to be hardcoded or determinsitcally generated to
allow retrieval.
*/
int pkpersist(unsigned id, void *data, size_t size);

/*
Retrieve the object pointer at index id in the persistent lookup table.
 
Returned structure is pre-allocated. Do not attempt to free.
*/
pkpersist_struct * pkretrieve(unsigned id);


/*
Use during pkmalloc recovery. Callers of pkpersist must implement a recovery scheme that
registers all addresses accessible from a pkpersist-ed location. pkregister-ing each address and size
allows pkmalloc to restore its DRAM data structures without expensive consistency schemes bottlenecking
pkmalloc.
 
This is preferable to a generic scheme since the caller of pkpersist has specialized knowledge of the
structure they are persisting, and can likely walk the structure efficiently. Not pkregistering allows that
memory to be re-allocated, borking the kernel.
 
Returns 0 on success and 1 on failure
*/
int pkregister(void *data, size_t size);

/* 
Called after VM is running after a crash. Restores pkmalloc's data structures to working order.
Requires users of pkpersist to implement recovery for their persisted structures.
 
Returns 0 on success and 1 on failure (NVM besides objects directly pkpersist-ed are effectively lost).
This should only happen if memory is exhausted or a caller recovery is incorrect.
*/
int pkrecover(void);







//Make sure your window is at least 100 characters wide for this




/* PK MAAALLLLOOOOOCCCC

oddddddddddddhdddddddddy`hdddddddydyyhdddddhhyhd: .hddddddh+hdddddddddddddddddddddddddddddodddddhyyy.oyh-:dddddddddddddd/hhddddddddddddddddyhoydhyyso/
yMMMMMMMMMMNmMMMMMMMMMm-dMMMMMNhmMmdNMMMMmdmdmN: :NMMMMMN+`yMmmMMMMMMMMMMMMMMMMMMMMMMMMMNMmNMMMMNddd.ydh::MMMMMMNdMNNMMMNMNMMMMmMMMMMMMMMMmhdyyNNyohys
yMMMMMMMMNddMMMMMMMMMN-hMMMMMyhMMmdNMMMNmdddmm. oMMMMMMM//dMMyhMMMMMMMMMMMMMMMMMMMMMMMMMhNMmMMNMNmdd-yd+/-MMMMMMNhMmmNMMMMMMMMMmMMMMMMMMMMsmN+ymMs.dyy
yMMMMMMmdddNMMMMMMMMN:yMMMMdomMMmdNMMMNddddmh``hMMMMMMMNNMMMMMMMMMMMMMMMMMMMMMMMNMMMhMsMMMMdMMNMNNdd-yy:``MMMMMMNshmdmMMMMMMMMMmNMMMMMMMMM+NM/yyMh`dys
yMMMmmydddmMMMMMMMMM:sMMMNsyMMMmdNNMMmddddd+ .hMMMNNMMMMMMMMMMMMMMNNMMMMMMMMMdmNNMMMNymNMMmdMdMMNNdd-y-. `NMMMMMsyNmddNMMMMNMNMdmMMMMMMMNMsMM/+ymmydho
yMMddohdddNMMMMMMMM/oMMMmomMMMmdmmNNdddddds -dNMMNhNoMMMmMMMNoNNNNNNMMMMMMNysNNmMMNMMMNMMMhmMdMMNmmd-y    mNMMMMydMmhhmMMMMdMmyhdMMMMMMMmMdMMo-yhmddhs
yNhy:yddddNMMMNMMMo+MMMdhMMMMmddhNNy:hdddo :NmMNm/NMNMMMyNMNNmdmmmmdMMMMMmshNNdmMMhmMMMMMMymMNMMmddd-s    hmNMMMMNMmhhdNMMMMMNohdNMMMMMMdNMMMd symddhy
sh+./ddddmMMNNMMMo/MMMdNMMMMNdhoNmdddddd+ /NdNdy.oMMmMMMNNmdmddddd/-dMMMMmMNMdmmMMMoyNMdMmymMMMNdddd:+    smmMMMMhMNhydmMMMMMmoydmMMMMMMddMMMM`+ydddyy
o/``hddddMMNdNMNs:NMMmNMMMMMdy-mmddddddo +Nddhd`.mmyMMNNdyhdyhddhhmNmNMMMMmMmdNdmMMNsyNsMhymMMNmNdhd-/    smyMMMMyMNdysmmMMMMsoyddMMMMMMdhMMMM/-yhdd/o
-  /dhddNMNymMmo-mMMMNMMMMMmy`smddddddo +Nmyods:md+dNmmyo+yyhhhyyhmdNMMMMNdNdmMdmdMMNymymyymmMMdNdsd--    smomMddNMMdysmdMMMsooyddNMMMMNdhdMMMd-yydd-/
   yyddNMNsdMdo-dMMMmdMMMMmh.+ddddddds`/Nmy:yMsNh+/dmhsoo+shhyoosyhMMMMMMsNddNNdMddMNhdmdsydyNMMmh+h.`   `smsdNdhMMMmyhmdmMN`sssddmMMMMMdhyNNMMohyddys
  :sydNMNshMm+-hMMMM:mMMMm-:/ddhdhddy`:Nmy.+MMNdo/sss++++oyso+/++sddmmmNysddmmhdMMdNNhddyoyysNMMm+/s`.   `omydmNyNMMNhdmddNy hs+dddMMMMMddydMNMdmyhyhy
 `s+dmMmhhNNo-sMMMNNdMMMMd/-ddydhhdh.-mms`.NMMdd/-`+++oo+oo+++ooydddddhy/yyyyssyNMmMNyhho/yyymMNs/-s..    omoddd:NMMMddmhyN/ hy/dddNMNMNNhyyNydNNhh-ds
 /.hmMNdhmMh/+MMNmNdNMMMdd/hdsddsdd-.hh/` oMNdhh` ./++++sdmmmmmmNNNNNNNmmmmmhyssddodmyyd+/sysdNNo/`/.     ohsdd+ mMhMNdmysm/ dd:hddmMNMmMhyyh+smNdysd-
:-/dMNdddNMo:NMdyMmdMMNsdyhdoydyhd+`s+.  .dNdyyy-:/+oymNMMmhhhhyyyyyyhhhhhdmmNNmdysydssys+soosdd/: :   `  oyhdd. sM+MMNNoymh`NN-ydddMmMmMyo/ossyNdhhd:
. yNNdhdmMm-mMh/oNdNMN+hdhdos.:+ds -` ``.smhsoo++osddMMMmhyyyyyyyyyyyyyyyyyyyhmNmmhys+ooosy/o`+//.`:`  `- +shdd  sd/MMMM+ydNoMM:sdddNmNmNho:oossyddhdh
  dMmhhdNN:hmsoNNdNMM+ydddssdy+oh.   -o.oyso+++oymMMMNmhyyyyyyyyyyyyyyyyyyyyyyyhhhdmho++/os-.--+s+:-.`  `.:hsdd-`msyNMMMoodNmMM/odddmmNdmNo+sooooydddh
 .MmdhhdMo+d+:NMNmMM+omddhsddh.h:   -y+oso+///smNMMMmhyyyyyyyyyyyyyyyyyyyyyyyyyhyyyydmd+//o:`/mNmMN:..`  `.y:yso:dsyNMMMh+hdNMMo/ddddmmddM/o+++++oyddd
 sNdhh/dd-h+:sMMmMMo:Ndddyddd/-s   :hys++oyhdmNMMMMNhyyyyyyyyyyyyyyyyyyyyyyyyyyyhyyyyydNs/+:+NMo.NMy-..`...::ooooyhomMMMNsshmMMy:yhddddddN++::+//+oydd
`NmhhdsN-s++-NMNNMs.ymdshdddy +.: /yoshdNNmmdddhdmNmdhhyyyyyyyyyyyyyyyyyyyyyyyyyyhyyyyymMs::yMN` hMm:::--:+yddsooodhddMMMd+yhMMd-dosdddddd+/. :////ohd
+NdhhdMs+sy-yMMmNy`ysdo/dsdd/`/-s++smNmmmdhhhhhhhhhddmmmmdhhhyyyyyyyyyyyyyyyyyyyyhyyyyyyNMo-dMh  sMN::::sNNhmMN+o:ys+mmMMN+soMMN.hd/:ydddd+y   ::::/os
ydhhhNm-dhs.MMNdh`ohsh-dhsdh`-`yhd/mMMNNmmdmNNNNNhhhddddddNMNNNNmmmddhhhhyyyyyyyyhhyyyyyhMN:NMo  sMN//omNs-`dMm++`o+sMhMMMyo/MMM-sdd+.+hddos/  .:::::o
shhhho/sNN`sMMyd`:dyhsyd+dd/ ./dddhsdmNNmdmNMMMMy-````````yNmNNNNmNNNNdoyhhhhhhhhdmyyyyyyNMyNM/  oMN+hNh- `sMMy:- /+sMhmmMd+:mMM+oddds`.ydy/d-  ----..
ohhy..:mMo`NMhh:`hdyhydh:dh` .hddddy+/+osysMMMMNo`    ``  hNmMNmdmNNmNh   `.-:+shdNmdhhyydMmNM:  oMNmd/` `oMMso.`.//oNm+dMN+`yMMs/ddddy/.sh+dy` `----`
ohh: .yNm`/Md+m.sdhsh/d:/do .yhdddhs+/////-MMMMd:      .. +mmNNNmy/mmd:      `.--::/sNNmdmMNNM.  oMm/` `-hMNs:/`.`-/ohy-hNMs sMMd-ddddddy/sddd/  .---`
ohh `+dms ym+sohdhys:sy od. -d+hdhys+:::://MMMMh.      .s:```/mhy. ```   ` `/hmdmmds:hNNmNMMMm.  +h.  `oNMNs+++::-:/+oo//NMh oMMN.hdddddddsyddo.  ..-.
odh .dms/ do++hdhyoy:d/ yo` :ysys+ss:/::/mNMMMMo`     :dMN/`  ..      `:o- :MMs`.:hmNdMMNMNh/.   `   .yMMNNmmNNNmNms/++s yNd`sdMN:yddddhdddhhds:` `...
omM+yyd+.-s+/oddyy.ssh`.s:.`+shs++o/://-hMMmmMN:     `NMMM:`         -+MMN.`smMds-``/sydd+:`         /o+/::..--:yMNy///y`/mm/+dMm+oddddyhdhdddy/.  ...
sNMyhh/s`:++/ddyyo.sd+`/:so.+m/ss+/-///-mMMm+my`     /NNMm`           yMMM:  `/yNNs.    `               -ohddddmh+:.+++h--dd+ohMMs+ddddyyd:hddh+-` `..
sMNhoy s.++/sdhyy+:hd-`+`m..oh-`s+::////oMMdhNo      .mNm:            mNMM:     -ymNo`                .:-``oN+:.  ``+ood+`ddyssNMh/ddddhoy/-dddo:  `..
yMhho- //+++mhyyy-/dy`.+`s:``od-`+oso//+-yMMNM+`      ./`   `-`       +ddo`       -mMm+`            `+mms``+My``  `.shhhm hddysmMm+hdddds/o ydds/`.`..
yMd+s  -+++ydyyys +d/ :/..:/` .+/` ``:y+/-hMMMy-            sy`                    +MMMd:`         `dMs..+mMy-`` ` /mNmhM:sddhyhNMyoddddy/+`:ddh/...`.
yNd/s  /++/mhyyy/ od. /-.-..o-       `d///-/ymN:            `:.`                  `mMMMMMs-``      -mNydMMN/..` ``-shMmdMh/dddyymMdohddddo`- ydd+-`...
ymo+o -+++sdyyys` oh  +``/o//m`       s+///:.:mh`        `-osyhyo-`              `yMMNyydNddy+:`````-yNMMm/.``  .:shydNdMN:dddhyyMmoyhdddy-  :dds:`...
yd:o+ /+s/dhyyy+  +o `/`+ooyyo`       .s//+o:`:N.        :h+:-.-/yy.             :Mh-`  `:dMMMMNmdyhNNNdo..`   .:sNhysmmNM+ddddhymmssydddd+`  ydh/.`..
ss-ss-+ohodyyys-  -/ .-.y+```           .:+ym/-ym:`      +s```````/m`          `+No`      -smNmmmdhyss/:.`  ``-+hdMMossmmMyhddddhhdyoyydddh-  :ydo- ..
o/:yd++yyyhyyy+`  -- .` :yhs+///-.`       `oMMNmmMy.      :o+/:--/s/         `.ys.       `/dMmyo/:-:-.`` ``.-/ydmymMd++ymmhydddddhhyssshddd+. `+sy: `.
+-/yy+odydyys+-   -` `  oddd/.o++hhyo-   .hMMmhyo/mmo.`     `.-..`         `/hmd       `/mdy+//:--:-..``.-:+ydmmMmyNMy++ddhyddddddhysso+yddh:  -/s/  `
:-+ho+ydhhys+-.        `odddhhs+sddyyyhs-+MMmys/:shmmNo:.                 /dhyhm`     /dMd+:/+:--/:-//++shmNhNMmmMmsMmo+mddsdddddddyyo+/:hdds. `:/+`
.-odo+hddhs/-.         -odydds//oddddsssNNMmdy/-syNdhhNMho-.`       `.:+sdMs.osdh`  -hmmmmMd.---+s:dNMMMMMMMmyNMmmMdyMsommdhyddddddys++/-:ddd-  .:.-
.:sho+dddyo:.          :oyoso/://yddhodh+MMNy+.oymdhdMMMMmhhdhyhhhhhmmmhNyds:-syhdosmmmmNMM/---:s/mmMMMMMMNMMmhMMmNMdhN:mNddhddddddds///-`:dds`  -.`
./shooddds/.`         .:o/```.::-sdd:hdssMMmh:/smmmdMMMmysssssssssssNhmdd+ysy/-+yshdddddNMhd+.-+hNMNNMMMMMNmMMddMMmMMdhs:Nhdddddddddh://:``:dd-  ..
.+sdohddh+-`          -:+`::.-::/yd+-dd/mMMNNNdmMddMMNo+oossyyyyyyyhMohmNo/ysyo--+ossyhmd+-sNo-hMMMMNNMMMMMdmMN+dMMMMMdd.-oddddddddddo-::.``:ds   .`
.osyoNmd+:`           ::- :+----/yy +ds:yyo:--:mmdNMh/-..-----:::---ydmmhNs/ssss+/--sNh+:---omsMMMMMNdMMMMMMsmm /mMMMMMms `ydddddddddy/`-.```-h.  ``
.ossyMmh`.`           -:. //----/+` +ddhyyd.`.-ddmMmyo++//:--...--:/sMdhhyMh/osssssmMh/::----dMMMMMNomNMNMMMN:ds+mNMMMNMN- :dddddddhdh````````.o   ``
-ooydNd+ ``           .:` +:.-.-:   +dhysyd`..-NNNNhssssssssssyyyyyyyymNdhmmNs:/oydMMh/:-----hMMMMN+/yMMMmNMMh-hmMmNMMNmMm. sdddddddyd`     ```.-  ``
:ooddmd- `            `:..+....-`   shsooys`../NMNs:+ossyyyssssyyyyyyyydNdhhyhyyshmdmmo::----mMMMMm/+mMMMMddMMo.hNMmNMMmdMm..hddddddyh:     ` ` ``  `
:osdddd`               :-o:`-.`.    yoo+os/..-oMN+-..----:///////::-----/ydhhyyyyhhhhN+::-:omMMMMMMomMMMMNMdyNM:`yMMdNMMdhMm.:dddddddsh/.`  `  `     `
/oydddy                --+``.``    .s+++o+/:/yNNys+:-....................-:odmmmmdmhmMssyhhdMMMMMMMMMMMMMNNMmomm.`+NNyNMMhhMm-+ddddddyyyy+` `` ``    `
/ohddmo                `.  `` `    /++++o-:+NNhyssssso+/:-.............-:/+osyyo:---:mNs++mNMMNMMNMMMMMMMMNNMNsyh. +mmodNMhyNN:sdddhdd/syy:` -  ``
+sdddd:                    ``      ++/+o+-yNm+-:+oysssssssso++////++oossososs+:-....:dMo:sdNMMdMMMNmNMMMMMMNNMMhsd-`+mm/sNNhyNN+hddyhdh/hdy/ `/  ``
+ydddh`                    .```    -//+ohmNmo-...-:+osysssssssssssssssssso+-......-+mhdm:+hdMNyhMMMMmdmMMMNMNMMMNyd` -hm:+dMdsNNsddhodd/oddh: -/  `
ohdddy`                    `..`     ./oohMmhhy+-....--:/+ossssssssssoo+/:-.....``..+/+yh+:oydyd+dMNMMNdyhNMmMMMMMMmo` `+m::ydyoNNmddosdh.yddh: ++
ymddds`                     ..`      +hNMMNmdhhy+:-.......---:::::---........-:+ss+/-`   ``/+oMsyMMmmNMNdsdNdMMMMMMNh+`.yN:.oysomNddyshd+-dddh-`yo
yMddds`                     ``      `oNNNNNNNmdhyyo/:-...................-:/osssyhNNdy+:--.```/ssNMMMNNNMMmMhyMMMMMMMMdsohN/`:/++dmddsydd./dddh-.h+
yNmddo`                            `:oMNmddddNNNdysyss+/::-......--:://osssyssydNNmhhhhhhdNs://:/+shmmNMMMMMMhhMMMMMMMMNhyhN/ -:-/ydddyhds +ddh+ -h-
ymmddo.                             `/NMdddddddmNNmhyyyysssssssssssssyssyyyhdmNNdhhhhhhhhhNMsssyhdmmmNNNMMMMMMNMMMMMMMMMNNddN+`:/.-sdddhdd:`yd/o .:y.
yNmddo.                              :yMmddddddddmmNNNmmdhyyyyyyyyhhddmmNNNNNmmddhhhhhhhhhmMhyhhommmMMMMMMMmmMMMMMMmNMMMMMMdmMy.:o-`/hhshdh..hy-  ./s`
yNmdds-                              :dMMmdddddddddddddddmmmmmmmmNNNNNmmmmddddddhhhhhhhhhhdMyyyhdhdmNMMMMMMMddMMMMMNo/ohmMMmmMMm+-s+.`/o+ydh//dh-  -/+
yNNdds:`                          ``:sMMNMmddddddddhhhhhhhhhdNdddddddddddddddddddhhhhhhhhhmMyhhsdNdmNMMMNmmNMNNMMMMMN+.`.oNMmddNMh:sy:  ./yddsydh:  -/
yMNdhy:.                        .:oydMNs:hMNmdddddddhhhhhdmNmNNNNmmNNmNmmmddddddhhhhhhhhdmMmooyssmdmNMMMy..-+MMMMMMMMMmo/:+NMNdmNMmo/h+`  :ydddddd/  -
yMmNhy/-`                    `.+ydddMd:  .sdmNNmdddhhhddmmmyssss+//+++ohmNNNNmmmmdddhhdmNMmyoyyyshomMMMMMy-.-yMMMMMMMMMMMMNmMMNNMMNMd+hs.  -sdddddd+`
yMNNmy+:.                  -+ydNmhyhN/      -+dmmdmmmmmmyyoosyy+-----/dNdyyhhdmNNNmmmddhmNmdhyyhsdoNMMMMMMMNMMMMMMMMMMMMNMMMMNMMMMMMMNyhh/. .ohddddds`
yMNmNmo:-`               .+dNmhhssshhh:-::/s++:-`.::/:--+syhdhy-----+NNy+yNMmhhhhhyyyyyyyyyhddys+/+NNMMMMMMMMMMMMNNNmdhhdMMMMMNMMMMMMMMmmNh+-.+dhddddy
yMMNmmh:..  `           -sNmhyhsyhoyhhhhdhmy-.--:-.`   -hdhyyds---:sMdosmMMMMMMNmNNMMNmmmmmhsossyshdNMMMMMMNNdyyhdmmNMMMMMMMMMMMMMMMMMMMMMMMNd+hyydddd
yMMMddyy-``            ./mmyyyohosyshysddyy/////-     `yh+:yddo/ohmMmhdMMMMMMMNNMMMMMNNNNMMMMMMMMMMMMMMMMMMNmNNMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMdd+dmdd
yMMMmdddy-`` `    .    :/Ndyyyhshyshyydmy+///-.       +:`:hddmmMmdmMmdNMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMNMMMMMMNNNNNNNmmmmmmNMMNNddddmNNMMMMMNNyddd
yMMMMddddy/.````  .:.  -/yNhyyyhhhoshhs/::.`         ` `+ddmNMMNddmddmMMMMMMMMMMMMMMMMMMMMMMMMNNNNNNNmmddhyyssoooooo++/+oosydmNNdhs+//+oyddNNNNNNNmNNm
yMMMMNddddh+-.``   `..  -/yddhhhhddd+.``     `.::     :yddNNNNmddddddhhyhhdhhyssyyyssssyhdddmmmmmdddhhhhdmmNNMMMMMMMMMNNNmmmmNNMMMMMMMNmdmmNNNNNNNNmdh
yMMMMMNdddddy+:.``       `.+oyhhhh+`     `-/oyhs.   -sddyhyysoydddddh+++++++ossssyhmMMMMMMMMMMMMMMMMMMMNMMNNNmmNMMMMMNNNNNNNNNNmddyyoo+++/+/////::://+
yMMMMMMMNdddddhs/-.``       `.....:-..:+shdddh/  `/ydmNMMMMMMmddddddNNMNdysooooydNMMNMMMNNNNNNNmdhyso+++///+sdmmmmNNMNNNmddhhysyyhhyyysssoooosso++//:-
yMMMMMMMMMNddddddhs+/:.`           -/+oooo++/--+yddmNMMMMMMMNmddddho++////+oosyyhyyysso+/:--..:/+oydddmmmNNmddhhhhyyyyhdmNNmdhyso+//:-.``
oyso++/:/oosoohhddddhs+/:-.`     ```...-:///oshddmNMMMNNmddhydddmhssssssso+/:--.....-----::::::---...```  `    ..-://:-.``
-/:-.`     .- `.:++shddhyso//::::::::://+oshdddo--.....-::/+yy/:-.`
            `-.   -::::/oyddhhyysoooosyhddddy/`   ```       -
                      ..`` .:+`         `.-`
*/



//when are the extra endifs necessary (see some other .h files)
#endif /* _KERNEL */
#endif /* _SYS_NVM_H_ */
