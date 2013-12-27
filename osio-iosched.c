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

#define FIFO_READ_BATCH		16
#define FIFO_WRITE_BATCH	8
#define WRITES_STARVED		2

enum osio_direction {
	OSIO_READ = 0,
	OSIO_WRITE,
	OSIO_UNDEF,
};

struct osio_data {
	struct list_head fifo_head[2];
	unsigned int batching;
	enum osio_direction fifo_dir;
	int starved;

	int fifo_batch[2];
	int writes_starved;
};

static void osio_merged_requests(struct request_queue *q, struct request *rq,
				 struct request *next)
{
	list_del_init(&next->queuelist);
}

static int osio_dispatch(struct request_queue *q, int force)
{
	struct osio_data *od = q->elevator->elevator_data;
	const unsigned int non_empty[2] = {!list_empty(&od->fifo_head[OSIO_READ]),
					   !list_empty(&od->fifo_head[OSIO_WRITE]),};
	struct request *rq = NULL;

	if (od->fifo_dir == OSIO_UNDEF) {
		;
	} else if (od->batching > od->fifo_batch[od->fifo_dir]) {
		od->fifo_dir = OSIO_UNDEF;
	} else if (!non_empty[od->fifo_dir]) {
		od->fifo_dir = OSIO_UNDEF;
	}

	if (od->fifo_dir == OSIO_UNDEF) {
		if (non_empty[OSIO_READ]) {
			od->fifo_dir = OSIO_READ;
			od->starved ++;
		} else if (non_empty[OSIO_WRITE]) {
			od->fifo_dir = OSIO_WRITE;
			od->starved = 0;
		}

		if ((od->starved > od->writes_starved) && non_empty[OSIO_WRITE]) {
			od->fifo_dir = OSIO_WRITE;
			od->starved = 0;
		}
		od->batching = 0;
	}

	if (od->fifo_dir != OSIO_UNDEF) {
		rq = list_entry(od->fifo_head[od->fifo_dir].next, struct request, queuelist);
		list_del_init(&rq->queuelist);
		elv_dispatch_add_tail(q, rq);
		od->batching ++;
		return 1;
	}

	return 0;
}

static void osio_add_request(struct request_queue *q, struct request *rq)
{
	struct osio_data *od = q->elevator->elevator_data;
	const int data_dir = rq_data_dir(rq);

	list_add_tail(&rq->queuelist, &od->fifo_head[data_dir]);
}

static struct request * osio_former_request(struct request_queue *q, struct request *rq)
{
	struct osio_data *od = q->elevator->elevator_data;
	const int data_dir = rq_data_dir(rq);

	if (rq->queuelist.prev == &od->fifo_head[data_dir])
		return NULL;
	return list_entry(rq->queuelist.prev, struct request, queuelist);
}

static struct request * osio_latter_request(struct request_queue *q, struct request *rq)
{
	struct osio_data *od = q->elevator->elevator_data;
	const int data_dir = rq_data_dir(rq);

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

	INIT_LIST_HEAD(&od->fifo_head[OSIO_READ]);
	INIT_LIST_HEAD(&od->fifo_head[OSIO_WRITE]);
	od->batching = 0;
	od->fifo_batch[OSIO_READ] = FIFO_READ_BATCH;
	od->fifo_batch[OSIO_WRITE] = FIFO_WRITE_BATCH;
	od->fifo_dir = OSIO_UNDEF;
	od->starved = 0;
	od->writes_starved = WRITES_STARVED;

	spin_lock_irq(q->queue_lock);
	q->elevator = eq;
	spin_unlock_irq(q->queue_lock);
	return 0;
}

static void osio_exit_queue(struct elevator_queue *e)
{
	struct osio_data *od = e->elevator_data;

	BUG_ON(!list_empty(&od->fifo_head[OSIO_READ]));
	BUG_ON(!list_empty(&od->fifo_head[OSIO_WRITE]));
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

OSIO_SHOW_FUNCTION(osio_writes_starved_show, writes_starved, 0);
OSIO_SHOW_FUNCTION(osio_fifo_read_batch_show, fifo_batch[OSIO_READ], 0);
OSIO_SHOW_FUNCTION(osio_fifo_write_batch_show, fifo_batch[OSIO_WRITE], 0);

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

OSIO_STORE_FUNCTION(osio_writes_starved_store, writes_starved, INT_MIN, INT_MAX, 0);
OSIO_STORE_FUNCTION(osio_fifo_read_batch_store, fifo_batch[OSIO_READ], 0, INT_MAX, 0);
OSIO_STORE_FUNCTION(osio_fifo_write_batch_store, fifo_batch[OSIO_WRITE], 0, INT_MAX, 0);

#define OSIO_ATTR(name) \
	__ATTR(name, S_IRUGO|S_IWUSR, osio_##name##_show, \
				      osio_##name##_store)

static struct elv_fs_entry osio_attrs[] = {
	OSIO_ATTR(writes_starved),
	OSIO_ATTR(fifo_read_batch),
	OSIO_ATTR(fifo_write_batch),
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
