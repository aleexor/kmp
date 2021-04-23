#ifndef KMP_MPI_HEADER
#define KMP_MPI_HEADER

#include "common.h"

/**
 * Capture network traffic and apply KMP search on capture packets using MPI.
 * @param app App
 */
void live_capture_mpi(const app_t* app);

/**
 * Apply KMP search on the provided text using MPI.
 * @param sz_text Text to search in
 * @param len Text length
 * @param app App
 */
void kmp_search_mpi(const char* sz_text, const int len, const app_t* app);

#endif