#ifndef COMMON_HEADER
#define COMMON_HEADER

#include <stdbool.h>
#include "kmp.h"

#define MAX_BUFFER_SIZE 1024 * 1024 * 4

#define KMP_SINGLE_THREAD 0
#define KMP_OPENMP 1
#define KMP_MPI 2

/**
 * App state
 */
typedef struct app_state_t
{
    int capture; // status of network capturing
    int* matched_words; // array of matched words
    int matches; // no. of matched words
} app_state_t;

/**
 * App
 */
typedef struct app_t
{
    int threads; // no. of threads to be used (openMP specific)
    char* device; // device to listen
    
    int words; // no. of words in input
    kmp_search_t* kmp_words; // array of failure tables

    app_state_t* state; // state of the application
    int version; // type of execution (single thread, openMP, MPI)

    int verbose; // verbose mode (print debugging info)
} app_t;

#endif