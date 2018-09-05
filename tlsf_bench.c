/**
 * tlsf library preliminary benchmark.
 *
 * Strategy: create a decently sized pool (def. 4GB) which
 * will then be hammered by consecutive mallocs/frees of 
 * various block sizes which will be variably sized given
 * a range (def. 8kb-50mb)
 *
 * 
 * @author Andreas Grammenos <axorl@niometrics.com>
 * @copyright Copyright (c) 2018, Niometrics
 * All rights reserved.
 */

/**
 * Includes
 */

/* define extensions */
#define _GNU_SOURCE

/* standard libraries */
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
/* sys includes */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
/* intrinsics */
#include <x86intrin.h>

/* tlsf lib */
#include "tlsf.h"
/* tlsf lib (original) */
#include "tlsf_ori.h"

#define MAX_FNAME_BUF 100
#define MAX_FPATH_BUF 2000
#define MAX_TIME_STR_BUF 200

/* tlsf wrapper for convenience */
typedef struct _wtlsf_t  {
  tlsf_t *tlsf_ptr;
  char *mem;
  size_t size;
} wtlsf_t;

/* tlsf original wrapper for convenience */
typedef struct _wtlsf_ori_t {
  //tlsf_ori_t *tlsf_ori_ptr;
  char *mem;
  size_t size;
} wtlsf_ori_t;

/* allocation plan types */
typedef enum _alloc_plan_type_t {
  ALLOC_SEQ = 0,
  ALLOC_RAMP = 1,
  ALLOC_HAMMER = 2,
  ALLOC_CUSTOM = 3,
  /* maybe more to be added? */
} alloc_plan_type_t;

/* allocation slot types */
typedef enum _slot_type_t {
  SLOT_EMPTY = 0,
  SLOT_MALLOC = 1,
  SLOT_FREE = 2,
  /* possibly expand? */
} slot_type_t;

/* benchmark type */
typedef enum _bench_type_t {
  BENCH_TLSF = 1,
  BENCH_NATIVE = 2,
  BENCH_TLSF_ORI = 3,
  BENCH_ALL = 4,
} bench_type_t;

/* type of allocator to use */
typedef enum _use_alloc_type_t {
  USE_TLSF = 1,
  USE_TLSF_ORI = 2,
  USE_NATIVE = 3,
} use_alloc_type_t;

/* allocation plan helper */
typedef struct _alloc_plan_t {
  // essentials
  // half sized arrays (as many as the allocs)
  char **mem_ptr;           // memory block pointers
  int *malloc_tag_time;     // memory block tag time
  size_t *cur_malloc_size;  // current malloc size
  // full sized arrays
  size_t *block_id;         // the block id
  size_t *block_size;       // memory block actual size
  slot_type_t *slot_type;   // flag to show if it's for malloc or free
  // timing stuff
  double *timings;          // timings for each op performed in the plan
  // misc. stuff
  // statistics
  size_t peak_alloc;        // peak allocation for this plan -- should be < pool_size 
  size_t aggregated_alloc;  // aggregated allocation for this plan
  // plan size
  size_t plan_size;         // the plan size
  // min/max alloc. block size for plan
  size_t min_block_size;
  size_t max_block_size;
  // data structure overhead
  double ds_overhead;       // the data structure overhead
  // allocation plan type
  alloc_plan_type_t type;   // the plan type
} alloc_plan_t;

/**
 * Globals
 */
const size_t mb_div = (1024*1024); // divider from Bytes to MB 

// pool config
size_t min_block_size = 8192;   // default 8kb (2^13) 
//size_t pool_size = 2147483648;  // default 2GB (2^31)
size_t pool_size = 4294967296;  // default 4GB (2^32)
size_t min_pool_size = 102400;  // default is 100Mb

// request block size range
size_t blk_mul_min = 1;             // min multiplier
size_t blk_mul_max = 6400;          // max multiplier 6400

// allocation configuration
size_t def_trail = 100;          // default trail size

// bench config (or 100000000)
size_t min_trials = 1000;        // min benchmark trials
//size_t bench_trials = 2000;      // benchmark trials
size_t bench_trials = 100000000; // benchmark trials
//size_t bench_trials = 600000000; // benchmark trials

size_t wtlsf_struct_size = sizeof(wtlsf_t);
size_t tlsf_ori_struct_size = sizeof(tlsf_ori_t);

// file format details
int line_offset = 2;    // line offset

// cpu id for affinity set
int def_cpu_core_id = 0;
// no of cores
int core_count = -1;
// no of available cores
int core_count_avail = -1;

// progress report divider 
int prog_num_steps = 10;
int prog_steps_div = 10000000;
//int prog_steps_div = bench_trials / 10;

// current time
time_t cur_time;

// print buffer
char fname_buf[MAX_FNAME_BUF];
char fpath_buf[MAX_FPATH_BUF];
char time_str_buf[MAX_TIME_STR_BUF];

// logging file pointer
FILE *log_fp = NULL;

// benchmark type (default is tlsf only)
bench_type_t bench_type = BENCH_TLSF;

// trace dump related
char *dump_dir = "./traces";                                  // dump directory
char *dump_ext = "csv";                                       // dump extension
char *log_dir = "./logs";                                     // log directory
char *log_ext = "log";                                        // log directory
char *dump_tlsf_trace_suffix = "tlsf_mem_trace_out";          // tlsf suffix trace 
char *dump_tlsf_ori_trace_suffix = "tlsf_ori_mem_trace_out";  // tlsf ori. suffix trace
char *dump_native_trace_suffix = "native_mem_trace_out";      // native suffix trace 

// tokenizer related
char *tok_delim_cm = ",";
char *tok_delim_nl = "\n";
bool parsing_out_traces = false;

// command line argument config

bool bflag = false; // benchmark flag type
bool cflag = false; // cpu pin flag
bool dflag = false; // dump flag
bool pflag = false; // import flag
bool tflag = false; // custom trial flag
bool iflag = false; // interactive (see progress)
bool lflag = false; // logging flag

// import plan (-p)
char *imp_fname = NULL;

// usage string
const char *usage_str = "\n    Usage: ./tlsf_bench -d -c ((-t ops) | (-p infile)) \n";

/**
 * Functions
 */

/**
 * My logging function
 */
void log_fun(const char *restrict fmt, ...) {
  va_list args;
  // log print
  if(lflag && log_fp != NULL) {
    va_start(args, fmt);
    vfprintf(log_fp, fmt, args);
    va_end(args);
  }
  // normal print
  va_start(args, fmt);
  vfprintf(stdout, fmt, args);
  va_end(args);
}

/**
 * tic: function that starts the timing context
 */
unsigned long long
tic(char *msg) {
  if(msg != NULL) {
    log_fun(" ** tick (%s)\n", msg);
  }
  return __builtin_ia32_rdtsc();
}

/**
 * toc: function that takes a timing context and
 * returns the difference as a double.
 */
double
toc(unsigned long long start, char *msg, bool print) {
  unsigned long long end = __builtin_ia32_rdtsc();
  unsigned long long diff = ((end - start)); /// CLOCKS_PER_SEC;
  if(print) {
    if(msg) {
      log_fun(" ** toc (%s): Elapsed time %llu cycles\n", msg, diff);
    } else {
      log_fun(" ** toc: Elapsed time %llu cycles\n", diff);
    }
  }
  // returns *cycles*
  return diff;
}

/**
 * tic_s: a less granular function that initiates a timing 
 * context and returns it.
 */
clock_t
tic_s(char *msg) {
  if(msg != NULL) {
    log_fun(" ** tick_s (%s)\n", msg);
  }
  // returns *time*
  return clock();
}

/**
 * toc_s; is a less granular timing function that takes a timing 
 * context and returns the difference as a double.
 */
double
toc_s(clock_t start, char *msg, bool print) {
  clock_t end = clock();
  double diff = ((double) end - start) / CLOCKS_PER_SEC;
  if(print) {
    if(msg) {
      log_fun(" ** toc_s (%s): Elapsed time %lf seconds\n", msg, diff);
    } else {
      log_fun(" ** toc_s: Elapsed time %lf seconds\n", diff);
    }
  }
  return diff;
}

/**
 * Function that uniformly generates integer numbers 
 * within (rlow, rhigh]
 */
int 
uni_rand(int rlow, int rhigh) {
    double rval = rand()/(1.0 + RAND_MAX); 
    int range = rhigh - rlow + 1;
    int sval = (rval * range) + rlow;
    return sval;
}

/**
 * This function generates a block of size proportional to the 
 * minimum block size by a factor randomly drawn from uniform 
 * distribution
 */
size_t
block_gen() {
  // generate a random multiplier from a uniform distribution
  int blk_mul = uni_rand(blk_mul_min, blk_mul_max);
  // generate the block size
  size_t blk = blk_mul * min_block_size;
  // return the block size
  return blk;
}

/**
 * This function generates a sequential type of allocation plan.
 */
size_t
tag_seq(alloc_plan_t *plan, size_t trail_size) {
  if(plan->plan_size % 2 != 0 || 
    plan->plan_size % trail_size != 0) {
    log_fun(" !! Cannot create allocation plan, plan size but be multiple \
      of trail size and even\n");
    return 0;
  } else {
    log_fun(" ** Plan size is %zu using a trail size of %zu\n", 
      plan->plan_size, trail_size);
  }
  // loop once and tag allocations
  size_t blk_size = 0;
  size_t total_alloc_size = 0;
  size_t cur_alloc_size = 0;
  size_t peak_alloc = 0;
  int mem_alloc = 0;
  for (int i = 0; i < plan->plan_size; i += 2*trail_size) {
    for(int j = i; j < i+trail_size; j++) {
      blk_size = block_gen();
      // calculate block metrics
      if(plan->min_block_size > blk_size) {
        plan->min_block_size = blk_size;
      }
      if(plan->max_block_size < blk_size) {
        plan->max_block_size = blk_size;
      }
      cur_alloc_size += blk_size;
      // tag malloc block
      //log_fun(" -- Tagging malloc block at: %d with size of %zu bytes\n", j, blk_size);
      // now tag allocation blocks
      plan->block_size[j] = blk_size;       // tag the block size
      plan->slot_type[j] = SLOT_MALLOC;     // tag the slot type, 'malloc'
      plan->malloc_tag_time[mem_alloc] = j; // tag the plan time for this malloc
      plan->block_id[j] = mem_alloc;
      // now tag deallocation blocks
      plan->block_size[j+trail_size] = blk_size;  // index in mem array instead of size
      plan->slot_type[j+trail_size] = SLOT_FREE;  // tag slot type, 'free'
      plan->block_id[j+trail_size] = mem_alloc;
      mem_alloc++;
    }
    // add to aggregate
    total_alloc_size += cur_alloc_size;
    // check for peak
    if(peak_alloc < cur_alloc_size) {
      peak_alloc = cur_alloc_size;
    }
    cur_alloc_size = 0;
  }
  // update plan details
  plan->aggregated_alloc = total_alloc_size;
  plan->peak_alloc = peak_alloc;
  // sanity check
  //assert(mem_alloc == plan->plan_size/2);
  // return the total allocation size for this plan
  return mem_alloc != plan->plan_size/2 ? 0 : total_alloc_size;
}

/**
 * This function tags blocks using a ramp-type of plan
 */
size_t
tag_ramp(alloc_plan_t *plan, double load_factor) {
  if(load_factor < 0 || load_factor >= 0.5) {
    log_fun(" !! Error load factor needs to be between (0, 0.5]\n");
  } else {
    log_fun(" ** Load factor for ramp-phase is %lf\n", load_factor);
  }
  //TODO
  return 0;
}

/**
 * This function tags blocks using a hammer-type of plan
 */
size_t
tag_hammer(alloc_plan_t *plan, double load_factor) {
  //TODO
  return 0;
}

/**
 * Tags memory blocks with specific memory size allocations
 *
 * ALLOC_SEQ: Sequential trails of allocs/deallocs
 *
 * ALLOC_RAMP: All allocations done first (ramp-phase), then all deallocs are performed
 *
 * ALLOC_HAMMER: All allocations are performed in pairs of alloc/dealloc
 */
bool
tag_blocks(alloc_plan_t *plan) {
  size_t total_alloc_size = 0;
  bool ret = false;
  // initialize the min/max block sizes
  plan->min_block_size = min_block_size * blk_mul_max;
  plan->max_block_size = 0; 
  // check plan types
  switch(plan->type) {
    case ALLOC_SEQ: {
      log_fun(" ** Tagging blocks with allocation plan: SEQUENTIAL\n");
      total_alloc_size = tag_seq(plan, def_trail);
      break;
    }
    case ALLOC_RAMP: {
      log_fun(" ** Tagging blocks with allocation plan: RAMP\n");
      total_alloc_size = tag_ramp(plan, 0.5);
      break;
    }
    case ALLOC_HAMMER: {
      log_fun(" ** Tagging blocks with allocation plan: HAMMER\n");
      total_alloc_size = tag_hammer(plan, 0.5);
      break;
    }
    default: {
      log_fun(" ** Tagging blocks with (default) allocation plan: SEQUENTIAL\n");
      plan->type = ALLOC_SEQ;
      total_alloc_size = tag_seq(plan, def_trail);
      break;
    }
  }
  // total allocation size should not be zero
  //assert(total_alloc_size > 0);
  ret = total_alloc_size > 0;
  if(ret) {
    log_fun(" ** Final tags: %zu out of %zu \n", plan->plan_size/2, plan->plan_size);
    log_fun(" -- Total plan pressure: %zu MB with peak allocation: %zu MB\n", 
      total_alloc_size/mb_div, plan->peak_alloc/mb_div);   
    log_fun(" -- Min/Max plan block size: %lf MB / %lf MB \n", 
      1.0*plan->min_block_size / mb_div, 1.0*plan->max_block_size / mb_div); 
  }
  return ret;
}

/**
 * Combined preallocation for allocation plan
 */
bool
perform_plan_prealloc(alloc_plan_t *plan) {
  size_t plan_size = plan->plan_size;
  size_t data_struct_overhead = 0;
  if(plan_size % 2 != 0) {
    log_fun(" !! Error, plan size must be even and contain as many allocs as deallocs\n");
    return false;
  }
  // block size array
  data_struct_overhead += plan_size * sizeof(size_t);
  if((plan->block_size = calloc(plan_size, sizeof(size_t))) == NULL) {
    log_fun(" !! Failed to allocate block size\n");
    return false;
  }
  // timings array
  data_struct_overhead += plan_size * sizeof(double);
  if((plan->timings = calloc(plan_size, sizeof(double))) == NULL) {
    log_fun(" !! Failed to allocate timings array\n");
    free(plan->block_size);
    return false;
  }
  // slot type array
  data_struct_overhead += plan_size * sizeof(slot_type_t);
  if((plan->slot_type = calloc(plan_size, sizeof(slot_type_t))) == NULL) {
    log_fun(" !! Failed to allocate slot type array\n");
    free(plan->block_size);
    free(plan->timings);
    return false;
  }
  // cur_malloc_size array
  data_struct_overhead += (plan_size / 2) * sizeof(size_t);
  if((plan->cur_malloc_size = calloc(plan_size / 2, sizeof(size_t))) == NULL) {
    log_fun(" !! Failed to allocate cur malloc size array\n");
    free(plan->block_size);
    free(plan->timings);
    free(plan->slot_type);
    return false;
  }
  // mem_ptr
  data_struct_overhead += (plan_size / 2) * sizeof(char *);
  if((plan->mem_ptr = calloc(plan_size / 2, sizeof(char *))) == NULL) {
    log_fun(" !! Failed to allocate pointer array\n");
    free(plan->block_size);
    free(plan->timings);
    free(plan->slot_type);
    free(plan->cur_malloc_size);
    return false;
  }
  // malloc tag time
  data_struct_overhead += (plan_size / 2) * sizeof(int);
  if((plan->malloc_tag_time = calloc(plan_size / 2, sizeof(int))) == NULL) {
    log_fun(" !! Failed to allocate malloc tag time array\n");
    free(plan->block_size);
    free(plan->timings);
    free(plan->slot_type);
    free(plan->cur_malloc_size);
    free(plan->mem_ptr);
    return false;
  }
  // block id
  data_struct_overhead += plan_size * sizeof(size_t);
  if ((plan->block_id = calloc(plan_size, sizeof(size_t))) == NULL) {
    log_fun(" !! Failed to allocate block id array\n");
    free(plan->block_size);
    free(plan->timings);
    free(plan->slot_type);
    free(plan->cur_malloc_size);
    free(plan->mem_ptr);
    free(plan->malloc_tag_time);
    return false;
  }
  // add the parent data structure overhead
  data_struct_overhead += sizeof(*plan);
  plan->ds_overhead = (1.0*data_struct_overhead) / mb_div;
  log_fun(" ** Preallocated successfully a plan of size %zu\n", plan_size);
  log_fun(" ** Data structure overhead is approximately: %lf MB\n", plan->ds_overhead);
  return true;
}


/**
 * This function generates an allocation plan based on
 * current trial size; we also make sure that the allocation
 * plan *does not* exceed maximum pool size at any given point.
 *
 * If no plan can be generated the user is informed and false is
 * returned to indicate failure
 *
 * NOTE: There have to be as many allocs as deallocs
 */
bool
gen_alloc_plan(size_t plan_size, alloc_plan_t *plan) {
  // basic error checks
  if(plan_size < min_trials) {
    log_fun(" !! Not enough trials, cannot continue (given: %zu, min req: %zu)\n", 
      plan_size, min_trials);
    return false;
  } else if(plan_size < 2) {
    log_fun(" !! Cannot have a plan size < 2 provided was: %zu\n", plan_size);
    return false;
  } else if(plan_size % 2 != 0) {
    log_fun(" !! Cannot have an odd plan size, given %zu \n", plan_size);
    return false;
  } else if(plan == NULL) {
    log_fun(" !! Null plan struct provided, cannot continue\n");
    return false;
  }
  // erase the structure
  memset(plan, 0, sizeof(*plan));
  // set the plan size
  plan->plan_size = plan_size;

  // first of all, preallocate required vars
  if(!perform_plan_prealloc(plan)) {
    return false;
  }

  // tag the blocks
  tag_blocks(plan);

  // finally return true
  return true;
}

/**
 * This function is responsible for destroying the allocation plan
 */
void
destroy_alloc_plan(alloc_plan_t *plan) {
  // check if we have a valid pointer
  if(plan == NULL) {
    log_fun(" !! No valid plan provided, cannot continue\n");
    return;
  }
  // now check if we have some addresses
  if(plan->mem_ptr != NULL) {
    log_fun(" -- Valid memory block pointer array found, freeing\n");
    free(plan->mem_ptr); 
    plan->mem_ptr = NULL;
  }
  // check if we have a malloc tag array
  if(plan->malloc_tag_time != NULL) {
    log_fun(" -- Valid malloc tag array found, freeing\n");
    free(plan->malloc_tag_time);
    plan->malloc_tag_time = NULL;
  }
  // check if we have a block id array
  if(plan->block_id != NULL) {
    log_fun(" -- Valid block_id array found, freeing\n");
    free(plan->block_id);
    plan->block_id = NULL;
  }
  // check if we have a cur malloc size type array
  if (plan->cur_malloc_size != NULL) {
    log_fun(" -- Valid cur malloc size array found, freeing\n");
    free(plan->cur_malloc_size);
    plan->cur_malloc_size = NULL;
  }
  // check if we have a valid block size type array
  if(plan->block_size != NULL) {
    log_fun(" -- Valid block size array found, freeing\n");
    free(plan->block_size);
    plan->block_size = NULL;
  }
  // check if we have a valid allocation type array
  if(plan->slot_type != NULL) {
    log_fun(" -- Valid allocation type array found, freeing\n");
    free(plan->slot_type);
    plan->slot_type = NULL;
  }
  // check if we have a valid timings array
  if(plan->timings != NULL) {
    log_fun(" -- Valid timings array found, freeing\n");
    free(plan->timings);
    plan->timings = NULL;
  }
}

/**
 * Parse console arguments
 */
bool
parse_args(int argc, char **argv) {
  // set opterr to 0 for silence
  bool ret = true;
  opterr = 0;
  int c;
  while((c = getopt(argc, argv, "b:c:di:lp:t:")) != -1) {
    switch(c) {
      case 'b': {
        // raise bench flag
        bflag = true;
        if(optarg != NULL) {
          char *endp;
          int num = (int) strtol(optarg, &endp, 0);
          switch(num) {
            case 1: {
              log_fun(" ** Benching TLSF allocator only\n");
              bench_type = BENCH_TLSF;
              break;
            }
            case 2: {
              log_fun(" ** Benching TLSF ORI allocator only\n");
              bench_type = BENCH_TLSF_ORI;
              break;
            }
            case 3: {
              log_fun(" ** Benching NATIVE allocator only\n");
              bench_type = BENCH_NATIVE;
              break;
            }
            case 4: {
              log_fun(" ** Benching TLSF, TLSF_ORI, & NATIVE allocators\n");
              bench_type = BENCH_ALL;
              break;
            }
            default: {
              log_fun(" !! Error could not parse valid bench flag using default\n");
              break;
            }
          }
        }
        break;
      }
      case 'c': {
        // raise the affinity flag
        cflag = true;
        if(optarg != NULL) {
          char *endp;
          errno = 0;
          int num = (int) strtol(optarg, &endp, 0);
          if(num == 0) {
            log_fun(" !! Error could not convert value to allowed range: [1, %d]\n",
              core_count_avail);
            ret = false;
          } else if(num < 0 || num >= core_count_avail) {
            log_fun(" !! Error core id given (%d) larger than allowed (%d)\n", 
              num, core_count_avail);
            ret = false;
          } else {
            log_fun(" ** Valid affinity core id (%d) parsed, will try to set\n", num);
            def_cpu_core_id = num-1;
          }
        }
        break;
      }      
      // dump the allocation plan to a trace file
      case 'd': {
        // raise the dump flag
        dflag = true;
        break;
      }    
      // report progress steps
      case 'i': {
        iflag = true;
        if(optarg != NULL) {
          char *endp;
          errno = 0;
          int num = (int) strtol(optarg, &endp, 0);
          if(num <= 0) {
            log_fun(" !! Error: could not convert value to allowed range: [%d, +oo]\n",
              prog_steps_div);
            ret = false;
          } else {
            log_fun(" ** Valid progress step parsed %d, setting\n", num);
            prog_steps_div = num;
          }
        }
        break;
      }  
      // logging
      case 'l': {
        lflag = true;
        break;
      }
      // parse custom plan from file, if supplied
      case 'p': {
        // raise the custom plan flag
        pflag = true;
        // set the filename
        if(optarg == NULL) {
          log_fun(" !! Error, p flag requires a plan trace file as an argument\n");
          ret = false;
        } else {
          imp_fname = optarg;
        }
        break;
      }
      // parse plan size, if supplied
      case 't': {
        // raise t flag
        tflag = true;
        if(optarg == NULL) {
          log_fun(" !! Error, t requires an argument > 0\n");
          ret = false;
        } else {
          char *endp;
          long t_trials = strtol(optarg, &endp, 0);
          if(t_trials == 0) {
            log_fun(" !! Error, could not parse the supplied -t argument\n");
            ret = false;
          } else if(optarg != endp && *endp == '\0') {
            if(t_trials < min_trials) {
              log_fun(" !! Error: trial number given (%zu) is low, reverting to default %zu\n", 
                t_trials, min_trials);
            } else {
              log_fun(" ** Trials set to %zu\n", t_trials);
              bench_trials = (size_t) t_trials;
              // set the reporting steps to be every 10 % (def)
              prog_steps_div = bench_trials / prog_num_steps;
            }
          } else {
            log_fun(" !! Error: Invalid argument supplied, reverting to default\n");
            ret = false;
          }
        }
        break;
      }

      // handle arguments that require a parameter
      case '?': {
        log_fun(" !! Error: argument -%c, requires an parameter\n", optopt);
        log_fun(usage_str);
        ret = false;
        break;
      }
      default: {
        log_fun(usage_str);
        ret = false;
        break;
      }
    }
  }
  // check for concurrent flags
  if(pflag && tflag) {
    log_fun(" !! Error: cannot have both -p and -t at the same time\n");
    ret = false;
  }
  return ret;
}

/**
 * Function that allocates memory while also warming it up,
 * so we eliminate the OS management overhead.
 */
char *
alloc_mem(size_t size) {
  char *mem = NULL;
  if(size < min_pool_size) {
    log_fun(" !! Size was below min threshold which is %zu bytes\n", min_pool_size);
    return NULL;
  } else if((mem = calloc(sizeof(char), size)) == NULL) {
    log_fun(" !! Requested memory failed to yield, aborting\n");
    return NULL;
  } else {
    log_fun(" -- Ghostly allocated memory of size: %lf MB\n", 1.0*size / mb_div);
  }
  log_fun(" -- Warming up memory...\n");
  // forcefully request memory
  char *mem_addr = mem;
  for (int i = 0; i < size; ++i) {
    *mem_addr = 0;
    mem_addr++;
  }
  log_fun(" -- Returning warmed-up memory of size: %lf MB\n", 1.0*size / mb_div);
  return mem;
}

/**
 * This function is responsible for creating the tlsf pool structure 
 */
tlsf_t *
create_tlsf_pool(wtlsf_t *pool, size_t size) {
  // check for valid input
  if(pool == NULL) {
    log_fun(" !! No pool provided\n");
    return NULL;
  } 
  // check for valid size
  if(size < min_pool_size) {
    log_fun(" !! Pool must be at least of size %zu and requested: %zu\n", 
      min_pool_size, size);
  }
  // set the size
  pool->size = size;
  // try to allocate (warm) memory
  if((pool->mem = alloc_mem(pool->size)) == NULL) {
    log_fun(" !! Failed to allocate and warm-up memory, cannot continue\n");
    return NULL;
  }

  // now, actually try to create tlsf
  log_fun(" -- Attempting to create tlsf pool of size: %zu MB \n", 
    (1.0*pool->size)/mb_div);
  pool->tlsf_ptr = tlsf_create_with_pool(pool->mem, pool->size);
  if(pool->tlsf_ptr == NULL) {
    log_fun(" !! Failed to create tlsf pool\n");
  } else {
    log_fun(" -- Created a tlsf pool with size %zu MB\n", 
      (1.0*pool->size)/mb_div);
  }
  return pool->tlsf_ptr;
}

/**
 * @brief      This function is responsible for destroying the tlsf pool structure
 *
 * @param      pool  The tlsf pool pointer
 */
void
destroy_tlsf_pool(wtlsf_t *pool) {
  // check for valid input
  if(pool == NULL) {
    log_fun(" !! Cannot destroy tlsf pool, no valid pool provided\n");
    return;
  }
  log_fun(" -- Destroying tlsf pool of size %zu\n", pool->size);
  // first, destroy the pool
  if(pool->tlsf_ptr) {
    tlsf_destroy(pool->tlsf_ptr);
  }
  // then free the memory block from the OS
  if(pool->mem) {
    free(pool->mem);
    pool->mem = NULL;
  }
}


/**
 * @brief      Creates a tlsf original pool.
 *
 * @param      pool_ptr  The pool pointer
 * @param[in]  size      The size
 *
 * @return     returns the actual pointer to pool structure
 */
wtlsf_ori_t *
create_tlsf_ori_pool(wtlsf_ori_t *pool, size_t size) {
  // check for valid input
  if(pool == NULL) {
    log_fun(" !! No valid tlsf original pointer provided\n");
    return NULL;
  }

  // check for valid size
  if(size < min_pool_size) {
    log_fun(" !! Pool must be at be at least of size %zu, and requested %zu\n",
      min_pool_size, size);
  }
  // set the size
  pool->size = size;
  // try to allocate (warm) memory
  if((pool->mem = alloc_mem(pool->size)) == NULL) {
    log_fun(" !! Failed to allocate and warm-up memory, cannot continue\n");
    return NULL;
  } else {
    log_fun(" -- Created a tlsf original pool with size %zu bytes\n", pool->size);
  }
  // create the tlsf original pool;
  return init_tlsf_ori_pool(pool->size, pool->mem) == -1 ? NULL : pool;
}

/**
 * @brief      Destroy a tlsf original pool
 *
 * @param      pool  The pool pointer
 */
void
destroy_tlsf_ori_pool(wtlsf_ori_t *pool) {
  // check for valid input
  if(pool == NULL) {
    log_fun(" !! Cannot destroy tlsf ori pool, no valid pool pointer provided\n");
    return;
  }
  log_fun(" -- Destroying tlsf ori pool\n");
  // first delete the pool
  if(pool->mem) {
    del_tlsf_ori_pool(pool->mem);
  }
  // then free the memory block from the OS
  if(pool->mem) {
    free(pool->mem);
    pool->mem = NULL;
  }
}

/**
 *
 * TLSF bench start
 * 
 */

/**
 * @brief      The sequential benchmark routine, which executed the respective
 *             plan using either tlsf, tlsf original, and native allocators.
 *
 * @param      pool      The tlsf pool, if NULL it's skipped
 * @param      ori_pool  The original tlsf pool, if NULL it's skipped
 * @param      plan      A valid and initialized allocation plan
 */
void
bench_seq(wtlsf_t *pool, wtlsf_ori_t *ori_pool, alloc_plan_t *plan) {
  log_fun(" !! Running a SEQUENTIAL plan type of size: %zu\n", plan->plan_size);
  int mem_pivot = 0;
  double timed_seg = 0;
  unsigned long long t_ctx = 0;

  //printf("Pool NULL: %s, Ori Pool NULL: %s\n", pool == NULL ? "YES" : "NO", ori_pool == NULL ? "YES" : "NO");

  for (int i = 0; i < plan->plan_size; ++i) {
    t_ctx = tic(NULL);
    // check if we have an allocation
    if(plan->slot_type[i] == SLOT_MALLOC) {
      //log_fun(" -- Allocating block at %d with size %zu bytes\n", 
      //  i, plan->block_size[i]);
      if(pool != NULL) {
        plan->mem_ptr[mem_pivot] = tlsf_malloc(pool->tlsf_ptr, plan->block_size[i]);  
      } else if(ori_pool != NULL) {
        plan->mem_ptr[mem_pivot] = malloc_ex(plan->block_size[i], ori_pool->mem);
      } else {
        plan->mem_ptr[mem_pivot] = malloc(plan->block_size[i]);
      }
      plan->cur_malloc_size[mem_pivot] = plan->block_size[i];
      mem_pivot++;
    } else if(plan->slot_type[i] == SLOT_FREE) {
      int free_blk = (int) plan->block_id[i];
      //log_fun(" -- Freeing block at %d\n", free_blk);
      if(pool != NULL) {
        tlsf_free(pool->tlsf_ptr, plan->mem_ptr[free_blk]);
      } else if(ori_pool != NULL) {
        free_ex(plan->mem_ptr[free_blk], ori_pool->mem);
      } else {
        free(plan->mem_ptr[free_blk]);
      }
      // explicitly NULL the pointer
      plan->mem_ptr[free_blk] = NULL;
      //plan->cur_malloc_size[free_blk] = 0;
    } else {
      // error
      log_fun(" !! Error, encountered empty slot of a full plan\n");
    }
    // calculate timing delta
    timed_seg = toc(t_ctx, NULL, NULL);
    // add the timed segment to the timing array
    plan->timings[i] = timed_seg;
    // report progress
    if(i % prog_steps_div == 0) {
      cur_time = time(NULL);
      ctime_r(&cur_time, time_str_buf);
      // remove new line
      time_str_buf[strcspn(time_str_buf, "\r\n")] = 0;
      log_fun(" -- Progress: completed %d out of %zu ops (Current time: %s)\n", 
        i, plan->plan_size, time_str_buf);
    }
  }
  assert(mem_pivot == plan->plan_size / 2);
}

/**
 * Execute the ramp plan
 */
void
bench_ramp(wtlsf_t *pool, wtlsf_ori_t *ori_pool, alloc_plan_t *plan) {
  log_fun(" !! Running a RAMP plan type of size: %zu\n", plan->plan_size);
  //TODO
}

/**
 * Execute the hammer plan
 */
void 
bench_hammer(wtlsf_t *pool, wtlsf_ori_t *ori_pool, alloc_plan_t *plan) {
  log_fun(" !! Running a HAMMER plan type of size: %zu\n", plan->plan_size);
  //TODO
}

/**
 * Execute a scripted custom plan from file import
 */
void
bench_custom(wtlsf_t *pool, wtlsf_ori_t *ori_pool, alloc_plan_t *plan) {
  log_fun(" !! Running a CUSTOM plan type of size: %zu\n", plan->plan_size);
  int mem_pivot = 0;
  double timed_seg = 0;
  unsigned long long t_ctx = 0;
  for (int i = 0; i < plan->plan_size; ++i) {
    t_ctx = tic(NULL);
    if(plan->slot_type[i] == SLOT_MALLOC) {
      // allocate the block to the designated slot
      if(pool != NULL) {
        plan->mem_ptr[mem_pivot] = tlsf_malloc(pool->tlsf_ptr, 
        plan->block_size[i]);        
      } else if(ori_pool != NULL) {
        plan->mem_ptr[mem_pivot] = malloc_ex(plan->block_size[i], ori_pool->mem);  
      } else {
        plan->mem_ptr[mem_pivot] = malloc(plan->block_size[i]);
      }
      plan->cur_malloc_size[mem_pivot] = plan->block_size[i];
      mem_pivot++;
    } else if(plan->slot_type[i] == SLOT_FREE) {
      // free the block
      if(pool != NULL) {
        tlsf_free(pool->tlsf_ptr, plan->mem_ptr[plan->block_id[i]]);
      } else if(ori_pool != NULL) {
        free_ex(plan->mem_ptr[plan->block_id[i]], ori_pool->mem);
      } else {
        free(plan->mem_ptr[plan->block_id[i]]);
      }
      // explicitly NULL the pointer
      plan->mem_ptr[plan->block_id[i]] = NULL;
    } else {
      log_fun(" !! Error, encountered empty slot on a full plan\n");
    }
    // calculate timing delta
    timed_seg = toc(t_ctx, NULL, NULL);
    // add the timed segment to the timing array
    plan->timings[i] = timed_seg;
  }
  assert(mem_pivot == plan->plan_size / 2);
}

/**
 * This function benchmarks tlsf/native allocator using predefined plans which
 * are comprised out of malloc/free pairs.
 */
void
mem_bench(wtlsf_t *pool, wtlsf_ori_t *pool_ori, alloc_plan_t *plan) {
  // check if plan is null
  if(plan == NULL || plan->peak_alloc == 0 || plan->aggregated_alloc == 0) {
    log_fun(" !! Error allocation plan cannot be NULL, cannot continue\n");
    return;
  }

  // check if pool is null -- we either use native allocator or the tlsf pool
  if(pool == NULL && pool_ori == NULL) {
    log_fun(" ** Null pool detected, using native allocator\n");
  } else if (pool != NULL && plan->peak_alloc > pool->size) {
    log_fun(" !! Error, pool size of %lf MB is too small to satisfy peak allocation \
of %lf MB; cannot continue\n", pool->size/1.0*mb_div, plan->peak_alloc/1.0*mb_div);
    return;
  } else if(pool_ori != NULL && plan->peak_alloc > pool_ori->size) {
    log_fun(" !! Error, tlsf ori pool size of %lf MB is too small to satisfy peak \
allocation of %lf MB; cannot continue\n", 
(1.0*pool_ori->size)/mb_div, (1.0*plan->peak_alloc)/mb_div);
    return;
  }
  
  // basic info
  if(pool != NULL) {
    log_fun(" -- Running %zu ops with a tlsf pool size of %zu MB\n", 
      plan->plan_size, (1.0*pool->size)/mb_div);
  } else if(pool_ori != NULL) {
    log_fun(" -- Running %zu ops with a tlsf original pool size of %zu MB\n", 
      plan->plan_size, (1.0*pool_ori->size)/mb_div);
  } else {
    log_fun(" -- Running %zu ops using the native memory allocator\n", 
      plan->plan_size);
  }
  unsigned long long ctx = tic(NULL);
  // execute the sequential plan
  switch(plan->type) {
    case ALLOC_SEQ: {
      bench_seq(pool, pool_ori, plan);
      break;
    }
    case ALLOC_RAMP: {
      bench_ramp(pool, pool_ori, plan);
      break;
    }
    case ALLOC_HAMMER: {
      bench_hammer(pool, pool_ori, plan);
      break;
    }
    case ALLOC_CUSTOM: {
      bench_custom(pool, pool_ori, plan);
      break;
    }
    default: {
      bench_seq(pool, pool_ori, plan);
      break;
    }
  }
  // find the total elapsed time
  long double elapsed = toc(ctx, NULL, false);
  if(pool != NULL) {
    log_fun(" -- Finished %zu ops in pool, elapsed cycles for bench was %Le\n", 
      plan->plan_size, elapsed);
  } else {
    log_fun(" -- Finished %zu ops, elapsed cycles for bench was %Le\n", 
      plan->plan_size, elapsed);
  }
  log_fun(" -- xput: %Lf [malloc/free] ops/cycle\n", plan->plan_size/elapsed);
}

/**
 *
 * TLSF bench end
 * 
 */

/**
 * little hack to get the numbers showing with two digits even when it's < 10 which
 * is extremely useful to have ISO 8061 compliance when we create the timestamp
 */
void
num_to_str_pad(char *buf, int number) {
  // rudimentary error check...
  if(buf == NULL) {
    return;
  }
  if(number < 10 && number >= 0) {
    snprintf(buf, 3, "0%d", number);
  } else if (number < 99) {
    snprintf(buf, 3, "%d", number);
  }
}

/**
 * Creates a timestamp filename based on the ISO 8061 specification.
 */
char *
create_iso8061_ts(char *fname_buf) {
  time_t ctime;       // holds unix time
  struct tm *mytime;  // holds local time
  // get current time
  time(&ctime);
  // buffers
  char tm_mon_buf[3] = {0};
  char tm_mday_buf[3] = {0};
  char tm_hour_buf[3] = {0};
  char tm_min_buf[3] = {0};
  char tm_sec_buf[3] = {0};
  // get local time
  mytime = localtime(&ctime);

  // perform the conversion
  num_to_str_pad(tm_mon_buf, mytime->tm_mon+1); // offset month, due to range [0,11]
  num_to_str_pad(tm_mday_buf, mytime->tm_mday);
  num_to_str_pad(tm_hour_buf, mytime->tm_hour);
  num_to_str_pad(tm_min_buf, mytime->tm_min);
  num_to_str_pad(tm_sec_buf, mytime->tm_sec);  

  // finally format the timestamp to be in ISO 8061
  snprintf(fname_buf, MAX_FNAME_BUF, "%d%s%sT%s%s%sZ", 
    mytime->tm_year+1900, tm_mon_buf, tm_mday_buf,
    tm_hour_buf, tm_min_buf, tm_sec_buf);

  // finally return
  return fname_buf;
}

/**
 * Create a *full* path filename which includes the directory path, filename
 * as well as extension and returns it as a pointer.
 */
char *
create_full_fpath(char* dest, size_t buf_sz, 
  char *dir, char *fname, char *suffix, char *ext) {
  int ret = 0;
  // two different versions, one using a suffix and the other plain.
  if(suffix == NULL) {
    ret = snprintf(dest, buf_sz, "%s/%s.%s", dir, fname, ext);
  } else {
    ret = snprintf(dest, buf_sz, "%s/%s_%s.%s", dir, fname, suffix, ext);
  }
  // crate the filename and return (error is when snprintf return value is < 0)
  return ret < 0 ? NULL : dest;
}

/**
 * Create the directory
 */
bool
create_dir(char *dir_full_path) {
  struct stat st = {0};
  if(stat(dir_full_path, &st) == -1) {
    log_fun(" ** Specified dump directory does not exist, creating\n");
    if(mkdir(dir_full_path, 0700) != 0) {
      log_fun(" !! Could not create directory: %s\n", dir_full_path);
      return false;
    } else {
      log_fun(" ** Directory (%s) created successfully\n", dir_full_path);
    }
    log_fun(" ** Directory (%s) already exists\n", dir_full_path);
  }
  return true;
}

/**
 * Create the file to store the results
 */
FILE *
create_out_file(char *fname, char *suffix, char *ext, char *dir, char *tag, char *fpath_buf) {
  // try to create the directory, if needed
  if(!create_dir(dir)) {
    return NULL;
  }
  // create the full name
  char *full_path = create_full_fpath(fpath_buf, MAX_FPATH_BUF, 
    dir, fname, suffix, ext);
  // open the file
  FILE *fp = fopen(full_path, "w");
  // check for errors
  if(fp == NULL) {
    log_fun(" !! Error, could not open the file\n");
  } else {
    log_fun(" ** %s file %s open for writing\n", tag, full_path);
  }
  // return the file for writing
  return fp;
}

/**
 * Function that notified the user for trace dump file closure.
 */
void
close_tag_file(FILE *fp, char *tag) {
  if(fp != NULL) {
    log_fun(" ** %s file closed successfully\n", tag);
    // close the file
    fclose(fp);
  }
}

/**
 * Dump results into a plot-friendly format
 */
void
dump_plan(alloc_plan_t *plan, char *fname, char* suffix) {
  if(plan == NULL) {
    log_fun(" !! Error null plan supplied, cannot continue\n");
  } else {
    log_fun(" ** Dumping allocation plan details with size %zu\n", plan->plan_size);
  }
  FILE *fp = create_out_file(fname, suffix, dump_ext, dump_dir, "Trace dump", fpath_buf);
  // sanity check
  if(fp == NULL) {
    log_fun(" !! Error, null file pointer, cannot continue dump\n");
    return;
  }
  // write the plan size in the first line
  fprintf(fp, "%zu\n", plan->plan_size);
  // now write the header
  fprintf(fp, "op_type,chunk_size,block_id,exec_time\n");
  // loop and dump
  size_t chunk_size = 0;
  size_t free_chunk_size = 0;
  size_t blk_cnt = 0;
  double exec_time = 0;
  size_t block_id = 0;
  for (int i = 0; i < plan->plan_size; ++i) {
    char *op_type = NULL;
    chunk_size = plan->block_size[i];
    exec_time = plan->timings[i];
    block_id = plan->block_id[i];
    // differentiate allocation based on alloc/dealloc op
    if(plan->slot_type[i] == SLOT_FREE) {
      op_type = "free";
      free_chunk_size = plan->cur_malloc_size[block_id];
      fprintf(fp, "%s,%zu,%zu,%lf\n", op_type, free_chunk_size, block_id, exec_time);
    } else if(plan->slot_type[i] == SLOT_MALLOC) {
      op_type = "malloc";
      fprintf(fp, "%s,%zu,%zu,%lf\n", op_type, chunk_size, blk_cnt, exec_time);
      // sanity check
      assert(block_id == blk_cnt);
      // increment the block count
      blk_cnt++;
    } else {
      log_fun(" !! Error encountered empty slot on a full plan\n");
    }
  }
  // close the file
  close_tag_file(fp, "Trace file");
}

/** 
 * Function to check header
 */
bool
check_trace_header(char *header) {
  char *tok = NULL;
  // op_type token
  tok = strtok(header, tok_delim_cm);
  if(tok == NULL || strcmp(tok, "op_type") != 0) {
    log_fun(" !! Header seems invalid, first token needs to be 'op_type' cannot continue\n");
    return false;
  }
  // chunk_size token
  tok = strtok(NULL, tok_delim_cm); 
  if(tok == NULL || strcmp(tok, "chunk_size") != 0) {
    log_fun(" !! Header seems invalid, second token needs to be 'chunk_size' cannot continue\n");
    return false;
  } 
  // block_id token
  tok = strtok(NULL, tok_delim_nl);
  if(tok == NULL) {
    log_fun(" !! Header seems invalid, third token needs to be 'block_id' cannot continue\n");
    return false;
  } else if(strcmp(tok, "block_id") == 0) {
    log_fun(" -- Header seems valid, trying to parse plan\n");
    return true;
  } else if(strcmp(tok, "block_id,exec_time") == 0) {
    log_fun(" !! Header seems valid, but seems to be from output trace; using first 3 fields\n");
    parsing_out_traces = true;
    return true;
  } else {
    log_fun(" !! Header seems invalid, cannot continue\n");
    return false;
  }
}

/**
 * Function that parses each line for the given allocation plan
 *
 * This function expects each of the lines to be of the following
 * format:
 * 
 * op_type,chunk_size,block_id(,exec_time)
 *
 * Currently op_type, chunk_size, and block_id are mandatory. The field 
 * exec_time optional and is present so that we are able to load synthetic 
 * traces output which have an additional field for execution timing.
 *
 * Specification:
 *
 *  op_type: has to be either 'malloc' or 'free'
 *  chunk_size: for malloc has to be the block size
 *  block_id: the unique block id, there are as many block id's as many 
 *            allocations and the total amount of id's must be half of 
 *            the plan size
 *  (timings): the optional field which has timing measurements if it's an
 *             already executed plan. This field is *ignored*.
 */
bool
parse_trace_line(char *line, int *line_no, int *malloc_cnt, 
  size_t *cur_alloc, alloc_plan_t *plan) {
  char *tok = NULL;
  size_t tok_cnt = 1;
  slot_type_t slot = SLOT_EMPTY;
  
  // handle op_type
  tok = strtok(line, tok_delim_cm);
  if(tok == NULL) {
    log_fun(" !! Error, encountered at line %d, NULL token at position %zu\n",
      (*line_no) + 1, tok_cnt);
    return false;
  } else if(strcmp("malloc", tok) == 0) {
    //log_fun(" -- malloc op_type detected\n");
    slot = SLOT_MALLOC;
  } else if(strcmp("free", tok) == 0) {
    //log_fun(" -- free op_type detected\n");
    // handle free op_type
    slot = SLOT_FREE;
  } else {
    log_fun(" !! Error invalid op_type detected: '%s', expecting \
either 'malloc' or 'free'\n", tok);
    return false;
  }

  // increment token count
  tok_cnt++;

  // handle chunk_size
  tok = strtok(NULL, tok_delim_cm);
  long chunk_size = 0;
  if(tok == NULL) {
    log_fun(" !! Error, encountered at line %d, NULL token at position %zu\n",
      *line_no, tok_cnt);
    return false;
  }

  errno = 0;
  chunk_size = strtol(tok, NULL, 10);
  if(chunk_size <= 0 || chunk_size == ULLONG_MAX || errno == EINVAL) {
    log_fun(" !! Error, encountered at line %d, could not convert \
token '%s' at position %zu to 'size_t'\n", (*line_no) + 1, tok, tok_cnt);
    return false;
  } else {
    //log_fun(" -- chunk_size of %ld bytes, parsed\n", chunk_size);
  }

  // increment token count
  tok_cnt++;

  // handle block_id
  tok = strtok(NULL, tok_delim_nl);
  if(tok == NULL) {
    log_fun(" !! Error, encountered at line %d, NULL token at position %zu\n",
      (*line_no) + 1, tok_cnt);
    return false;
  }
  // check if we have four fields, in the case of out traces
  if(parsing_out_traces) {
    tok = strtok(tok, tok_delim_cm);
    if(tok == NULL) {
      log_fun(" !! Error, encountered at line %d, NULL token at position %zu\n",
        (*line_no) + 1, tok_cnt);
      return false;
    }
  }

  // parse the block
  long block_id = 0;
  errno = 0;
  block_id = strtol(tok, NULL, 10);
  if(block_id < 0 || ((block_id == 0 || block_id == ULONG_MAX) && errno == EINVAL)) {
    log_fun(" !! Error, encountered at line %d, could not convert \
token '%s' at position %zu to 'size_t'\n", (*line_no) + 1, tok, tok_cnt);
    return false;
  } else if(block_id > plan->plan_size/2) {
    log_fun(" !! Error, it appears block_id: %ld is larger than the \
allowed limit plan_size/2 (%zu)\n", block_id, plan->plan_size/2);
      return false;
  } else {
    //log_fun(" -- block_id %ld, parsed\n", block_id);
  }
  
  // calculate the index
  int cur_idx = (*line_no)-line_offset;
  plan->block_id[cur_idx] = block_id;
  plan->block_size[cur_idx] = chunk_size;
  if(plan->slot_type[cur_idx] != SLOT_EMPTY) {
    log_fun(" !! Error, encountered at line %d a non-empty slot in an unexpected \
position.\n", (*line_no) + 1);
    return false;
  } else if(slot == SLOT_MALLOC) {
    if(block_id != *malloc_cnt) {
      log_fun(" !! Error, encountered at line %d block_id (%ld) provided for \
malloc is not valid, expecting: %d\n", (*line_no) + 1, block_id, *malloc_cnt);
      return false;
    }
    plan->slot_type[cur_idx] = SLOT_MALLOC;       // set the slot type
    plan->malloc_tag_time[*malloc_cnt] = cur_idx; // set the malloc tagged time
    // increment the malloc counter
    (*malloc_cnt)++;
    // update the current allocation size
    *cur_alloc += chunk_size;
    // add the aggregated allocation size
    plan->aggregated_alloc += chunk_size;
  } else if(slot == SLOT_FREE) {
    // grab the corresponding malloc slot index
    int malloc_slot = plan->malloc_tag_time[block_id];
    // now check if the pointed 'malloc' has the same block id as the 'free' being parsed
    if(plan->block_id[malloc_slot] != block_id) {
      log_fun(" !! Error, encountered at line %d block_id for respective \
malloc (%zu)/free (%zu) do not match.\n", 
      (*line_no) + 1, plan->block_id[malloc_slot], block_id);
      return false;
    }
    plan->slot_type[cur_idx] = SLOT_FREE;         // set the slot type
    // update the current allocation size
    *cur_alloc -= chunk_size;
  } else {
    log_fun(" !! Error, encountered at line %d an unexpected empty-slot type.\n", 
      (*line_no) + 1);
    return false;
  }

  // check if we need to adjust out peak allocation size
  if(plan->peak_alloc < *cur_alloc) {
    plan->peak_alloc = *cur_alloc;
  }

  // after all the checks, finally return.
  return true;
}

/**
 * Parse the plan size
 */
bool
parse_plan_size(char *line, alloc_plan_t *plan) {
  long ret;
  errno = 0;
  ret = strtol(line, NULL, 10);
  if(ret <= 0 || ret == ULONG_MAX || errno == EINVAL) {
    log_fun(" !! Error, could not parse the plan size number in the first line\n");
    return false;
  } else {
    log_fun(" !! Parsed plan size of %zu\n", ret);
    plan->plan_size = (size_t) ret;
    // preallocate the plan
    return perform_plan_prealloc(plan);
  }
}

/** 
 * Load allocation plan from trace file
 */
alloc_plan_t *
import_alloc_plan(char *fname, alloc_plan_t *plan) {
  // check if filename is null
  if(fname == NULL) {
    log_fun(" !! Error, cannot have null filename, cannot continue\n");
    return NULL;
  }
  // check if plan is null
  if(plan == NULL) {
    log_fun(" !! Error, cannot have a null allocation plan, cannot continue\n");
    return NULL;
  }
  // open the file
  FILE *fp = fopen(fname, "r");
  if(fp == NULL) {
    log_fun(" !! Error, failed to open the file at: %s for reading\n", fname);
    return NULL;
  }
  plan->peak_alloc = 0;
  plan->aggregated_alloc = 0;
  // initialize file parsing variables
  char *fline = NULL;
  size_t llen = 0;
  size_t bytes_read = 0;
  size_t cur_alloc = 0;
  int lcnt = 0;
  int malloc_cnt = 0;
  bool ret = false;
  // loop through the file
  while((bytes_read = getline(&fline, &llen, fp)) != -1) {
    //log_fun(" -- Parsing line %d of %zu bytes\n", lcnt+1, bytes_read);
    // checks for parse type
    if(lcnt == 0) {
      // parse first line, which preallocates the plan
      ret = parse_plan_size(fline, plan);
    } else if(lcnt == 1) {
      // parse the csv header
      ret = check_trace_header(fline);
    } else {
      // parse the actual traces
      ret = parse_trace_line(fline, &lcnt, &malloc_cnt, &cur_alloc, plan);
    }
    // check for possible errors
    if(!ret) {
      log_fun(" !! Fatal parse error encountered at line %d, aborting\n", lcnt+1);
      break;
    }
    // increment the line counter
    lcnt++;
  }

  // perform the final checks
  if(lcnt-line_offset != plan->plan_size) {
    log_fun(" !! Error, it appears that import file ops (%d) are \
more than the parsed plan size (%zu)\n", lcnt-line_offset, plan->plan_size);
    ret = false;
  } else if(malloc_cnt != plan->plan_size / 2) {
    log_fun(" !! Error, it appears that malloc counts (%d) is not \
equal to half plan size (%zu) \n", malloc_cnt, plan->plan_size / 2);
    ret = false;
  } 

  // if all went well, set the plan type to be custom
  plan->type = ALLOC_CUSTOM;

  // clean up the line
  if(fline) {
    free(fline);
  }
  // check what to return
  if(!ret) {
    destroy_alloc_plan(plan);
    return NULL;
  } else {
    return plan;
  }
}

/**
 * Print allocation plan details (mostly for debug reasons)
 */
void
print_plan(alloc_plan_t *plan) {  
  size_t chunk_size = 0;
  size_t free_chunk_size = 0;
  size_t blk_cnt = 0;
  size_t block_id = 0;
  double exec_time = 0;
  log_fun(" ** Printing allocation plan details (size %zu)\n", plan->plan_size);
  for (int i = 0; i < plan->plan_size; ++i) {
    char *op_type = NULL;
    chunk_size = plan->block_size[i];
    exec_time = plan->timings[i];
    //log_fun("\t%s, %zu, %lf, %zu (%d)\n", op_type, chunk_size, exec_time, idx, i);
    // differentiate allocation based on alloc/dealloc op
    if(plan->slot_type[i] == SLOT_FREE) {
      op_type = "free";
      free_chunk_size = plan->cur_malloc_size[block_id];
      log_fun("\t%s, %zu, %lf, %zu (%d)\n", op_type, free_chunk_size, exec_time, block_id, i);
    } else if(plan->slot_type[i] == SLOT_MALLOC) {
      op_type = "malloc";
      log_fun("\t%s, %zu, %lf, %zu (%d)\n", op_type, chunk_size, exec_time, block_id, i);
      // sanity check
      assert(blk_cnt == block_id);
      // increment block
      blk_cnt++;
    } else {
      log_fun(" !! Error, encountered empty slot on a full plan");
    }
  }
  log_fun(" ** End of allocation plan details print\n");
}

/**
 * Function that is a stub for executing the allocation plan
 * either by using one of the built in ones or from parsing
 * an existing trace.
 */
void 
execute_plan(use_alloc_type_t alloc_type) {
  // start hint
  if(alloc_type == USE_TLSF) {
    log_fun("\n ## Executing plan using tlsf allocator\n\n");    
  } else if(alloc_type == USE_TLSF_ORI) {
    log_fun("\n ## Executing plan using tlsf (original) allocator\n\n");
  } else if(alloc_type == USE_NATIVE) {
    log_fun("\n ## Executing plan using native allocator\n\n");   
  } else {
    log_fun("\n !! Unknown allocation type -- cannot continue plan execution.\n\n");
    return;
  }

  // return value
  bool ret = true;
  // our plan structure
  alloc_plan_t plan = {0};

  // parse plan or generate one
  if(pflag) {
    ret = import_alloc_plan(imp_fname, &plan);
  } else {
    ret = gen_alloc_plan(bench_trials, &plan);
  }

  // check for result
  if(!ret) {
    log_fun(" !! Error: could not generate a valid plan -- aborting\n");
  }

  // now run the experiment
  char *ctag = "Global Tag cycles";
  char *stag = "Timer tag";
  clock_t s_ctx = tic_s(stag);
  unsigned long long c_ctx = tic(ctag);

  if(alloc_type == USE_NATIVE) {
    // use the native allocator
    mem_bench(NULL, NULL, &plan);
  } else if(alloc_type == USE_TLSF) {
    // declare our pool structure
    wtlsf_t pool = {0};
    // create the pool
    if(create_tlsf_pool(&pool, pool_size) == NULL) {
      log_fun(" !! Error: fatal error encountered when creating the pool\n");
      ret = false;
    } else {  
      // now run the bench
      mem_bench(&pool, NULL, &plan);
      // destroy the pool
      destroy_tlsf_pool(&pool);
    }
  } else if(alloc_type == USE_TLSF_ORI) {
    // declare our pool structure
    wtlsf_ori_t tlsf_ori_pool = {0};
    if(create_tlsf_ori_pool(&tlsf_ori_pool, pool_size) == NULL) {
      log_fun(" !! Error: fatal error encountered when creating the tlsf_ori pool\n");
      ret = false;
    } else {
      // now run the bench for tlsf original
      mem_bench(NULL, &tlsf_ori_pool, &plan);
      // destroy the pool
      destroy_tlsf_ori_pool(&tlsf_ori_pool);      
    }
  }

  // timing point
  toc(c_ctx, ctag, true);
  toc_s(s_ctx, stag, true);
  //print_plan(&plan);

  // dump the plan
  if(dflag && ret) {
    if(alloc_type == USE_NATIVE) {
      dump_plan(&plan, fname_buf, dump_native_trace_suffix); 
    } else if(alloc_type == USE_TLSF) {
      dump_plan(&plan, fname_buf, dump_tlsf_trace_suffix); 
    } else if(alloc_type == USE_TLSF_ORI) {
      dump_plan(&plan, fname_buf, dump_tlsf_ori_trace_suffix);  
    }
  }
  
  // finally destroy the allocation plan
  destroy_alloc_plan(&plan);
  // finish hint
  if(alloc_type == USE_TLSF) {
    log_fun("\n ## Finished executing plan using tlsf allocator\n");    
  } else if(alloc_type == USE_TLSF_ORI) {
    log_fun("\n ## Finished executing plan using tlsf (original) allocator\n");
  } else if(alloc_type == USE_NATIVE) {
    log_fun("\n ## Finished executing plan using native allocator\n");   
  }
}

/**
 * Function that pins the current thread to a certain CPU id as 
 * defined by `core_id`
 * @param  core_id cpu id to pin
 * @return true if successful, false otherwise
 */
bool cpu_pin(int core_id) {
  int ret = 0;
  pthread_t tid;
  cpu_set_t cpu_set;
  // initialize the pthread to be self
  tid = pthread_self();
  // zero out the affinity mask
  CPU_ZERO(&cpu_set);
  // set the process affinity to be on core_id
  CPU_SET(core_id, &cpu_set);
  // try to set the affinity of the thread
  ret = pthread_setaffinity_np(tid, sizeof(cpu_set_t), &cpu_set);
  // check the result
  if(ret != 0) {
    log_fun(" !! Error, could not set affinity on core %d with internal id: %d\n", 
      core_id+1, core_id);
  } else {
    log_fun(" ** Affinity set successful; using core %d with internal id: %d\n", 
      core_id+1, core_id);
  }
  return ret != 0;
}

/**
 * Enumerate available cores
 */
void
enum_cpu_cores() {
  core_count = get_nprocs_conf(); 
  core_count_avail = get_nprocs();
  log_fun(" ** Detected %d number of cores out of which usable are: %d\n", 
    core_count, core_count_avail);
}

/**
 * Stub to run our benchmark types (tlsf, native, or both)
 */
void
run_bench() {
  // handle affinity, if enabled
  if(cflag) {
    cpu_pin(def_cpu_core_id);
  } else {
    log_fun(" ** Using OS scheduled core affinity\n");
  }

  // benchmark type to run, currently we have three types
  // tlsf, native, or both.
  switch(bench_type) {
    case BENCH_TLSF: {
      // use tlsf allocator
      execute_plan(USE_TLSF);
      break;
    }
    case BENCH_NATIVE: {
      // use native allocator
      execute_plan(USE_NATIVE);
      break;
    }
    case BENCH_TLSF_ORI: {
      // use tlsf original allocator
      execute_plan(USE_TLSF_ORI);
      break;
    }
    case BENCH_ALL: {
      // use native allocator
      execute_plan(USE_NATIVE);
      // use tlsf allocator
      execute_plan(USE_TLSF);
      // use tlsf original allocator
      execute_plan(USE_TLSF_ORI);
      break;
    }
    default: {
      // use tlsf allocator
      execute_plan(USE_TLSF);
      break;
    }
  }
}

/**
 * This bootstraps the logging functionality and opens the respective file
 * for writing
 */
bool
bootstrap_logging(char *fname, char *suffix) {
  log_fun(" -- Logging to file is: %s\n", lflag ? "ENABLED" : "DISABLED");
  // return if we don't want logging
  if(!lflag) {
    return true;
  }
  // else create the required files
  if(log_fp != NULL) {
    log_fun(" !! Warning: non-null logging file pointer found, closing\n");
    fclose(log_fp);
  }
  // try to create the file
  log_fp = create_out_file(fname, suffix, log_ext, log_dir, "Logging", fpath_buf);
  if(log_fp == NULL) {
    log_fun(" !! Error: null file pointer on log creation, logging will be DISABLED\n");
    lflag = false;
    return false;
  } else {
    log_fun(" ** Output logging to file started now\n\n");
  }
  return true;
}

/**
 * Handle general initialization stuff.
 */
bool
bootstrap(int argc, char **argv) {
  log_fun("\n");
  // initialize the random number generator
  srand(2);
  // set the amount of cpu cores
  enum_cpu_cores();
  // parse arguments
  if(!parse_args(argc, argv)) {
    return false;
  } 
  // create the timestamp filename for use in logging and dump plan
  create_iso8061_ts(fname_buf);
  // boostrap logging
  bootstrap_logging(fname_buf, NULL);
  // notify of dump flag
  log_fun(" ** Dumping traces is: %s\n", dflag ? "ENABLED" : "DISABLED");
  return true;
}

/**
 * Final cleanup actions
 */
void
cleanup() {
  // clean up logging pointer if valid
  close_tag_file(log_fp, "Logging file");
}

/**
 * Main stub
 */
int
main(int argc, char **argv) {
  if(!bootstrap(argc, argv)) { 
    return EXIT_FAILURE; 
  }
  // run our benchmark
  run_bench();
  // final cleanup
  cleanup();
  // finally, return
  return EXIT_SUCCESS;
}
