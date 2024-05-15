
#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define MAXT_IN_POOL 200 // maximum number of threads allowed in a pool


threadpool* create_threadpool(int num_threads_in_pool) {
    if (num_threads_in_pool < 1 || num_threads_in_pool >MAXT_IN_POOL) {
        fprintf(stderr, "Usage: <pool-size> <number-of-tasks> <max-number-of-request>\n");
        //exit(EXIT_FAILURE); // Incorrect command usage
        return NULL;
    }

    threadpool* pool = (threadpool*)malloc(sizeof(threadpool));
    if (pool == NULL) {
        perror("error: malloc");
        // exit(EXIT_FAILURE); // Memory allocation failed
        return NULL;
    }

    pool->num_threads = num_threads_in_pool;
    pool->qsize = 0;
    pool->qhead = pool->qtail = NULL;
    pool->shutdown = 0;
    pool->dont_accept = 0;

    if (pthread_mutex_init(&(pool->qlock), NULL) != 0) {
        perror("error: mutex init");
        free(pool);
        //  exit(EXIT_FAILURE);
        return NULL;
    }
    if (pthread_cond_init(&(pool->q_not_empty), NULL) != 0) {
        perror("error: condition variable init");
        pthread_mutex_destroy(&(pool->qlock));
        free(pool);
        // exit(EXIT_FAILURE);
        return NULL;
    }
    if (pthread_cond_init(&(pool->q_empty), NULL) != 0) {
        perror("error: condition variable init");
        pthread_mutex_destroy(&(pool->qlock));
        pthread_cond_destroy(&(pool->q_not_empty));
        free(pool);
        // exit(EXIT_FAILURE);
        return NULL;
    }

    pool->threads = (pthread_t*)malloc(num_threads_in_pool * sizeof(pthread_t));
    if (pool->threads == NULL) {
        perror("error: malloc");
        pthread_mutex_destroy(&(pool->qlock));
        pthread_cond_destroy(&(pool->q_not_empty));
        pthread_cond_destroy(&(pool->q_empty));
        free(pool);
        //exit(EXIT_FAILURE); // Memory allocation failed
        return NULL;
    }

    for (int i = 0; i < num_threads_in_pool; i++) {
        if (pthread_create(&(pool->threads[i]), NULL, do_work, (void*)pool) != 0) {
            perror("error: thread creation");
            pthread_mutex_destroy(&(pool->qlock));
            pthread_cond_destroy(&(pool->q_not_empty));
            pthread_cond_destroy(&(pool->q_empty));
            free(pool->threads);
            free(pool);
            //  exit(EXIT_FAILURE);
            return NULL;
        }
    }

    return pool;
}

void dispatch(threadpool* from_me, dispatch_fn dispatch_to_here, void *arg) {

    if (from_me == NULL || dispatch_to_here == NULL) {
        return; // Input sanity check
    }

    work_t* work = (work_t*)malloc(sizeof(work_t));
    if (work == NULL) {
        perror("error: malloc");
        return;
    }

    work->routine = dispatch_to_here;
    work->arg = arg;
    work->next = NULL;

    pthread_mutex_lock(&(from_me->qlock));

    // If destruction function has begun, don't accept new items to the queue
    if (from_me->dont_accept) {
        pthread_mutex_unlock(&(from_me->qlock));
        free(work);
        return;
    }

    // Add item to the queue
    if (from_me->qhead == NULL) {
        from_me->qhead = from_me->qtail = work;
    } else {
        from_me->qtail->next = work;
        from_me->qtail = work;
    }
    from_me->qsize++;

    // Signal that queue is not empty
    pthread_cond_signal(&(from_me->q_not_empty));

    pthread_mutex_unlock(&(from_me->qlock));
}
// Thread-local variable to store thread ID
static pthread_key_t thread_id_key;

void* do_work(void* p) {

    // Set thread ID for TLS
    pthread_setspecific(thread_id_key, (void*)pthread_self());

    threadpool* tp = (threadpool*)p;

    while(1) {
        pthread_mutex_lock(&(tp->qlock));

        // If destruction process has begun, exit thread
        if (tp->shutdown) {
            pthread_mutex_unlock(&(tp->qlock));
            pthread_exit(NULL);
        }

        // If the queue is empty, wait
        if (tp->qsize == 0) {
            pthread_cond_wait(&(tp->q_not_empty), &(tp->qlock));
        }
        // Check again destruction flag after waking up
        if (tp->shutdown) {
            pthread_mutex_unlock(&(tp->qlock));
            pthread_exit(NULL);

        }


        // Take the first element from the queue
        work_t* work = tp->qhead;
        tp->qhead = tp->qhead->next;
        tp->qsize--;

        // If the queue becomes empty and destruction process wait to begin, signal destruction process
        if (tp->qsize == 0 && tp->dont_accept) {
            pthread_cond_signal(&(tp->q_empty));
        }

        pthread_mutex_unlock(&(tp->qlock));

        // Call the thread routine
        (*(work->routine))(work->arg);
        free(work);
    }
}

void destroy_threadpool(threadpool* destroyme) {
    if (destroyme == NULL) {
        return; // Input sanity check
    }

    pthread_mutex_lock(&(destroyme->qlock));

    // Set don't_accept flag to 1
    destroyme->dont_accept = 1;

    // Wait for queue to become empty
    while (destroyme->qsize > 0) {
        pthread_cond_wait(&(destroyme->q_empty), &(destroyme->qlock));
    }

    // Set shutdown flag to 1
    destroyme->shutdown = 1;

    // Signal threads that wait on empty queue
    pthread_cond_broadcast(&(destroyme->q_not_empty));

    pthread_mutex_unlock(&(destroyme->qlock));

    // Join all threads
    for (int i = 0; i < destroyme->num_threads; i++) {
        pthread_join(destroyme->threads[i], NULL);

    }
   // printf("all threads finish\n");
    pthread_mutex_destroy(&(destroyme->qlock));
    pthread_cond_destroy(&(destroyme->q_not_empty));
    pthread_cond_destroy(&(destroyme->q_empty));
    // Free memory associated with the thread pool
    free(destroyme->threads);
    free(destroyme);
}

//int task_function(void* arg) {
//    int thread_id = *((int*)arg);
//    for (int i = 0; i < 1000; i++) {
//        printf("Thread ID: %d, Iteration: %d\n", thread_id, i);
//        usleep(100000); // Sleep for 100 milliseconds
//    }
//    return 0;
//}
int task_function(void* arg) {
    // Cast pthread_t to unsigned long before printing
    unsigned long thread_id = (unsigned long)pthread_self();
    //  int thread_id= *((int*)arg);
    for (int i = 0; i < 1000; i++) {
        printf("Thread ID: %lu, Iteration: %d\n", thread_id, i);
        usleep(100000); // Sleep for 100 milliseconds
    }
    return 0;
}

//int main(int argc, char *argv[]) {
//    if (argc != 4) {
//        fprintf(stderr, "Usage: pool <pool-size> <number-of-tasks> <max-number-of-request>\n");
//        return EXIT_FAILURE;
//    }
//    int pool_size = atoi(argv[1]);
//    int num_tasks = atoi(argv[2]);
//    int max_requests = atoi(argv[3]);// when to use?
//
//    // Create thread-local key for thread ID
//    pthread_key_create(&thread_id_key, NULL);
//
//    threadpool* pool = create_threadpool(pool_size);
//    if (pool == NULL) {
//        fprintf(stderr, "Failed to create thread pool\n");
//        return EXIT_FAILURE;
//    }
//
//    // Create an array to hold task arguments
//    int* task_args = (int*)malloc(num_tasks * sizeof(int));
//    if (task_args == NULL) {
//        fprintf(stderr, "Memory allocation failed\n");
//        destroy_threadpool(pool);
//        return EXIT_FAILURE;
//    }
//
//    // Dispatch tasks using multiple threads
//    int tasks_dispatched = 0;
//    while (tasks_dispatched < num_tasks && tasks_dispatched < max_requests) {
//        for (int i = 0; i < num_tasks && tasks_dispatched < max_requests; i++) {
//            task_args[i] = i; // Assign a unique ID to each task
//            dispatch(pool, (dispatch_fn)task_function, &task_args[i]);
//            tasks_dispatched++;
//        }
//        // Sleep for a short while before dispatching more tasks
//        usleep(100000); // 100 milliseconds
//    }
//
//    // Sleep for some time to allow tasks to complete
//    usleep(1000000);
//
////    // Dispatch tasks using multiple threads
////    for (int i = 0; i < num_tasks; i++) {
////        task_args[i] = i; // Assign a unique ID to each task
////        dispatch(pool, (dispatch_fn)task_function, &task_args[i]);
////    }
////
////    // Sleep for some time to allow tasks to complete
////    usleep(1000000);
//
//    // Cleanup thread-local key
//    pthread_key_delete(thread_id_key);
//
//    destroy_threadpool(pool);
//
//    // Free allocated memory
//    free(task_args);
//
//    return EXIT_SUCCESS;
//}

