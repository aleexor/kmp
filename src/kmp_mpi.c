#include <mpi/mpi.h>

#include <argp.h>
#include <stdio.h>
#include <stdlib.h>
#include <mpi/mpi.h>
#include <string.h>
#include <math.h>
#include <pcap.h>
#include <omp.h>

#include "kmp_mpi.h"
#include "app.h"

#define TAG_PROCESS_CHUNK 1
#define TAG_SHUTDOWN 255

/**
 * Deallocate memory.
 * @param wordsp No. of words
 * @param sz_wordsp Strings vector
 * @param words_lengthp Length vector
 */
void free_data(int* wordsp, char*** sz_wordsp, int** words_lengthp);

/**
 * Brodcast data to each worker.
 * @param rank Process rank
 * @param wordsp No. of words
 * @param sz_wordsp Strings vector
 * @param words_lengthp Lenght vector
 * @param app
 */
void broadcast_data(int rank, int* wordsp, char*** sz_wordsp, int** words_lengthp, const app_t* app);

/**
 * Process chunk of data (worker).
 * @param rank Process rank
 * @param worker Communicator for workers
 * @param words No. of words
 * @param sz_words Strings vector
 * @param app App
 */
void process_mpi_data(int rank, MPI_Comm worker, const int words, char** sz_words, const app_t* app);

/**
 * Apply KMP search on the provided text using MPI.
 * @param sz_text Text to search in
 * @param len Text length
 * @param app App
 */
void kmp_search_impl_mpi(const char* sz_text, const int len, const app_t* app);

void live_capture_mpi(const app_t* app)
{
    int rank; // Current process rank
    int comm_sz; // No. of processors

    MPI_Comm_size(MPI_COMM_WORLD, &comm_sz);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // Communicator for non-master processes.
    MPI_Comm worker;
    MPI_Comm_split(MPI_COMM_WORLD, (rank == 0), rank, &worker);

    int words = 0; // No. of words
    char** sz_words = NULL; // Words to match
    int* words_length = NULL; // Length of each word (required for broadcasting)

    // broadcast necessary data for worker s(since app is available only to master process)
    broadcast_data(rank, &words, &sz_words, &words_length, app);

    // network capture is processed only by master process
    if (rank == 0)
        live_capture(kmp_search_impl_mpi, app);

    // process data
    else
        process_mpi_data(rank, worker, words, sz_words, app);

    // free allocated memory
    free_data(&words, &sz_words, &words_length);
}

void kmp_search_mpi(const char* sz_text, const int len, const app_t* app)
{
    int rank; // Current process rank
    int comm_sz; // No. of processors

    MPI_Comm_size(MPI_COMM_WORLD, &comm_sz);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // Communicator for non-master processes.
    MPI_Comm worker;
    MPI_Comm_split(MPI_COMM_WORLD, (rank == 0), rank, &worker);

    int words = 0; // No. of words
    char** sz_words = NULL; // Words to match
    int* words_length = NULL; // Length of each word (required for broadcasting)

    // broadcast necessary data for worker s(since app is available only to master process)
    broadcast_data(rank, &words, &sz_words, &words_length, app);

    if (rank == 0)
        kmp_search_impl_mpi(sz_text, len, app);

    // process data
    else
        process_mpi_data(rank, worker, words, sz_words, app);

    if (app->verbose) printf("Shutdown received (rank=%d).\n", rank);

    // free allocated memory
    free_data(&words, &sz_words, &words_length);

    // free communicator
    MPI_Comm_free(&worker);
}

void kmp_search_impl_mpi(const char* sz_text, const int len, const app_t* app)
{
    int rank; // Current process rank
    int comm_sz; // No. of processors
    MPI_Status status; // Receive status
    MPI_Request request;// Send request status

    MPI_Comm_size(MPI_COMM_WORLD, &comm_sz);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // get chunk size
    int chunk_size = (int)ceil((double)len / (comm_sz - 1));

    if (chunk_size > MAX_BUFFER_SIZE)
        chunk_size = MAX_BUFFER_SIZE;

    if (comm_sz > 1 && len > 0)
    {
        int data_processed = 0; // amount of data processed

        // send a chunk to every process
        for (int proc_rank = 1; proc_rank < comm_sz; ++proc_rank)
        {
            // calculate the start position of the chunk to send
            int start_pos = chunk_size * (proc_rank - 1);

            // the amount of data to send (the latest process may receive smaller chunk of data)
            int data_count = start_pos + chunk_size > len ? len - start_pos >= 0 ? len - start_pos : 0 : chunk_size;

            if (app->verbose) printf("Sending data to %d (data_count=%d)\n", proc_rank + 1, data_count);

            // send chunk of data
            MPI_Isend(sz_text + start_pos, data_count, MPI_CHAR, proc_rank, TAG_PROCESS_CHUNK, MPI_COMM_WORLD, &request);

            // increase processed data
            data_processed += data_count;
        }

        int* result = (int*)malloc(sizeof(int) * comm_sz * app->words);

        // set each word as not found
        for (int proc_rank = 0; proc_rank < comm_sz; ++proc_rank)
            for (int idx = 0; idx < app->words; ++idx)
                result[proc_rank * app->words + idx] = -1;

        // process remaining data (> 0 when chunk_size > MAX_BUFFER_SIZE)
        int not_processed_data = len - data_processed;

        for (int idx = 0; idx < app->words; ++idx)
        {
            kmp_search_t kmp_word = app->kmp_words[idx];
            int start_pos = not_processed_data - kmp_word.length;

            // ignore
            if (start_pos < 0)
                continue;

            int position = kmp_search(sz_text + start_pos, len - start_pos, &kmp_word);

            if (position != -1)
            {
                result[idx] = position + start_pos;
                continue;
            }
        }

        // check if words are between chunks (cannot be resolved by children processes, it may be expensive)
        // proc_rank[1]: aaabbcc
        // proc_rank[2]:        ddeeff
        // word:             bccdd
        //                    ^^^^
        for (int idx = 0; idx < app->words; ++idx)
        {
            kmp_search_t kmp_word = app->kmp_words[idx];

            for (int proc_rank = 1; proc_rank < comm_sz - 1; ++proc_rank)
            {
                // calculate the start and the end position of the (missed) chunk
                int start_pos = chunk_size * (proc_rank - 1) + chunk_size - kmp_word.length + 1; // one character (the latest one) is part of the next chunk

                // since the word is bigger than chunk size
                // the word spans across multiple chunks
                if (kmp_word.length > chunk_size)
                    start_pos = chunk_size * (proc_rank - 1);

                int end_pos = chunk_size * (proc_rank - 1) + chunk_size + kmp_word.length - 1; // one character (the first one) is part of current chunk
                int data_count = end_pos - start_pos;

                // illegal statement
                if (start_pos < 0)
                    continue;

                // length overflow check
                if (start_pos + data_count > len)
                    data_count = len - start_pos;

                int position = kmp_search(sz_text + start_pos, data_count, &kmp_word);

                // update with the correct position (since we are applying kmp on a portion of the entire string)
                if (position != -1)
                {
                    position += start_pos;
                    result[idx] = position;
                    break; // first occurrence is enough
                }
            }
        }

        // receive the result from working processes (if any)
        for (int proc_rank = 1; proc_rank < comm_sz; ++proc_rank)
        {
            if (app->verbose) printf("Waiting data from %d\n", proc_rank+1);
            MPI_Recv(&(result[proc_rank * app->words]), app->words, MPI_INT, proc_rank, TAG_PROCESS_CHUNK, MPI_COMM_WORLD, &status);
            if (app->verbose) printf("Data received from %d.\n\n", status.MPI_SOURCE + 1);
        }

        if (app->verbose) printf("Data received.\n");

        for (int proc_rank = 0; proc_rank < comm_sz; ++proc_rank)
        {
            int* positions = result + proc_rank * app->words;

            for (int i = 0; i < app->words; ++i) {
                int position = positions[i];
                kmp_search_t kmp_word = app->kmp_words[i];

                if (position != -1)
                {
                    // update position (the received position is relative to the chunk processed from worker)
                    if (proc_rank != 0)
                    {
                        int start_pos = chunk_size * (proc_rank - 1);
                        position = positions[i] + start_pos;
                    }

                    if (!app->state->matched_words[i])
                    {
                        // update word status
                        app->state->matched_words[i] = 1;
                        app->state->matches++;

                        // notify match
                        notify_match(sz_text, len, &kmp_word, app);
                    }
                }
            }
        }

        // shutdown workers
        if (!app->state->capture)
        {
            if (app->verbose) printf("Shutdown started (no capture...).\n");

            for (int proc_rank = 1; proc_rank < comm_sz; ++proc_rank)
                MPI_Isend(NULL, 0, MPI_CHAR, proc_rank, TAG_SHUTDOWN, MPI_COMM_WORLD, &request);
        }

        free(result);
    }
    else if (len > 0)
    {
        // execute the single-threaded version (no workers)
        kmp_search_impl(sz_text, len, app);
    }
}

void process_mpi_data(int rank, MPI_Comm worker, const int words, char** sz_words, const app_t* app)
{
    kmp_search_t* kmp_words; // kmp failure tables
    int length; // amount of data received
    char buf[MAX_BUFFER_SIZE]; // buffer for receive
    MPI_Status status; // receive status

    // initialize kmp for each worker
    kmp_words = (kmp_search_t*)malloc(sizeof(kmp_search_t) * words);

    for (int idx = 0; idx < words; ++idx) {
        kmp_search_t kmp_word;
        kmp_search_init(sz_words[idx], &kmp_word);
        kmp_words[idx] = kmp_word;
    }

    // wait to process data
    while (1)
    {
        // printf("Waiting from master: %d\n", rank+1);

        // wait for chunk of data
        MPI_Recv(buf, MAX_BUFFER_SIZE, MPI_CHAR, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

        // printf("Received from master: %d\n\n", rank+1);

        // wait for all processes to receive data
        MPI_Barrier(worker);

        // shutdown worker
        if (status.MPI_TAG == TAG_SHUTDOWN)
            break;

        // get chunk size and process chunk
        if (MPI_Get_count(&status, MPI_CHAR, &length) == MPI_SUCCESS)
        {
            //printf("Receive chuck from master process (count=%d, words=%d).\n", length, app.words);
            int* positions = (int*)malloc(sizeof(int) * words);

            for (int idx = 0; idx < words; ++idx)
            {
                kmp_search_t kmp_word = kmp_words[idx];
                positions[idx] = kmp_search(buf, length, &kmp_word);
            }

            MPI_Send(positions, words, MPI_INT, 0, TAG_PROCESS_CHUNK, MPI_COMM_WORLD);
            MPI_Barrier(worker);

            free(positions);
        }
    }

    // destroy kmp for each worker
    for (int idx = 0; idx < words; ++idx) {
        kmp_search_t kmp_word = kmp_words[idx];
        kmp_search_destroy(&kmp_word);
    }

    free(kmp_words);
}

void broadcast_data(int rank, int* wordsp, char*** sz_wordsp, int** words_lengthp, const app_t* app)
{
    int words = *wordsp;
    char** sz_words = *sz_wordsp;
    int* words_length = *words_lengthp;

    if (rank == 0)
    {
        words = app->words;
        sz_words = (char**)malloc(sizeof(char*) * app->words);
        words_length = (int*)malloc(sizeof(int) * app->words);

        for (int idx = 0; idx < words; ++idx)
        {
            words_length[idx] = app->kmp_words[idx].length + 1;
            sz_words[idx] = (char*)malloc(sizeof(char) * words_length[idx]);
            strncpy(sz_words[idx], app->kmp_words[idx].sz_word, words_length[idx]);
        }
    }

    // communicate the no. of words to processes
    MPI_Bcast(&words, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // allocate memory for words and lengths
    if (rank != 0)
    {
        sz_words = (char**)malloc(sizeof(char*) * words);
        words_length = (int*)malloc(sizeof(int) * words);
    }

    // broadcast array of lenghts
    MPI_Bcast(words_length, words, MPI_INT, 0, MPI_COMM_WORLD);

    // broadcast each word
    for (int idx = 0; idx < words; ++idx)
    {
        // allocate memory for the word
        if (rank != 0)
            sz_words[idx] = (char*)malloc(sizeof(char) * words_length[idx]);

        MPI_Bcast(sz_words[idx], words_length[idx], MPI_CHAR, 0, MPI_COMM_WORLD);
    }

    // wait for all workers
    MPI_Barrier(MPI_COMM_WORLD);

    *wordsp = words;
    *sz_wordsp = sz_words;
    *words_lengthp = words_length;
}

void free_data(int* wordsp, char*** sz_wordsp, int** words_lengthp)
{
    char** sz_words = *sz_wordsp;
    int* words_length = *words_lengthp;

    // free memory of each word
    for (int idx = 0; idx < *wordsp; ++idx)
    {
        free(sz_words[idx]);
        sz_words[idx] = 0;
    }

    // free arrays
    free(words_length);
    free(sz_words);

    *words_lengthp = NULL;
    *sz_wordsp = NULL;
    *wordsp = 0;
}