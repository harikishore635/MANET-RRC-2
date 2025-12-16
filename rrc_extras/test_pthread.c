#include <stdio.h>
#define HAVE_STRUCT_TIMESPEC
#include <pthread.h>

void* test_thread(void* arg) {
    printf("Thread running\n");
    return NULL;
}

int main() {
    pthread_t thread;
    pthread_create(&thread, NULL, test_thread, NULL);
    pthread_join(thread, NULL);
    printf("Success!\n");
    return 0;
}
