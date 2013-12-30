/*
 * elevator osio
 * Copyright (C) Octagram Sun <octagram@qq.com>
 */
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>

/*
 * why not async read ? 
 * kernel treat all read req as sync req. And most of write req are async.
 * see the source code:
 * in include/linux/blkdev.h
 * static inline bool rw_is_sync(unsigned int rw_flags)
 * {
 *	return !(rw_flags & REQ_WRITE) || (rw_flags & REQ_SYNC);
 * }
 */

/*** log ***/
#define OSIO_DEBUG                          0
#define osio_crt(x...)                      printk(KERN_CRIT "[OSIO CRT] " x)
#define osio_inf(x...)                      printk(KERN_INFO "[OSIO INF] " x)
#define osio_err(x...)                      printk(KERN_ERR "[OSIO ERR] " x)
#define osio_wrn(x...)                      printk(KERN_WARNING "[OSIO WRN] " x)
#if OSIO_DEBUG
   #define osio_dbg(x...)                   printk(KERN_DEBUG "[OSIO DBG] " x)
#else
   #define osio_dbg(x...)
#endif

/* macro */
#define FIFO_READ_BATCH				16
#define FIFO_SYNC_WRITE_BATCH			12
#define FIFO_ASYNC_WRITE_BATCH			8
#define SYNC_WRITE_STARVED_LINE			2
#define ASYNC_WRITE_STARVED_LINE		5

enum osio_direction {
	OSIO_DIR_READ = 0,	/* read */
	OSIO_DIR_SYNC_WRITE,	/* sync write */
	OSIO_DIR_ASYNC_WRITE,	/* async write */
	OSIO_DIR_UNDEF,		/* redecide the direction */
};

enum {
	OSIO_SYNC = 0,
	OSIO_ASYNC,
};

struct osio_data {
	struct list_head fifo_head[3];
	unsigned int batching;
	enum osio_direction fifo_dir;
	int write_starved[2];

	int fifo_batch[3];
	int write_starved_line[2];
};

static void osio_merged_requests(struct request_queue *q, struct request *rq,
				 struct request *next)
{
	list_del_init(&next->queuelist);
}

static void osio_add_request(struct request_queue *q, struct request *rq)
{
	struct osio_data *od = q->elevator->elevator_data;
	const int data_dir = rq_data_dir(rq) + !rq_is_sync(rq);

	osio_dbg("osio_add_request(), data_dir = %d, rq_is_sync(rq) = %d\n", data_dir, rq_is_sync(rq));
	list_add_tail(&rq->queuelist, &od->fifo_head[data_dir]);
}

static int osio_dispatch(struct request_queue *q, int force)
{
	struct osio_data *od = q->elevator->elevator_data;
	const unsigned int non_empty[3] = {!list_empty(&od->fifo_head[OSIO_DIR_READ]),
					   !list_empty(&od->fifo_head[OSIO_DIR_SYNC_WRITE]),
					   !list_empty(&od->fifo_head[OSIO_DIR_ASYNC_WRITE]),};
	struct request *rq = NULL;

	osio_dbg("osio_dispatch() 1, od->fifo_dir = %d\n", od->fifo_dir);
	osio_dbg("osio_dispatch() 1, non_empty[0] = %d\n", non_empty[0]);
	osio_dbg("osio_dispatch() 1, non_empty[1] = %d\n", non_empty[1]);
	osio_dbg("osio_dispatch() 1, non_empty[2] = %d\n", non_empty[2]);

	if (od->fifo_dir != OSIO_DIR_UNDEF) {
		if ((od->batching > od->fifo_batch[od->fifo_dir]) || (!non_empty[od->fifo_dir])) {
			od->fifo_dir = OSIO_DIR_UNDEF;
		} else {
			goto dispatch_request;
		}
	}

	/* redecide the direction */
	if (non_empty[OSIO_DIR_READ]) {
		goto dir_read;
	}

	if (non_empty[OSIO_DIR_SYNC_WRITE]) {
		goto dir_sync_write;
	}

	if (non_empty[OSIO_DIR_ASYNC_WRITE]) {
		goto dir_async_write;
	}

	return 0;

dir_read:
	/* find a starved write rq */
	if ((od->write_starved[OSIO_SYNC] > od->write_starved_line[OSIO_SYNC]) && non_empty[OSIO_DIR_SYNC_WRITE]) {
		goto dir_sync_write;
	} else if ((od->write_starved[OSIO_ASYNC] > od->write_starved_line[OSIO_ASYNC]) && non_empty[OSIO_DIR_ASYNC_WRITE]) {
		goto dir_async_write;
	}

	od->fifo_dir = OSIO_DIR_READ;
	od->batching = 0;
	od->write_starved[OSIO_SYNC]++;
	od->write_starved[OSIO_ASYNC]++;
	goto dispatch_request;

dir_sync_write:
	if ((od->write_starved[OSIO_ASYNC] > od->write_starved_line[OSIO_ASYNC]) && non_empty[OSIO_DIR_ASYNC_WRITE]) {
		goto dir_async_write;
	}

	od->fifo_dir = OSIO_DIR_SYNC_WRITE;
	od->batching = 0;
	od->write_starved[OSIO_SYNC] = 0;
	od->write_starved[OSIO_ASYNC]++;
	goto dispatch_request;

dir_async_write:
	od->fifo_dir = OSIO_DIR_ASYNC_WRITE;
	od->batching = 0;
	od->write_starved[OSIO_ASYNC] = 0;
	od->write_starved[OSIO_SYNC]++;
	goto dispatch_request;

dispatch_request:
	/* dispatch req */
	osio_dbg("osio_dispatch() 2, od->fifo_dir = %d\n", od->fifo_dir);
	osio_dbg("osio_dispatch() 2, od->batching = %d\n", od->batching);
	rq = rq_entry_fifo(od->fifo_head[od->fifo_dir].next);
	list_del_init(&rq->queuelist);
	elv_dispatch_add_tail(q, rq);
	od->batching ++;
	return 1;
}

static struct request * osio_former_request(struct request_queue *q, struct request *rq)
{
	struct osio_data *od = q->elevator->elevator_data;
	const int data_dir = rq_data_dir(rq) + !rq_is_sync(rq);

	if (rq->queuelist.prev == &od->fifo_head[data_dir])
		return NULL;
	return list_entry(rq->queuelist.prev, struct request, queuelist);
}

static struct request * osio_latter_request(struct request_queue *q, struct request *rq)
{
	struct osio_data *od = q->elevator->elevator_data;
	const int data_dir = rq_data_dir(rq) + !rq_is_sync(rq);

	if (rq->queuelist.next == &od->fifo_head[data_dir])
		return NULL;
	return list_entry(rq->queuelist.next, struct request, queuelist);
}

static int osio_init_queue(struct request_queue *q, struct elevator_type *e)
{
	struct osio_data *od;
	struct elevator_queue *eq;

	eq = elevator_alloc(q, e);
	if (!eq)
		return -ENOMEM;

	od = kmalloc_node(sizeof(*od), GFP_KERNEL, q->node);
	if (!od) {
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}
	eq->elevator_data = od;

	INIT_LIST_HEAD(&od->fifo_head[OSIO_DIR_READ]);
	INIT_LIST_HEAD(&od->fifo_head[OSIO_DIR_SYNC_WRITE]);
	INIT_LIST_HEAD(&od->fifo_head[OSIO_DIR_ASYNC_WRITE]);
	od->batching = 0;
	od->fifo_dir = OSIO_DIR_UNDEF;
	od->write_starved[OSIO_SYNC] = 0;
	od->write_starved[OSIO_ASYNC] = 0;
	od->fifo_batch[OSIO_DIR_READ] = FIFO_READ_BATCH;
	od->fifo_batch[OSIO_DIR_SYNC_WRITE] = FIFO_SYNC_WRITE_BATCH;
	od->fifo_batch[OSIO_DIR_ASYNC_WRITE] = FIFO_ASYNC_WRITE_BATCH;
	od->write_starved_line[OSIO_SYNC] = SYNC_WRITE_STARVED_LINE;
	od->write_starved_line[OSIO_ASYNC] = ASYNC_WRITE_STARVED_LINE;

	spin_lock_irq(q->queue_lock);
	q->elevator = eq;
	spin_unlock_irq(q->queue_lock);
	return 0;
}

static void osio_exit_queue(struct elevator_queue *e)
{
	struct osio_data *od = e->elevator_data;

	BUG_ON(!list_empty(&od->fifo_head[OSIO_DIR_READ]));
	BUG_ON(!list_empty(&od->fifo_head[OSIO_DIR_SYNC_WRITE]));
	BUG_ON(!list_empty(&od->fifo_head[OSIO_DIR_ASYNC_WRITE]));
	kfree(od);
}

/*
 * sysfs interface
 */
static ssize_t osio_var_show(int var, char *page)
{
	return sprintf(page, "%d\n", var);
}

static ssize_t osio_var_store(int *var, const char *page, size_t count)
{
	char *p = (char *) page;

	*var = simple_strtol(p, &p, 10);
	return count;
}

#define OSIO_SHOW_FUNCTION(__FUNC, __VAR, __CONV)			\
static ssize_t __FUNC(struct elevator_queue *e, char *page)		\
{									\
	struct osio_data *od = e->elevator_data;			\
	int __data = od->__VAR;						\
	if (__CONV)							\
		__data = jiffies_to_msecs(__data);			\
	return osio_var_show(__data, (page));				\
}

OSIO_SHOW_FUNCTION(osio_sync_write_starved_line_show, write_starved_line[OSIO_SYNC], 0);
OSIO_SHOW_FUNCTION(osio_async_write_starved_line_show, write_starved_line[OSIO_ASYNC], 0);
OSIO_SHOW_FUNCTION(osio_fifo_read_batch_show, fifo_batch[OSIO_DIR_READ], 0);
OSIO_SHOW_FUNCTION(osio_fifo_sync_write_batch_show, fifo_batch[OSIO_DIR_SYNC_WRITE], 0);
OSIO_SHOW_FUNCTION(osio_fifo_async_write_batch_show, fifo_batch[OSIO_DIR_ASYNC_WRITE], 0);

#define OSIO_STORE_FUNCTION(__FUNC, __VAR, MIN, MAX, __CONV)			\
static ssize_t __FUNC(struct elevator_queue *e, const char *page, size_t count)	\
{										\
	struct osio_data *od = e->elevator_data;				\
	int __data;								\
	int ret = osio_var_store(&__data, (page), count);			\
	if (__data < (MIN))							\
		__data = (MIN);							\
	else if (__data > (MAX))						\
		__data = (MAX);							\
	if (__CONV)								\
		od->__VAR = msecs_to_jiffies(__data);				\
	else									\
		od->__VAR = __data;						\
	return ret;								\
}

OSIO_STORE_FUNCTION(osio_sync_write_starved_line_store, write_starved_line[OSIO_SYNC], 1, INT_MAX, 0);
OSIO_STORE_FUNCTION(osio_async_write_starved_line_store, write_starved_line[OSIO_ASYNC], 1, INT_MAX, 0);
OSIO_STORE_FUNCTION(osio_fifo_read_batch_store, fifo_batch[OSIO_DIR_READ], 1, INT_MAX, 0);
OSIO_STORE_FUNCTION(osio_fifo_sync_write_batch_store, fifo_batch[OSIO_DIR_SYNC_WRITE], 1, INT_MAX, 0);
OSIO_STORE_FUNCTION(osio_fifo_async_write_batch_store, fifo_batch[OSIO_DIR_ASYNC_WRITE], 1, INT_MAX, 0);

#define OSIO_ATTR(name) \
	__ATTR(name, S_IRUGO|S_IWUSR, osio_##name##_show, \
				      osio_##name##_store)

static struct elv_fs_entry osio_attrs[] = {
	OSIO_ATTR(sync_write_starved_line),
	OSIO_ATTR(async_write_starved_line),
	OSIO_ATTR(fifo_read_batch),
	OSIO_ATTR(fifo_sync_write_batch),
	OSIO_ATTR(fifo_async_write_batch),
	__ATTR_NULL,
};

/* osio */
static struct elevator_type elevator_osio = {
	.ops = {
		.elevator_merge_req_fn		= osio_merged_requests,
		.elevator_dispatch_fn		= osio_dispatch,
		.elevator_add_req_fn		= osio_add_request,
		.elevator_former_req_fn		= osio_former_request,
		.elevator_latter_req_fn		= osio_latter_request,
		.elevator_init_fn		= osio_init_queue,
		.elevator_exit_fn		= osio_exit_queue,
	},
	.elevator_name = "osio",
	.elevator_attrs = osio_attrs,
	.elevator_owner = THIS_MODULE,
};

/* module init & exit */
static int __init osio_init(void)
{
	return elv_register(&elevator_osio);
}

static void __exit osio_exit(void)
{
	elv_unregister(&elevator_osio);
}

module_init(osio_init);
module_exit(osio_exit);


MODULE_AUTHOR("Octagram Sun <octagram@qq.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("osio scheduler");
