/*

 * The Ultimate CLI File Searcher.
 * "Simplicity is the ultimate sophistication."
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

// --- Platform Specifics ---
#if defined(_WIN32) || defined(_WIN64)
    #define OS_WINDOWS
    #include <windows.h>
    #include <conio.h>
    #include <shellapi.h>
    #define PATH_SEP '\\'
#else
    #define OS_POSIX
    #include <dirent.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #include <termios.h>
    #define PATH_SEP '/'
    #ifndef STDIN_FILENO
        #define STDIN_FILENO 0
    #endif
#endif

// --- Configuration ---
#define HASH_TABLE_SIZE 16384
#define MAX_PATH_LEN 1024
#define VIEWPORT_HEIGHT 12
#define MAX_RESULTS 50

// --- ANSI Colors ---
#define COLOR_RESET "\033[0m"
#define COLOR_BOLD "\033[1m"
#define COLOR_DIM "\033[2m"
#define COLOR_CYAN "\033[36m"
#define COLOR_WHITE "\033[37m"
#define COLOR_YELLOW "\033[33m"

// --- Data Structures ---
typedef struct FileEntry {
    char *filename;
    char *fullpath;
    struct FileEntry *next;
} FileEntry;

FileEntry *hashTable[HASH_TABLE_SIZE];
long totalFiles = 0;

// --- System Utilities ---

void enable_ansi() {
    #ifdef OS_WINDOWS
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
    #endif
}

// Case-insensitive substring search
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

unsigned long hash(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + tolower(c);
    return hash % HASH_TABLE_SIZE;
}

// --- Indexing Engine ---

void addFile(const char *name, const char *path) {
    unsigned long index = hash(name);
    FileEntry *newEntry = (FileEntry *)malloc(sizeof(FileEntry));
    if (!newEntry) return;

    newEntry->filename = strdup(name);
    newEntry->fullpath = strdup(path);
    newEntry->next = hashTable[index];
    hashTable[index] = newEntry;
    totalFiles++;
}

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

#ifdef OS_WINDOWS
void traverseDirectory(const char *basePath) {
    char searchPath[MAX_PATH_LEN];
    snprintf(searchPath, sizeof(searchPath), "%s\\*", basePath);
    WIN32_FIND_DATA findData;
    HANDLE hFind = FindFirstFile(searchPath, &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) continue;
        char fullPath[MAX_PATH_LEN];
        snprintf(fullPath, sizeof(fullPath), "%s\\%s", basePath, findData.cFileName);
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            traverseDirectory(fullPath);
        } else {
            addFile(findData.cFileName, fullPath);
        }
    } while (FindNextFile(hFind, &findData) != 0);
    FindClose(hFind);
}
#endif

#ifdef OS_POSIX
void traverseDirectory(const char *basePath) {
    DIR *dir = opendir(basePath);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char fullPath[MAX_PATH_LEN];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", basePath, entry->d_name);
        struct stat statbuf;
        if (stat(fullPath, &statbuf) == -1) continue;
        if (S_ISDIR(statbuf.st_mode)) {
            traverseDirectory(fullPath);
        } else {
            addFile(entry->d_name, fullPath);
        }
    }
    closedir(dir);
}
#endif

void buildIndex(const char *root) {
    printf(COLOR_CYAN "  Index > " COLOR_RESET "Scanning %s ...\n", root);
    traverseDirectory(root);
}

// --- Interaction Logic ---

void openFile(const char *path) {
    #ifdef OS_WINDOWS
        ShellExecute(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL);
    #elif defined(__APPLE__)
        char command[MAX_PATH_LEN + 10];
        snprintf(command, sizeof(command), "open \"%s\"", path);
        system(command);
    #else
        char command[MAX_PATH_LEN + 15];
        snprintf(command, sizeof(command), "xdg-open \"%s\"", path);
        system(command);
    #endif
}

int get_char_raw() {
    #ifdef OS_WINDOWS
        return _getch();
    #else
        struct termios oldt, newt;
        int ch;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        ch = getchar();
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        return ch;
    #endif
}


void shorten_path(const char *in, char *out, int max_len) {
    int len = strlen(in);
    if (len < max_len) {
        strcpy(out, in);
    } else {
        snprintf(out, max_len, "...%s", in + (len - (max_len - 4)));
    }
}

void render_ui(const char *query, FileEntry **matches, int count, double searchTime) {
    // Save Cursor
    printf("\0337"); 

    // Render matches in viewport
    for (int i = 0; i < VIEWPORT_HEIGHT; i++) {
        printf("\033[B");   // Move down
        printf("\033[2K");  // Clear line
        printf("\r");       // Return to start

        if (i < count && i < VIEWPORT_HEIGHT) {
            char shortPath[60];
            shorten_path(matches[i]->fullpath, shortPath, 55);

            // [ID] Filename (Bold) ... Path (Dimmed)
            printf("  " COLOR_CYAN "[%2d]" COLOR_RESET "  " COLOR_BOLD "%-35s" COLOR_RESET "  " COLOR_DIM "%s" COLOR_RESET, 
                   i + 1, 
                   // Truncate filename visual if too long
                   (strlen(matches[i]->filename) > 35) ? "..." : matches[i]->filename,
                   shortPath);
        } else if (i == 0 && strlen(query) > 0 && count == 0) {
            printf(COLOR_YELLOW "       No matches found." COLOR_RESET);
        }
    }

    // Status Bar (below viewport)
    printf("\033[B\033[2K\r");
    printf(COLOR_DIM "  ______________________________________________________" COLOR_RESET);
    printf("\033[B\033[2K\r");
    if (strlen(query) > 0)
        printf(COLOR_DIM "  Found %d matches in %.4fs" COLOR_RESET, count, searchTime);
    else 
        printf(COLOR_DIM "  %ld files indexed. Ready." COLOR_RESET, totalFiles);

    // Restore Cursor to search bar
    printf("\0338"); 
}

void app_loop() {
    char query[256] = {0};
    int pos = 0;
    int ch;
    
    #ifdef OS_WINDOWS
        system("cls");
    #else
        system("clear");
    #endif

    printf("\n" COLOR_BOLD COLOR_WHITE "  SPOTLIGHT SEARCH" COLOR_RESET "\n");
    printf(COLOR_DIM "  Type to search. Enter to open. ESC to quit." COLOR_RESET "\n\n");
    
    // Prepare blank lines for the UI to sit in
    for(int i = 0; i < VIEWPORT_HEIGHT + 4; i++) printf("\n");
    // Move cursor back up to input position
    printf("\033[%dA", VIEWPORT_HEIGHT + 4);

    while (1) {
        // Render Search Bar
        printf("\r\033[2K  " COLOR_CYAN "> " COLOR_RESET COLOR_BOLD "%s" COLOR_RESET, query);
        fflush(stdout);

        // Search Logic
        FileEntry *matches[MAX_RESULTS];
        int count = 0;
        clock_t start = clock();

        if (strlen(query) > 0) {
            for (int i = 0; i < HASH_TABLE_SIZE; i++) {
                FileEntry *entry = hashTable[i];
                while (entry) {
                    if (stristr(entry->filename, query)) {
                        matches[count++] = entry;
                        if (count >= VIEWPORT_HEIGHT) goto search_finished;
                    }
                    entry = entry->next;
                }
            }
        }
        search_finished:;
        
        double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;

        // Render Viewport
        render_ui(query, matches, count, elapsed);
        fflush(stdout);

        // Input
        ch = get_char_raw();

        // Handle Escape
        if (ch == 27) break;

        // Handle Enter
        else if (ch == '\r' || ch == '\n') {
            if (count > 0) {
                // Freeze UI and ask for selection
                printf("\033[%dB", VIEWPORT_HEIGHT + 3); // Move to bottom
                printf("\n  " COLOR_CYAN "Open file ID (1-%d): " COLOR_RESET, count);
                
                // Switch to buffered input
                #ifdef OS_POSIX
                    struct termios oldt, newt;
                    tcgetattr(STDIN_FILENO, &oldt);
                    newt = oldt;
                    newt.c_lflag |= (ICANON | ECHO);
                    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
                #endif

                char numBuf[16];
                if (fgets(numBuf, sizeof(numBuf), stdin)) {
                    int choice = atoi(numBuf);
                    if (choice > 0 && choice <= count) {
                        openFile(matches[choice-1]->fullpath);
                    }
                }

                #ifdef OS_POSIX
                    tcgetattr(STDIN_FILENO, &oldt);
                    newt = oldt;
                    newt.c_lflag &= ~(ICANON | ECHO);
                    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
                #endif

                // Reset UI
                memset(query, 0, sizeof(query));
                pos = 0;
                
                // Clear the input line we just made
                printf("\033[A\033[2K");
                // Move back to search bar
                printf("\033[%dA", VIEWPORT_HEIGHT + 3);
            }
        }
        // Handle Backspace
        else if (ch == 127 || ch == 8) {
            if (pos > 0) {
                query[--pos] = '\0';
            }
        }
        // Handle Typing
        else if (isprint(ch) && pos < sizeof(query) - 1) {
            query[pos++] = (char)ch;
            query[pos] = '\0';
        }
    }
}

// --- Main ---

int main(int argc, char *argv[]) {
    enable_ansi();
    char rootPath[MAX_PATH_LEN];

    if (argc > 1) {
        strncpy(rootPath, argv[1], MAX_PATH_LEN - 1);
    } else {
        #ifdef OS_WINDOWS
            GetCurrentDirectory(MAX_PATH_LEN, rootPath);
        #else
            if (getcwd(rootPath, MAX_PATH_LEN) == NULL) return 1;
        #endif
    }

    memset(hashTable, 0, sizeof(hashTable));
    buildIndex(rootPath);
    app_loop();
    clearIndex();
    
    // Clear screen on exit 
    #ifdef OS_WINDOWS
        system("cls");
    #else
        system("clear");
    #endif
    return 0;
}

