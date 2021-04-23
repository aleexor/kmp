#ifndef KMP_HEADER
#define KMP_HEADER

typedef struct kmp_search_t
{
    int length;
    int* kmp_table;
    const char* sz_word;
} kmp_search_t;

/**
 * Searches for occurrences of kmp pattern within sz_text
 * @param sz_text Text to search in
 * @param len Text length
 * @param kmp Pattern
 * @return First occurrence position (or -1)
 */
int kmp_search(const char* sz_text, const int len, const kmp_search_t* kmp);

/**
 * Initialize the KMP search
 * @param sz_word Pattern
 * @param kmp Pointer to struct
 * @return -1 on failure
 */
int kmp_search_init(const char* sz_word, kmp_search_t* kmp);

/**
 * Destroy KMP search.
 * @param kmp Pattern
 * @return -1 on failure
 */
int kmp_search_destroy(kmp_search_t* kmp);

#endif