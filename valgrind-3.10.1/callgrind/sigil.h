/*
   This file is part of Sigil, a tool for call graph profiling programs.
 
   Copyright (C) 2012, Siddharth Nilakantan, Drexel University
  
   This tool is derived from and contains code from Callgrind
   Copyright (C) 2002-2011, Josef Weidendorfer (Josef.Weidendorfer@gmx.de)
 
   This tool is also derived from and contains code from Cachegrind
   Copyright (C) 2002-2011 Nicholas Nethercote (njn@valgrind.org)
 
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.
 
   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
 
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.
 
   The GNU General Public License is contained in the file COPYING.
*/

#include "global.h"

/* DEFINITIONS ADDED TO PUT ALL DATA ACCESSES FOR EVERY ADDRESS IN A LINKED LIST - Sid */
#define DRWINFOCHUNK_SIZE 10000
#define NUM_FUNCTIONS 10000
#define SELF 20000
#define NO_PROD 30000
#define STARTUP_FUNC 30001
#define NUM_CALLEES 1000
#define HASH_SIZE 20
#define DEPENDENCE_LIST_CHUNK_SIZE 100
#define NUM_CACHE_ENTRIES 10000
#define CACHE_ENTRY_SIZE 20
#define CACHE_LOOKUP_PART_SIZE 10
//#define ADDRCHUNK_ARRAY_SIZE 1000
#define ADDRCHUNK_ARRAY_SIZE 10
#define MAX_THREADS 512
#define EVENTCHUNK_SIZE 10000
#define MAX_EVENTLIST_CHUNKS_PER_THREAD 3
#define MAX_EVENTLIST_CHUNKS 1400
#define MAX_EVENTINFO_SIZE 1 //In Giga bytes. 
//#define MAX_EVENTINFO_SIZE 1 //In Giga bytes. 
#define MAX_EVENTADDRCHUNK_SIZE 1 //In Giga bytes. 
#define DOLLAR_ON 1
//#define LWC_PM_SIZE 524288
//#define LWC_PM_SIZE 4194304
//#define LWC_SM_SIZE 65536
#define LWC_PM_SIZE 1048576 //1MB
#define LWC_SM_SIZE 262144 //256K
#define FUNCINSTLIST_CHUNK_SIZE 5
#define MAX_NUM_SYSCALLS      256   // Highest seen in vg_syscalls.c: 237
#define INVALID_TEMPREG 999999999
#define SYSCALL_ADDRCHUNK_SIZE 10000
/* Done with DEFINITIONS - inserted by Sid */

/*------------------------------------------------------------*/
/*--- Structure declarations                               ---*/
/*------------------------------------------------------------*/

/* STRUCTURES ADDED TO PUT ALL DATA ACCESSES FOR EVERY ADDRESS IN A LINKED LIST - Sid */

typedef struct _addrchunk addrchunk;
typedef struct _funcinst funcinst;
typedef struct _addrchunknode addrchunknode;
typedef struct _evt_addrchunknode evt_addrchunknode;
typedef struct _drwevent drwevent;
typedef struct _funcinfo funcinfo;
typedef struct _dependencelist dependencelist;
typedef struct _dependencelist_elemt dependencelist_elemt;

struct _evt_addrchunknode {
  Addr rangefirst, rangelast;
  int shared_bytes, shared_read_tid;
  ULong shared_read_event_num;
  evt_addrchunknode *prev, *next;
};

struct _addrchunknode {
  Addr rangefirst, rangelast;
  addrchunknode *prev, *next;
  ULong count_unique; //Holds the count of the number of times this chunk of addresses was accessed uniquely
  ULong count; //Holds the count of the number of times this was accessed, either for a read or write
};

typedef struct _pth_node pth_node;
struct _pth_node {
  Addr addr;
  ULong threadmap;
  pth_node *prev, *next;
};

struct _addrchunk {
  Addr first, last; // Holds the first hash and last hash 
  addrchunknode* original; //Original points to the first addrchunknode whose  hash of address range is in between first and last hash
};

/* Structure to list all the functions that have been consumed from.
   This will be used by funcinfo to hold addresses from various consumers.
 */
struct _dependencelist_elemt { //8bytes + consumerfuncinfostuff
  int fn_number;
  int funcinst_number;
  int tid;

/* //Min, Max not used in per-call list, but in overall list, it holds the min and max of the count of bytes transferred into this function from that function/call */
/*   int min_count_unique;  */
/*   int max_count_unique; */
/*   int tot_count_unique; */
/* //In the overall list, it holds the average, of the amount of data transferred per call of the function that this function consumed from. In the per-call list it holds the count seen by this particular call */
/*   float avg_count_unique; */
/* //These remain uninitialized in the percall list, but in the overall list, they hold the average, min and max of the number of calls to a function that this one depends upon. */
/*   int min_num_dep_calls;  */
/*   int max_num_dep_calls; */
/*   float avg_num_dep_calls; //Per call average of number of calls to a function */
/*   int tot_num_dep_calls; */

  //Copied over from consumerfuncinfo
  funcinst *vert_parent_fn;
  funcinst *consumed_fn;
  dependencelist_elemt *next_horiz, *prev_horiz;
  addrchunk* consumedlist; //Pointer to a linked list of address chunks. These addresses have been consumed from by the funcinfo function and have been produced by this function.
  ULong count;
  ULong count_unique;
};

struct _dependencelist { //If a pointer is 8 bytes, then 16bytes * DEPENDENCE_LIST_CHUNK_SIZE + 32bytes + 4bytes. With default values, size = 15.6kB
  dependencelist *next, *prev;
  dependencelist_elemt list_chunk[DEPENDENCE_LIST_CHUNK_SIZE]; //note that this is not a list of pointers to structures, it is a list of structures!
  int size; //Should be initialized to zero
  
};

typedef struct _funccontext funccontext;
struct _funccontext {
  int fn_number;
  int funcinst_number;
  funcinst *funcinst_ptr;
};

/* Array to store central function info. Also has global link to the 
 * structures of functions who consume from this function.
 * Chunks are allocated on demand, and deallocated at program termination.
 * This can also act as a linked list. This is needed because 
 * contiguous locations in the funcarray are not necessarily used. There may
 * be holes in between. Thus we need to track only the locations which are 
 * used from this list. 
 * Alternatively, an array could be used, but needs to be sized statically
 */

struct _funcinfo {
  fn_node* function; //From here we can access name with function->name
  int fn_number;
  funcinfo *next, *prev; //To quickly access the list of the functions present in the program
  int number_of_funcinsts;

  //  funcinst *funcinst_list; //Not sure if this list is needed

  //Cache entire contexts to accelerate the common case lookup
  funccontext *context;
  int context_size;
};

//DON"T NEED THIS BECAUSE IT WAS DETERMINED THAT WE HAVE TO SEARCH THROUGH THE CALLTREE EVERYTIME TO DETERMINE IF THIS IS UNIQUE OR NOT ANYWAY
/* typedef struct _funcinst_list_chunk funcinst_list_chunk; */
/* struct _funcinst_list_chunk { */
/*   funcinst_list_chunk *next, *prev; */
/*   funcinst funcinst_list[FUNCINSTLIST_CHUNK_SIZE]; //note that this is not a list of pointers to structures, it is a list of structures! */
/*   int size; //Should be initialized to zero */
/* } */

/* Array to store data for a function instance. This is separated from other structs
 * to support a dynamic number of functioninfos for a function info item.
 * Chunks are allocated on demand, and deallocated at program termination.
 * This can also act as a linked list. This is needed because 
 * contiguous locations in the funcarray are not necessarily used. There may
 * be holes in between. Thus we need to track only the locations which are 
 * used from this list. 
 * Alternatively, an array could be used, but needs to be sized statically
 */
struct _funcinst {
  funcinfo* function_info;
  int fn_number;
  int tid;

  //addrchunk* producedlist; //Pointer to a linked list of address chunks. Should point to the first element in the list
  //consumerfuncinfo* consumedlist; //Pointer to a linked list of functions consumed from. Should point to the first element in that list
  //consumerfuncinfo* consumerlist; //Pointer to a linked list of functions that consume from this function. Should point to the first element in that list. This corresponds to the horizontal linking of the consumerfuncinfo lists

  dependencelist *consumedlist; //Pointer to a linked list of functions consumed from. Should point to the first element in that list
  dependencelist_elemt *consumerlist; //Pointer to a linked list of functions that consume from this function. Should point to the first element in that list. This corresponds to the horizontal linking of the consumerfuncinfo lists
  //Int fn_number; 
  funcinst *caller; //Points to the single caller in this context. (There will be a different context for each unique caller to this function and different such structures for this function under different contexts)
  funcinst **callees; //Pointer to the pointers for the different callees of this function. NUM_CALLEES will determine the number of callees.
  int num_callees;
  ULong ip_comm_unique;
  //ULong op_comm_unique; //Output communication can be inferred.
  ULong ip_comm;
  //ULong op_comm;
  ULong instrs, flops, iops;
  int num_calls;

  //For assigning unique numbers for funcinsts and finding them quickly.
  int funcinst_number; //Each funcinst for a particular function will have a unique number.

  //funcinst* funcinst_list_next; //Not sure if this list is needed.
  //funcinst* funcinst_list_prev;

/*   //Maintain a list of funcinsts, for this particular call, that this funcinsts reads from. When the call returns, then we need to compare this list against the global call history (func_hist) and add to the dependencelist */
/*   //Limited to 1000 for now. Can be made into a hash table eventually, if a function really consumes from that many other functions in one call. */
/*     dependencelist *data_dep_list_for_call; //Hopefully will not need more than one chunk in the list for a single call. count not used for this, but num_dep_calls is used. */
/*     dependencelist *overall_dep_list; //We'll just keep a list, where each element in the list is a chunk of funcinsts. I don't really expect that more than one chunk will be exceeded */
/*   int func_hist_prev_call_idx; */
/*   int func_hist_curr_call_idx; */

  ULong num_events;
  //These are not used as a central list is used instead
  //drwevent* latest_event;
  //drweventlist* drw_eventlist_start;
  //drweventlist* drw_eventlist_end;
  SysRes res;
  int fd;
  
  //To keep track of size
  ULong num_dependencelists;
  ULong num_addrchunks;
  ULong num_addrchunknodes;

};

typedef struct _drwbbinfo drwbbinfo;
struct _drwbbinfo {
  int previous_bb_jmpindex;
  BB* previous_bb;
  BBCC* previous_bbcc;
  BB* current_bb;
  ClgJumpKind expected_jmpkind;
};

struct _drwevent {
  int type; //type = 0 -> operations, 1 -> communication, 2 -> pthread_event
  int optype; 
  funcinst* producer; //This is used for type 0 and type 1 events
  funcinst* consumer; //This is 0 for type 0 events and has the consumer to type 1 events
  ULong bytes; //0 for type 0, count for type 1
  ULong bytes_unique; //0 for type 0, count for type 1
  ULong iops; //count for type 0, 0 for type 1
  ULong flops; //count for type 0, 0 for type 1
  //  int tid;

  //These are to capture statistics for local memory operations during a type0 event
  ULong unique_local, non_unique_local; //Unique will give the number of cold misses and non-unique can be used to calculate hits
  ULong total_mem_reads;
  ULong total_mem_writes;

  //Shared stuff
  ULong shared_bytes_written;
  //ULong 
  ULong event_num;

  //Pointers just to keep necessary timing information when a producedlist chunk is updated/overwritten
  //  drwevent *next, *prev;
  //evt_addrchunknode *producedlist;
  //evt_addrchunknode *conslist;
  evt_addrchunknode *rlist;
  evt_addrchunknode *wlist;
  fn_node* debug_node1;
  fn_node* debug_node2;
  
};

typedef struct _drweventlist drweventlist;
struct _drweventlist {
  drweventlist *next, *prev;
  drwevent list_chunk[EVENTCHUNK_SIZE]; //note that this is not a list of pointers to structures, it is a list of structures!
  int size; //Should be initialized to zero
};

typedef struct _drwglobvars drwglobvars;
struct _drwglobvars {
  funcinfo* funcarray[NUM_FUNCTIONS];
  //funcinfo** CLG_(sortedfuncarray)[NUM_FUNCTIONS];
  funcinfo* funcinfo_first;
  //funcinfo* CLG_(funcinfo_last);
  funcinst* funcinst_first;
  funcinst* previous_funcinst;
  drwbbinfo current_drwbbinfo;
  int tid;
  Bool in_pthread_api_call;
  //We need to move these to funcinsts so that we can experiment with capturing events in functions
/*   ULong num_events; */
/*   drwevent* latest_event; */
/*   drweventlist* drw_eventlist_start; */
/*   drweventlist* drw_eventlist_end; */
/*   SysRes res; */
/*   int fd; */
  //Above lines commented out as we have moved this functionality to be within a funcinst
};

//We can encapsulate these in structures as well making it slightly more efficient
typedef struct { // Secondary Map: covers 64KB
  funcinst *last_writer[LWC_SM_SIZE]; // 64K entries for the last writer of the location
  funcinst *last_reader[LWC_SM_SIZE]; // 64K entries for the last writer of the location
} SM;

typedef struct { // Secondary Map: covers 64KB
  funcinst *last_writer[LWC_SM_SIZE]; // 64K entries for the last writer of the location
  //drwevent *last_writer_event[LWC_SM_SIZE];
  ULong last_writer_event[LWC_SM_SIZE];
  funcinst *last_reader[LWC_SM_SIZE];
  drwevent *last_reader_event[LWC_SM_SIZE];
  //int last_writer_event_dumpnum[LWC_SM_SIZE];
} SM_event;

//SM* PM[LWC_PM_SIZE]; // Primary Map: covers 32GB
void* PM[LWC_PM_SIZE]; // Primary Map: covers 32GB

typedef struct {
      Char*  name;
      UInt   argc;
      Char** argv;            // names of args
      UInt   min_mem_reads;   // min number of memory reads done
      UInt   max_mem_reads;   // max number of memory reads done
} SyscallInfo;

typedef enum {
      Sz0=0, Sz1=1, Sz2=2, Sz4=4,
} RxSize;
typedef enum {
      PZero=0, POne=1, PMany=2
} PCount;
typedef struct _ShW {
       // word 1
       UInt   kind:8;                  // node kind
       UInt   extra:8;                 // extra info for some node kinds
       UInt   argc:8;                  // Num of args for variable sized ones
       RxSize size:3;                  // Sz0, Sz1, Sz2 or Sz4;  3 bits used
                                       //   so that Sz4 can be 4 (a bit lazy)

       PCount n_parents:2;             // number of dependents (PZero..PMany)
       UInt   rewritten:1;             // has node been rewritten?
       UInt   graphed:1;               // has node been graphed?

       // word 2
       UInt   val;                     // actual value represented by node

#ifdef RX_SLICING
       // word 3
       Addr   instr_addr;              // address of original x86 instruction
#endif
       // words 3+
       struct _ShW* argv[0];           // variable number of args;
   }
   ShW;

/* Done with STRUCTURES - inserted by Sid */

/*------------------------------------------------------------*/
/*--- Functions                                            ---*/
/*------------------------------------------------------------*/

/* FUNCTIONS AND GLOBAL VARIABLES ADDED TO PUT ALL DATA ACCESSES FOR EVERY ADDRESS IN A LINKED LIST - Sid */

//New stuff for changed format
extern drwglobvars* CLG_(thread_globvars)[MAX_THREADS];
extern ULong CLG_(total_data_reads);
extern ULong CLG_(total_data_writes);
extern ULong CLG_(total_instrs);
//extern int CLG_(previous_bb_jmpindex);
extern drwbbinfo CLG_(current_drwbbinfo);
extern void* CLG_(DSM);
extern SysRes CLG_(drw_res);
extern int CLG_(drw_fd);
extern ULong CLG_(num_events);
//Variable for FUNCTION CALLS TO INSTRUMENT THIGNS DURING SYSTEM CALLS
extern SyscallInfo CLG_(syscall_info)[MAX_NUM_SYSCALLS];
extern Int  CLG_(current_syscall);
extern Int  CLG_(current_syscall_tid);
extern Addr CLG_(last_mem_write_addr);
extern UInt CLG_(last_mem_write_len);
extern addrchunk* CLG_(syscall_addrchunks);
extern Int CLG_(syscall_addrchunk_idx);
extern ULong CLG_(pthread_thread_map)[MAX_THREADS];
extern int CLG_(num_threads);

//Pthread stuff
extern Bool CLG_(mutex_lock);
extern Bool CLG_(mutex_unlock);

void CLG_(init_funcarray)(void);
void CLG_(free_funcarray)(void);
void CLG_(print_to_file)(void);
void CLG_(drwinit_thread)(int tid);
void CLG_(storeIDRWcontext) (InstrInfo* inode, int datasize, Addr ea, Bool WR, int opsflag);
void CLG_(put_in_last_write_cache) (Int datasize, Addr ea, funcinst* writer, int tid);
void CLG_(read_last_write_cache) (Int datasize, Addr ea, funcinfo *current_funcinfo_ptr, funcinst *current_funcinst_ptr, int tid);

//FUNCTION CALLS TO INSTRUMENT THIGNS DURING SYSTEM CALLS
void CLG_(pre_mem_read_asciiz)(CorePart part, ThreadId tid, const HChar* s, Addr a);
void CLG_(pre_mem_read)(CorePart part, ThreadId tid, const HChar* s, Addr a, SizeT len);
void CLG_(pre_mem_write)(CorePart part, ThreadId tid, const HChar* s, Addr a, SizeT len);
void CLG_(post_mem_write)(CorePart part, ThreadId tid, Addr a, SizeT len);
void CLG_(new_mem_brk) ( Addr a, SizeT len, ThreadId tid);
void CLG_(new_mem_mmap) ( Addr a, SizeT len, Bool rr, Bool ww, Bool xx, ULong di_handle );
void CLG_(new_mem_startup) ( Addr start_a, SizeT len, Bool rr, Bool ww, Bool xx, ULong di_handle );

//End new stuff for changed format

/* Done with FUNCTIONS AND GLOBAL VARIABLES - inserted by Sid */
