//
// Created by Ido Cohen on 20/12/2021.
//
#include <stdlib.h>
#include <stdio.h>

#include "threadpool.h"

threadpool *create_threadpool(int num_threads_in_pool) {

    threadpool *tp = (threadpool*)malloc(sizeof(threadpool));
    if(tp == NULL){
        perror("error: <sys_call>\n");
        return NULL;
    }
    tp->num_threads = num_threads_in_pool;
    tp->qsize = 0;

    tp->threads = (pthread_t*) malloc(sizeof(pthread_t) * num_threads_in_pool);
    if(tp->threads == NULL){
        free(tp);
        perror("error: <sys_call>\n");
        return NULL;
    }
    tp->qhead = NULL;
    tp->qtail = NULL;
    if(pthread_mutex_init(&tp->qlock, NULL) != 0){
        free(tp->threads);
        free(tp);
        perror("error: <sys_call>\n");
        return NULL;
    }
    if(pthread_cond_init(&tp->q_not_empty, NULL) != 0 || pthread_cond_init(&tp->q_empty, NULL) != 0){
        free(tp->threads);
        free(tp);
        perror("error: <sys_call>\n");
        return NULL;
    }
    tp->shutdown = 0;
    tp->dont_accept = 0;
    for (int i = 0; i < num_threads_in_pool; i++) {
        if(pthread_create(&tp->threads[i], NULL, do_work, (void*)tp) != 0){
            free(tp->threads);
            free(tp);
            perror("error: <sys_call>\n");
            return NULL;
        }
    }

    return tp;
}

void dispatch(threadpool *from_me, dispatch_fn dispatch_to_here, void *arg) {
    if(from_me->dont_accept == 1){
        return;
    }
    work_t *w = (work_t*)malloc(sizeof(work_t));
    if(w == NULL){
        return;
    }
    w->arg = arg;
    w->routine = dispatch_to_here;
    w->next = NULL;
    pthread_mutex_lock(&from_me->qlock);
    if(from_me->qsize == 0){
        from_me->qhead = w;
        from_me->qtail = w;
    } else {
        from_me->qtail->next = w;
        from_me->qtail = w;
    }
    from_me->qsize++;
    pthread_mutex_unlock(&from_me->qlock);
    pthread_cond_broadcast(&from_me->q_not_empty);
}

void destroy_threadpool(threadpool *destroyme) {
    pthread_mutex_lock(&destroyme->qlock);
    destroyme->dont_accept = 1;
    if(destroyme->qsize > 0){
        pthread_cond_wait(&destroyme->q_empty, &destroyme->qlock);
    }
    destroyme->shutdown = 1;
    pthread_mutex_unlock(&destroyme->qlock);
    pthread_cond_broadcast(&destroyme->q_not_empty);
    for (int i = 0; i < destroyme->num_threads; i++) {
        pthread_join(destroyme->threads[i], NULL);
    }
    pthread_mutex_destroy(&destroyme->qlock);
    pthread_cond_destroy(&destroyme->q_not_empty);
    pthread_cond_destroy(&destroyme->q_empty);
    free(destroyme->threads);
    free(destroyme);
}

void *do_work(void *p) {
    threadpool *tp = (threadpool*)p;
    while(1){
        if(tp->shutdown == 1){
            return NULL;
        }
        pthread_mutex_lock(&tp->qlock);
        if(tp->qsize != 0){
            work_t *w = tp->qhead;
            tp->qhead = tp->qhead->next;
            tp->qsize--;
            pthread_mutex_unlock(&tp->qlock);
            w->routine(w->arg);
            free(w);
            if(tp->dont_accept == 1 && tp->qsize == 0){
                pthread_cond_signal(&tp->q_empty);
            }
        } else{
            pthread_cond_wait(&tp->q_not_empty, &tp->qlock);
            pthread_mutex_unlock(&tp->qlock);
        }
    }
}

