#ifndef APP_HEADER
#define APP_HEADER

#include "common.h"

/**
 * List of arguments
 */
typedef struct arguments_t
{
    int list_devices; // Print devices list
    char* device; // Device name
    int threads; // No. of threads (OpenMP only)
    int words; // No. of words
    int version; // Type of execution (single-thread, OpenMP, MPI)
    char* sz_text; // Text to search in
    char** sz_words; // Vector of strings
    char* sz_filename; // File to search in
} arguments_t;

/**
 * Parse input into arguments struct.
 * @param argc (input) Arguments count
 * @param argv (input) Arguments pointer
 * @param arguments (output) Arguments pointer
 */
void parse_input(int argc, char** argv, arguments_t* arguments);

/**
 * Initialize app
 * @param app
 * @param arguments
 */
void init_app(app_t* app, const arguments_t* arguments);

/**
 * Destroy app
 * @param app
 */
void destroy_app(app_t* app);

/**
 * A function handler (used as callback for live capture)
 */
typedef void (*kmp_handler)(const char* sz_text, int len, const app_t* app);

/**
 * Capture TCP/UDP network traffic and call callback with the captured payload.
 * @param callback Callback function
 * @param app App
 * @return -1 on failure
 */
int live_capture(kmp_handler callback, const app_t* app);

/**
 * Capture TCP/UDP network traffic and call the opportune callback with the captured payload.
 * Callback function is decided at runtime.
 * @param app
 */
void capture_network(const app_t* app);

/**
 * Callback function when a pattern is matched in a string.
 * @param sz_text Text containing the pattern
 * @param len Text length
 * @param kmp_word Pattern matched
 * @param app App
 */
void notify_match(const char* sz_text, const int len, const kmp_search_t* kmp_word, const app_t* app);

/**
 * Apply KMP search on the provided text (single-thread).
 * @param sz_text Text to search in
 * @param len Text length
 * @param app App
 */
void kmp_search_impl(const char* sz_text, const int len, const app_t* app);

/**
 * Apply KMP search on the provided text (OpenMP).
 * @param sz_text Text to search in
 * @param len Text length
 * @param app App
 */
void kmp_search_impl_openmp(const char* sz_text, const int len, const app_t* app);

/**
 * Print available devices.
 */
void print_available_devices();

/**
 * Apply KMP search on the file contents.
 * @param sz_filename Name of the file to search in
 * @param app App
 */
void test_file(const char* sz_filename, const app_t* app);

/**
 * Apply KMP search on the provided text.
 * @param sz_text Text to search in
 * @param app App
 */
void test_pattern(const char* sz_text, const app_t* app);

/**
 * Get wall time
 * @return
 */
double get_wall_time();

#endif