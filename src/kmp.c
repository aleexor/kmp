#include <string.h>
#include <stdlib.h>
#include "kmp.h"

// https://en.wikipedia.org/wiki/Knuth%E2%80%93Morris%E2%80%93Pratt_algorithm
void build_kmp_table(kmp_search_t* kmp)
{
    int pos = 1; // the current position we are computing in kmp_table
    int cnd = 0; // the zero-based index in W of next character of the current candidate substring
    kmp->kmp_table[0] = -1;

    while (pos < kmp->length)
    {
        if (kmp->sz_word[pos] == kmp->sz_word[cnd]) // if the same character is present several times
            kmp->kmp_table[pos] = kmp->kmp_table[cnd];
        else
        {
            kmp->kmp_table[pos] = cnd;

            while (cnd >= 0 && kmp->sz_word[pos] != kmp->sz_word[cnd])
                cnd = kmp->kmp_table[cnd];
        }

        pos++;
        cnd++;
    }

    kmp->kmp_table[pos] = cnd;
}

int kmp_search(const char* sz_text, const int len, const kmp_search_t* kmp)
{
    if (kmp == NULL)
        return -1;

    int j = 0; // the position of the current character in sz_text
    int k = 0; // the position of the current character in sz_word

    while (j < len)
    {
        if (kmp->sz_word[k] == sz_text[j])
        {
            j++;
            k++;

            if (k == kmp->length) // occurrence found
                return j - k;
        }
        else
        {
            k = kmp->kmp_table[k];

            if (k < 0)
            {
                j++;
                k++;
            }
        }
    }

    return -1; // occurrence not found
}

int kmp_search_init(const char* sz_word, kmp_search_t* kmp)
{
    if (kmp == NULL || sz_word == NULL)
        return -1; // failure (cannot be initialized)

    // allocate required memory
    int length = strlen(sz_word);
    int kmp_table_size = length + 1;
    int* kmp_table = (int*)malloc(sizeof(int) * kmp_table_size);

    kmp->kmp_table = kmp_table;
    kmp->sz_word = sz_word;
    kmp->length = length;

    // build failure table
    build_kmp_table(kmp);
    return 0;
}

int kmp_search_destroy(kmp_search_t* kmp)
{
    if (kmp == NULL)
        return -1; // cannot be destroyed

    // clear memory and clear data
    free(kmp->kmp_table);
    kmp->sz_word = NULL;
    kmp->length = 0;

    return 0;
}