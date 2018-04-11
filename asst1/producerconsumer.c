/* This file will contain your solution. Modify it as you wish. */
#include <types.h>
#include "producerconsumer_driver.h"
#include <synch.h>

/* Declare any variables you need here to keep track of and
   synchronise your bounded. A sample declaration of a buffer is shown
   below. You can change this if you choose another implementation. */

static struct pc_data buffer[BUFFER_SIZE];

static struct semaphore *mutex;
static struct semaphore *empty;
static struct semaphore *full;

static int queue_front;
static int queue_rear;


/* consumer_receive() is called by a consumer to request more data. It
   should block on a sync primitive if no data is available in your
   buffer. */

struct pc_data consumer_receive(void)
{
        struct pc_data thedata;
        P(full);
        P(mutex);
        thedata = buffer[queue_front];
        queue_front = (queue_front + 1) % BUFFER_SIZE;
        V(mutex);
        V(empty);
        return thedata;
}

/* procucer_send() is called by a producer to store data in your
   bounded buffer. */

void producer_send(struct pc_data item)
{
        P(empty);
        P(mutex);
        buffer[queue_rear] = item;
        queue_rear = (queue_rear + 1) % BUFFER_SIZE;
        V(mutex);
        V(full);
}




/* Perform any initialisation (e.g. of global data) you need
   here. Note: You can panic if any allocation fails during setup */

void producerconsumer_startup(void)
{
        mutex = sem_create("mutex", 1);
        empty = sem_create("empty", BUFFER_SIZE);
        full = sem_create("full", 0);
        queue_front = queue_rear = 0;
}

/* Perform any clean-up you need here */
void producerconsumer_shutdown(void)
{
        sem_destroy(mutex);
        sem_destroy(empty);
        sem_destroy(full);
}

