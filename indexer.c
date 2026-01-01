/*
 * file_indexer.c
 * High-Performance Recursive File Indexer, Searcher, and Launcher.
 *
 * COMPILATION INSTRUCTIONS:
 *
 * Windows (MinGW/GCC):
 * gcc file_indexer.c -o file_indexer.exe
 *
 * Linux / macOS:
 * gcc file_indexer.c -o file_indexer
 *
 * USAGE:
 * ./file_indexer [optional_root_path]
 * (If no path is provided, it defaults to the current directory)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

// --- OS Specific Includes & Macros ---
#if defined(_WIN32) || defined(_WIN64)
    #define OS_WINDOWS
    #include <windows.h>
    #include <shellapi.h>
    #define PATH_SEP '\\'
    #define PATH_SEP_STR "\\"
#else
    #define OS_POSIX
    #include <dirent.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #define PATH_SEP '/'
    #define PATH_SEP_STR "/"
#endif

// --- Configuration ---
#define HASH_TABLE_SIZE 16384 // Power of 2 for fast distribution
#define MAX_PATH_LEN 1024
#define MAX_RESULTS 100

// --- Data Structures ---

// Linked list node for Hash Table collisions
typedef struct FileEntry {
    char *filename;         // Just the file name (e.g., "report.pdf")
    char *fullpath;         // Full absolute path
    struct FileEntry *next; // Pointer to next entry in bucket
} FileEntry;

// Global Hash Table
FileEntry *hashTable[HASH_TABLE_SIZE];
long totalFiles = 0;

// --- Helper Functions ---

// Custom Case-Insensitive Substring Search (strcasestr replacement)
char *stristr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
            const char *h = haystack, *n = needle;
            while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
                h++;
                n++;
            }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}

// DJB2 Hash Function
unsigned long hash(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + tolower(c); // hash * 33 + c (case insensitive hashing)
    return hash % HASH_TABLE_SIZE;
}

// Add file to Hash Table
void addFile(const char *name, const char *path) {
    unsigned long index = hash(name);
    
    FileEntry *newEntry = (FileEntry *)malloc(sizeof(FileEntry));
    if (!newEntry) return;

    newEntry->filename = strdup(name);
    newEntry->fullpath = strdup(path);
    newEntry->next = hashTable[index]; // Insert at head
    hashTable[index] = newEntry;
    
    totalFiles++;
}

// Free memory (for rebuilds or cleanup)
void clearIndex() {
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        FileEntry *entry = hashTable[i];
        while (entry) {
            FileEntry *temp = entry;
            entry = entry->next;
            free(temp->filename);
            free(temp->fullpath);
            free(temp);
        }
        hashTable[i] = NULL;
    }
    totalFiles = 0;
}

// --- File System Traversal ---

#ifdef OS_WINDOWS
void traverseDirectory(const char *basePath) {
    char searchPath[MAX_PATH_LEN];
    snprintf(searchPath, sizeof(searchPath), "%s\\*", basePath);

    WIN32_FIND_DATA findData;
    HANDLE hFind = FindFirstFile(searchPath, &findData);

    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        // Skip . and ..
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0)
            continue;

        char fullPath[MAX_PATH_LEN];
        snprintf(fullPath, sizeof(fullPath), "%s\\%s", basePath, findData.cFileName);

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Recurse into subdirectory
            traverseDirectory(fullPath);
        } else {
            // Add file to index
            addFile(findData.cFileName, fullPath);
        }
    } while (FindNextFile(hFind, &findData) != 0);

    FindClose(hFind);
}
#endif

#ifdef OS_POSIX
void traverseDirectory(const char *basePath) {
    DIR *dir = opendir(basePath);
    if (!dir) return; // Permission denied or invalid path

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char fullPath[MAX_PATH_LEN];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", basePath, entry->d_name);

        struct stat statbuf;
        if (stat(fullPath, &statbuf) == -1) continue;

        if (S_ISDIR(statbuf.st_mode)) {
            // Recurse
            traverseDirectory(fullPath);
        } else {
            // Add to index
            addFile(entry->d_name, fullPath);
        }
    }
    closedir(dir);
}
#endif

// --- Core Functionality ---

void buildIndex(const char *root) {
    printf("Indexing files in '%s'...\n", root);
    clock_t start = clock();
    
    traverseDirectory(root);
    
    clock_t end = clock();
    double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
    printf("Index complete. Found %ld files in %.2f seconds.\n", totalFiles, time_spent);
}

// Open file using default OS application
void openFile(const char *path) {
    printf("Opening: %s\n", path);
    
    #ifdef OS_WINDOWS
        // Windows ShellExecute
        ShellExecute(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL);
    #elif defined(__APPLE__)
        // macOS open command
        char command[MAX_PATH_LEN + 10];
        snprintf(command, sizeof(command), "open \"%s\"", path);
        system(command);
    #else
        // Linux xdg-open
        char command[MAX_PATH_LEN + 15];
        snprintf(command, sizeof(command), "xdg-open \"%s\"", path);
        system(command);
    #endif
}

// Search Logic
void searchAndInteract() {
    char query[256];
    
    while (1) {
        printf("\n------------------------------------------------\n");
        printf("Enter filename to search (or 'exit' to quit, 'rebuild' to refresh):\n> ");
        if (!fgets(query, sizeof(query), stdin)) break;

        // Strip newline
        query[strcspn(query, "\n")] = 0;

        if (strlen(query) == 0) continue;
        if (strcmp(query, "exit") == 0) break;
        if (strcmp(query, "rebuild") == 0) {
            // In a real app, we'd store the root path globally to rebuild easily
            printf("To rebuild, please restart the application with the desired path.\n");
            continue;
        }

        // --- Search Phase ---
        FileEntry *matches[MAX_RESULTS];
        int matchCount = 0;

        // Iterate through Hash Table buckets
        // Note: For partial matching, we must scan the table.
        // Hash tables are O(1) for exact match, but O(N) for substrings.
        // However, in-memory scanning of struct pointers is extremely fast.
        for (int i = 0; i < HASH_TABLE_SIZE; i++) {
            FileEntry *entry = hashTable[i];
            while (entry) {
                if (stristr(entry->filename, query)) {
                    matches[matchCount++] = entry;
                    if (matchCount >= MAX_RESULTS) goto search_done;
                }
                entry = entry->next;
            }
        }

        search_done:
        if (matchCount == 0) {
            printf("No matches found for '%s'.\n", query);
        } else {
            printf("\nFound %d matches (showing top %d):\n", matchCount, MAX_RESULTS);
            for (int i = 0; i < matchCount; i++) {
                printf("[%d] %s\n    Path: %s\n", i + 1, matches[i]->filename, matches[i]->fullpath);
            }

            printf("\nEnter number to open (or 0 to cancel): ");
            int choice;
            char inputBuffer[10];
            if (fgets(inputBuffer, sizeof(inputBuffer), stdin)) {
                choice = atoi(inputBuffer);
                if (choice > 0 && choice <= matchCount) {
                    openFile(matches[choice - 1]->fullpath);
                } else if (choice != 0) {
                    printf("Invalid selection.\n");
                }
            }
        }
    }
}

// --- Main Entry ---

int main(int argc, char *argv[]) {
    char rootPath[MAX_PATH_LEN];

    // Determine root directory
    if (argc > 1) {
        strncpy(rootPath, argv[1], MAX_PATH_LEN - 1);
    } else {
        // Use current working directory
        #ifdef OS_WINDOWS
            GetCurrentDirectory(MAX_PATH_LEN, rootPath);
        #else
            if (getcwd(rootPath, MAX_PATH_LEN) == NULL) {
                perror("getcwd() error");
                return 1;
            }
        #endif
    }
    
    // Normalize path separators if needed (simple check)
    // (In robust code, we would canonicalize the path here)

    // Initialize Hash Table
    memset(hashTable, 0, sizeof(hashTable));

    // 1. Build Index
    buildIndex(rootPath);

    // 2. Interactive Loop
    searchAndInteract();

    // 3. Cleanup
    clearIndex();
    
    return 0;
}