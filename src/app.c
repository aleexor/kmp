#include <stdlib.h>
#include <string.h>
#include <pcap.h>
#include <argp.h>
#include <omp.h>
#include "app.h"
#include <math.h>
#include "kmp_mpi.h"

#include <mpi/mpi.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

static char doc[] = "kmp - check if TCP/UDP packets contains some words";

static struct argp_option options[] = {
    { "devices", 'l', 0, 0, "Print a list of available devices" },
    { "device", 'd', "[name]", 0, "Device to capture" },
    { "openmp", 'o', 0, 0, "Use the OpenMP variant of the application" },
    { "mpi", 'm', 0, 0, "Use the MPI variant of the application" },
    { "single", 's', 0, 0, "Use the single-thread variant of the application (default)" },
    { "text", 't', "[text]", 0, "Check if there is occurrences of the words given as input" },
    { "file", 'f', "[filename]", 0, "Check if there is occurrences of the words given as input in the file" },
    { "threads", 'p', "[no_threads]", 0, "No. of threads" },
    { 0 }
};

static error_t parse_opt(int key, char* arg, struct argp_state *state)
{
    int rank; // Current process rank
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    arguments_t* arguments = state->input;

    switch (key)
    {
        case 'l': // list of devices
            arguments->list_devices = 1;
            break;

        case 'd': // use device
            arguments->device = arg;
            break;

        case 'o': // OpenMP variant
            arguments->version = KMP_OPENMP;
            break;

        case 'm': // MPI variant
            arguments->version = KMP_MPI;
            break;

        case 's': // single-thread variant
            arguments->version = KMP_SINGLE_THREAD;
            break;

        case 't': // text to be searched
            arguments->sz_text = arg;
            break;

        case 'f': // file to be searched
            arguments->sz_filename = arg;
            break;

        case 'p': // no. of threads (openp variant only)
            arguments->threads = atoi(arg);
            break;

        case ARGP_NO_ARGS:
            if (rank == 0)
                argp_usage(state);
            break;

        case ARGP_KEY_ARG:
            arguments->sz_words = &state->argv[state->next - 1];
            state->next = state->argc;
            break;

        case ARGP_KEY_END:
            arguments->words = state->arg_num;
            bool optional_words = arguments->list_devices;
            bool show_usage = rank == 0 && arguments->words == 0 && !optional_words;

            if (show_usage)
                argp_usage(state);

            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

void parse_input(int argc, char** argv, arguments_t* arguments)
{
    // Prepare environment
    if (arguments == NULL)
    {
        printf("<parse_input> Null argument exception (arguments)");
        exit(EXIT_FAILURE);
    }

    struct argp argp = { options, parse_opt, "words...", doc };

    // Setting default values
    arguments->version = KMP_SINGLE_THREAD;
    arguments->list_devices = 0;
    arguments->device = NULL;
    arguments->threads = 1;
    arguments->words = 0;
    arguments->sz_words = NULL;
    arguments->sz_text = NULL;
    arguments->sz_filename = NULL;

    // Parse
    argp_parse(&argp, argc, argv, 0, 0, arguments);
}

void notify_match(const char* sz_text, const int len, const kmp_search_t* kmp_word, const app_t* app)
{
    printf("%s matches!\n", kmp_word->sz_word);
    printf("Matched words: %d of %d.\n", app->state->matches, app->words);

    if (app->state->matches == app->words)
    {
        printf("Complete (%d of %d).\n", app->state->matches, app->words);

        // since all words have been matched, shutdown network capturing.
        app->state->capture = 0;
    }
}

void init_app(app_t* app, const arguments_t* arguments)
{
    app->words = arguments->words;

    // build failure table for each word
    app->kmp_words = (kmp_search_t*)malloc(sizeof(kmp_search_t) * arguments->words);

    for (int idx = 0; idx < arguments->words; ++idx)
    {
        char* sz_word = arguments->sz_words[idx];

        kmp_search_t kmp_word;
        kmp_search_init(sz_word, &kmp_word);

        app->kmp_words[idx] = kmp_word;
    }

    app->threads = arguments->threads;
    app->version = arguments->version;
    app->device = arguments->device;

    // used for debugging purposes
    app->verbose = 0;

    // initialize app state (track progress)
    app->state = (app_state_t*)malloc(sizeof(struct app_state_t));
    app->state->matched_words = (int*)calloc(arguments->words, sizeof(int));
    app->state->matches = 0;
    app->state->capture = 0;
}

void destroy_app(app_t* app)
{
    for (int idx = 0; idx < app->words; ++idx)
        kmp_search_destroy(&app->kmp_words[idx]);

    free(app->kmp_words);
    app->words = 0;

    // destroy app state
    free(app->state->matched_words);
    free(app->state);
}

void print_available_devices()
{
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_if_t* alldevsp, *device;

    if (pcap_findalldevs(&alldevsp, errbuf) == -1) {
        printf("Error finding devices: %s\n", errbuf);
        return;
    }

    printf("Available devices:\n");
    for (device = alldevsp; device != NULL; device = device->next)
    {
        if (device->name != NULL)
            printf("  - %s\n", device->name);
    }

    pcap_freealldevs(alldevsp);
}

void process_packet(const struct pcap_pkthdr *header, const u_char *packet, kmp_handler callback, const app_t* app)
{
    struct ether_header* eth_header = (struct ether_header*)packet;
    if (ntohs(eth_header->ether_type) != ETHERTYPE_IP) {
        return;
    }

    const u_char *ip_header;
    const u_char *tcp_header;
    const u_char *payload;

    int ethernet_header_length = 14;
    int ip_header_length;
    int tcp_header_length;
    int payload_length;

    /* Find start of IP header */
    ip_header = packet + ethernet_header_length;
    /* The second-half of the first byte in ip_header
       contains the IP header length (IHL). */
    ip_header_length = ((*ip_header) & 0x0F);
    /* The IHL is number of 32-bit segments. Multiply
       by four to get a byte count for pointer arithmetic */
    ip_header_length = ip_header_length * 4;

    /* Now that we know where the IP header is, we can
       inspect the IP header for a protocol number to
       make sure it is TCP before going any further.
       Protocol is always the 10th byte of the IP header */
    u_char protocol = *(ip_header + 9);
    if (protocol != IPPROTO_TCP && protocol != IPPROTO_UDP) {
        //printf("Not a TCP nor UDP packet. Skipping...\n\n");
        return;
    }

    /* Add the ethernet and ip header length to the start of the packet
       to find the beginning of the TCP header */
    tcp_header = packet + ethernet_header_length + ip_header_length;
    /* TCP header length is stored in the first half
       of the 12th byte in the TCP header. Because we only want
       the value of the top half of the byte, we have to shift it
       down to the bottom half otherwise it is using the most
       significant bits instead of the least significant bits */
    tcp_header_length = ((*(tcp_header + 12)) & 0xF0) >> 4;
    /* The TCP header length stored in those 4 bits represents
       how many 32-bit words there are in the header, just like
       the IP header length. We multiply by four again to get a
       byte count. */
    tcp_header_length = tcp_header_length * 4;

    /* Add up all the header sizes to find the payload offset */
    int total_headers_size = ethernet_header_length+ip_header_length+tcp_header_length;
    payload_length = header->caplen -
                     (ethernet_header_length + ip_header_length + tcp_header_length);
    payload = packet + total_headers_size;

    const char* sz_text = (char*) payload;
    callback(sz_text, payload_length, app);
}

// kmp specific pcap_loop implementation
int kmp_pcap_loop(pcap_t *handle, kmp_handler callback, const app_t* app)
{
    const u_char *packet;
    struct pcap_pkthdr packet_header;

    while (app->state->capture) {
        packet = pcap_next(handle, &packet_header);
        if (packet == NULL) {
            printf("No packet found.\n");
            return 2;
        }

        process_packet(&packet_header, packet, callback, app);
    }

    return 0;
}

int live_capture(kmp_handler callback, const app_t* app)
{
    if (app->device == NULL) {
        printf("Unknown device name. Live capture cannot be initialized.\n");
        print_available_devices();
        return -1;
    }

    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_if_t* alldevsp, *dev, *device = NULL; // Devices linked list

    int packet_count_limit = 1; // promiscuous mode
    int timeout_limit = 10000; // in milliseconds

    // Find all available devices
    if (pcap_findalldevs(&alldevsp, errbuf) == -1) {
        printf("Error finding devices: %s\n", errbuf);
        return -1;
    }

    // Look for specified device
    for (dev = alldevsp; dev != NULL; dev = dev->next)
    {
        if (dev->name != NULL && strcmp(dev->name, app->device) == 0)
        {
            device = dev;
            break;
        }
    }

    // Specified device cannot be found
    if (device == NULL)
    {
        printf("Error finding device %s.\n", app->device);
        print_available_devices();
        return -1;
    }

    // Open device for live capture
    pcap_t* handle = pcap_open_live(device->name, BUFSIZ, packet_count_limit, timeout_limit, errbuf);

    if (handle == NULL) {
        fprintf(stderr, "Could not open device %s: %s\n", app->device, errbuf);
        return 2;
    }

    // Set application in a capture state
    app->state->capture = 1;

    // Process data in loop (while application is in capture state or there is an error)
    kmp_pcap_loop(handle, callback, app);

    // Remove application from capture state (eg. after error)
    app->state->capture = 0;

    // Free devices list
    pcap_freealldevs(alldevsp);

    // Free live capture handle
    pcap_close(handle);

    return 0;
}

void kmp_search_impl(const char* sz_text, const int len, const app_t* app)
{
    for (int i = 0; i < app->words; ++i)
    {
        const kmp_search_t kmp_word = app->kmp_words[i];
        int position = kmp_search(sz_text, len, &kmp_word);

        if (position != -1 && !app->state->matched_words[i])
        {
            // update word status
            app->state->matched_words[i] = 1;
            app->state->matches++;

            // notify match
            notify_match(sz_text, len, &kmp_word, app);
        }
    }
}

void kmp_search_impl_openmp(const char* sz_text, const int len, const app_t* app)
{
    int thread_count = app->threads;
    int* result = (int*)malloc(sizeof(int) * thread_count * app->words);
    int chunk_size = (int)ceil((double)len / thread_count);

    // set each word as not found
    for (int proc_rank = 0; proc_rank < thread_count; ++proc_rank)
        for (int idx = 0; idx < app->words; ++idx)
            result[proc_rank * app->words + idx] = -1;

    #pragma omp parallel num_threads(thread_count) default(none) shared(sz_text, len, app, thread_count, result, chunk_size)
    {
        int proc_rank = omp_get_thread_num();
        int* positions = result + proc_rank * app->words;

        // calculate the start position of the chunk to process
        int start_pos = chunk_size * proc_rank;

        // the effective amount of data to process (the latest thread may receive smaller chunk of data)
        int data_count = start_pos + chunk_size > len ? len - start_pos : chunk_size;

        for (int idx = 0; idx < app->words; ++idx)
        {
            // word cannot matches
            if (app->kmp_words[idx].length > data_count || app->state->matched_words[idx])
                continue;

            int position = kmp_search(sz_text + start_pos, data_count, &app->kmp_words[idx]);

            // update position (the received position is relative to the chunk processed from thread)
            if (position != -1)
            {
                position = position + start_pos;
                positions[idx] = position;
                continue; // first occurrence is enough
            }
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

        for (int proc_rank = 0; proc_rank < thread_count - 1; ++proc_rank)
        {
            // calculate the start and the end position of the (missed) chunk
            int start_pos = chunk_size * proc_rank + chunk_size - kmp_word.length + 1; // one character (the latest one) is part of the next chunk

            // since the word is bigger than chunk size
            // the word spans across multiple chunks
            if (kmp_word.length > chunk_size)
                start_pos = chunk_size * proc_rank;

            int end_pos = chunk_size * proc_rank + chunk_size + kmp_word.length - 1; // one character (the first one) is part of current chunk
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

    for (int proc_rank = 0; proc_rank < thread_count; ++proc_rank)
    {
        int* positions = result + proc_rank * app->words;

        for (int idx = 0; idx < app->words; ++idx)
        {
            int position = positions[idx];
            kmp_search_t kmp_word = app->kmp_words[idx];

            if (position != -1 && !app->state->matched_words[idx])
            {
                // update word status
                app->state->matched_words[idx] = 1;
                app->state->matches++;

                // notify match
                notify_match(sz_text, len, &kmp_word, app);
            }
        }
    }

    free(result);
}

double get_wall_time()
{
    struct timeval time;
    if (gettimeofday(&time,NULL))
        return 0;

    return (double)time.tv_sec + (double)time.tv_usec * .000001;
}

void capture_network(const app_t* app)
{
    if (app->version == KMP_MPI)
        live_capture_mpi(app);
    else
    {
        kmp_handler handler = NULL;

        // use a different handler based on the variant of execution
        if (app->version == KMP_SINGLE_THREAD)
            handler = kmp_search_impl;
        else if (app->version == KMP_OPENMP)
            handler = kmp_search_impl_openmp;

        live_capture(handler, app);
    }
}

void test_file(const char* sz_filename, const app_t* app)
{
    int rank; // Current process rank
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    FILE* file;
    int cnd = 1;
    double time = 0;
    char* buffer = NULL;
    int buffer_size = MAX_BUFFER_SIZE;

    if (rank == 0)
    {
        file = fopen(sz_filename, "r");
        buffer = (char*)malloc(sizeof(char) * buffer_size);

        if (file == NULL)
        {
            printf("Could not open file %s.\n", sz_filename);
            exit(EXIT_FAILURE);
        }
    }

    while (cnd)
    {
        int len = 0;

        if (rank == 0)
        {
            cnd = fgets(buffer, buffer_size, file) != NULL;
            len = strlen(buffer);
        }

        MPI_Bcast(&cnd, 1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Barrier(MPI_COMM_WORLD);

        if (cnd != 0)
        {
            double start_time = get_wall_time();

            // test provided text
            if (app->version == KMP_MPI)
                kmp_search_mpi(buffer, len, app);
            else if (app->version == KMP_SINGLE_THREAD)
                kmp_search_impl(buffer, len, app);
            else if (app->version == KMP_OPENMP)
                kmp_search_impl_openmp(buffer, len, app);

            MPI_Barrier(MPI_COMM_WORLD);
            time += get_wall_time() - start_time;

            // matched all the strings.
            if (app->state->matches == app->words)
                break;
        }
    }

    if (rank == 0)
    {
        free(buffer);
        fclose(file);
    }

    // check if any word matched
    if (!app->state->matches && rank == 0)
        printf("No matches...\n");

    if (rank == 0) printf("Wall time: %f\n", time);
}

void test_pattern(const char* sz_text, const app_t* app)
{
    int rank; // Current process rank
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // test provided text
    if (app->version == KMP_MPI)
        kmp_search_mpi(sz_text, strlen(sz_text), app);
    else if (app->version == KMP_SINGLE_THREAD)
        kmp_search_impl(sz_text, strlen(sz_text), app);
    else if (app->version == KMP_OPENMP)
        kmp_search_impl_openmp(sz_text, strlen(sz_text), app);

    // check if any word matched
    if (!app->state->matches && rank == 0)
        printf("No matches...\n");
}