#include <types.h>
#include <lib.h>
#include <synch.h>
#include <test.h>
#include <thread.h>

#include "bar.h"
#include "bar_driver.h"

/*
 * **********************************************************************
 * YOU ARE FREE TO CHANGE THIS FILE BELOW THIS POINT AS YOU SEE FIT
 *
 */

/* Declare any globals you need here (e.g. locks, etc...) */
#define QUEUE_SIZE NBARTENDERS

static struct barorder *queue_bar_order[QUEUE_SIZE]; // the job queue
static int queue_front;
static int queue_rear;

static struct semaphore *queue_mutex;
static struct semaphore *empty;
static struct semaphore *full;

static struct semaphore *bottle_mutexs[NBOTTLES]; // the lock every kind of bottle

/*
 * **********************************************************************
 * FUNCTIONS EXECUTED BY CUSTOMER THREADS
 * **********************************************************************
 */
char *generate_uuid_by_address(const void *);
char *generate_uuid_by_address(const void *p)
{
        char *str = kmalloc(11);
        int i = snprintf(str, 11, "%p", p);
        str[i] = '\0';
        return str;
}

char *generate_uuid_by_seed(int);
char *generate_uuid_by_seed(int i)
{
        char *str = kmalloc(15);
        int size = snprintf(str, 15, "uuid%d", i);
        str[size] = '\0';
        return str;
}

//a thread method, not used int this version
void to_addinto_queue(void *, unsigned long);
void to_addinto_queue(void *p_barorder, unsigned long unusedlong)
{
        struct barorder *order = (struct barorder *)p_barorder;
        (void)unusedlong;
        thread_yield(); // make sure the caller thread execute first
        P(empty);
        P(queue_mutex);
        queue_bar_order[queue_rear] = order;
        queue_rear = (queue_rear + 1) % QUEUE_SIZE;
        // P(order->order_done) should be here logically, however it is blocked
        V(queue_mutex);
        V(full);
}

/*
 * order_drink()
 *
 * Takes one argument referring to the order to be filled. The
 * function makes the order available to staff threads and then blocks
 * until a bartender has filled the glass with the appropriate drinks.
 */

void order_drink(struct barorder *order)
{
        // because P(order->order_done) will block, fork a new thread to excute the other part of citical region
        // int result = thread_fork("to_addinto_queue", NULL, &to_addinto_queue, order, 0);
        // if (result)
        // {
        //         panic("order_drink: thread_fork failed: %s\n",
        //               strerror(result));
        // }

        if (!order->go_home_flag && !order->is_semaphore_init)
        {
                order->order_done = sem_create(generate_uuid_by_address(order), 0);
                if (order->order_done == NULL)
                {
                        panic("order->order_done: couldn't create semaphore\n");
                }
                order->is_semaphore_init = 1;
        }
        P(empty);
        P(queue_mutex);
        queue_bar_order[queue_rear] = order;
        queue_rear = (queue_rear + 1) % QUEUE_SIZE;
        // P(order->order_done) should be here logically, however it is blocked
        V(queue_mutex);
        V(full);

        if (!order->go_home_flag)
        {
                P(order->order_done);
        }
}

/*
 * **********************************************************************
 * FUNCTIONS EXECUTED BY BARTENDER THREADS
 * **********************************************************************
 */

/*
 * take_order()
 *
 * This function waits for a new order to be submitted by
 * customers. When submitted, it returns a pointer to the order.
 *
 */

struct barorder *take_order(void)
{
        struct barorder *ret = NULL;
        P(full);
        P(queue_mutex);
        ret = queue_bar_order[queue_front];
        queue_front = (queue_front + 1) % QUEUE_SIZE;
        V(queue_mutex);
        V(empty);
        return ret;
}

void sort_order_requested_bottles(struct barorder *);
void sort_order_requested_bottles(struct barorder *order) // insert sort
{
        unsigned i, j;
        unsigned tmp;

        for (i = 1; i < DRINK_COMPLEXITY; i++)
        {
                j = i - 1;
                tmp = order->requested_bottles[i];
                while (j > 0 && tmp < order->requested_bottles[j])
                {
                        order->requested_bottles[j + 1] = order->requested_bottles[j];
                        --j;
                }
                order->requested_bottles[j + 1] = tmp;
        }
}

/*
 * fill_order()
 *
 * This function takes an order provided by take_order and fills the
 * order using the mix() function to mix the drink.
 *
 * NOTE: IT NEEDS TO ENSURE THAT MIX HAS EXCLUSIVE ACCESS TO THE
 * REQUIRED BOTTLES (AND, IDEALLY, ONLY THE BOTTLES) IT NEEDS TO USE TO
 * FILL THE ORDER.
 */

void fill_order(struct barorder *order)
{
        /* add any sync primitives you need to ensure mutual exclusion
           holds as described */
        sort_order_requested_bottles(order); // sort the resource(it should be done in bar_driver actually) in case of deadlock
        unsigned i;
        unsigned bottle;
        for (i = 0; i < DRINK_COMPLEXITY; ++i)
        {
                bottle = order->requested_bottles[i];
                if (bottle)
                {
                        P(bottle_mutexs[bottle - 1]);
                }
        }
        /* the call to mix must remain */
        mix(order);
        for (i = DRINK_COMPLEXITY; i > 0; --i)
        {
                bottle = order->requested_bottles[i - 1];
                if (bottle)
                {
                        V(bottle_mutexs[bottle - 1]);
                }
        }
}

/*
 * serve_order()
 *
 * Takes a filled order and makes it available to (unblocks) the
 * waiting customer.
 */

void serve_order(struct barorder *order)
{
        if (!order->go_home_flag)
        {
                V(order->order_done); // unblock the order, customer can drink now
                // kfree(order->order_done);
        }
}

/*
 * **********************************************************************
 * INITIALISATION AND CLEANUP FUNCTIONS
 * **********************************************************************
 */

/*
 * bar_open()
 *
 * Perform any initialisation you need prior to opening the bar to
 * bartenders and customers. Typically, allocation and initialisation of
 * synch primitive and variable.
 */

void bar_open(void)
{
        queue_front = queue_rear = 0;

        queue_mutex = sem_create("queue_mutex", 1);
        if (queue_mutex == NULL)
        {
                panic("bar_open: couldn't create semaphore\n");
        }
        empty = sem_create("empty", QUEUE_SIZE);
        if (empty == NULL)
        {
                panic("bar_open: couldn't create semaphore\n");
        }
        full = sem_create("full", 0);
        if (full == NULL)
        {
                panic("bar_open: couldn't create semaphore\n");
        }
        unsigned i;
        for (i = 0; i < NBOTTLES; ++i)
        {
                bottle_mutexs[i] = sem_create(generate_uuid_by_seed(i), 1);
                if (bottle_mutexs[i] == NULL)
                {
                        panic("bar_open: couldn't create semaphore\n");
                }
        }
}

/*
 * bar_close()
 *
 * Perform any cleanup after the bar has closed and everybody
 * has gone home.
 */

void bar_close(void)
{
        queue_front = queue_rear = 0;
        sem_destroy(queue_mutex);
        sem_destroy(empty);
        sem_destroy(full);
        unsigned i;
        for (i = 0; i < NBOTTLES; ++i)
        {
                sem_destroy(bottle_mutexs[i]);
        }
}
