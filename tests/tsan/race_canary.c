#include <pthread.h>

static int shared;

static void *bump(void *arg)
{
    (void)arg;
    shared++;
    return NULL;
}

int main(void)
{
    pthread_t a, b;

    pthread_create(&a, NULL, bump, NULL);
    pthread_create(&b, NULL, bump, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    return 0;
}
