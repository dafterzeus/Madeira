/* Run the pinned rpmalloc tests relevant to the shipped ENABLE_OVERRIDE=0
 * build. Each test runs in a separate process. main.c is otherwise unchanged.
 * Standard malloc overrides and OS huge-page privileges are not used by FEX. */
#include <rpmalloc.c>
#define NO_MAIN 1
#include <main.c>

volatile int FEX_AllocWatch_Armed;
void FEX_AllocWatch_Event(const void* p, unsigned int event) { (void)p; (void)event; }
/* Uncalled override tests referenced by the original test_run entry point. */
int test_malloc(int print_log) { (void)print_log; abort(); }
int test_free(int print_log) { (void)print_log; abort(); }
int test_malloc_thread(void) { abort(); }

int main(int argc, char** argv) {
    ios_fex_band_base = 0x7d00000000ULL;
    ios_fex_band_end = 0x7fffffffffULL;
    test_initialize();
    hardware_threads = 2;
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "alloc")) return test_alloc();
    if (!strcmp(argv[1], "realloc")) return test_realloc();
    if (!strcmp(argv[1], "aligned")) return test_superalign();
    if (!strcmp(argv[1], "cross-thread")) return test_crossthread();
    if (!strcmp(argv[1], "threaded")) return test_threaded();
    if (!strcmp(argv[1], "thread-spam")) return test_threadspam();
    if (!strcmp(argv[1], "heaps")) return test_first_class_heaps();
    return 2;
}
