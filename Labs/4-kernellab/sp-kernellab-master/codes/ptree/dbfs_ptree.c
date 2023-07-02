/* 
 * ptree - Kernel module to trace process tree
 * 
 * 2020-14378
 * stu70@sp01.snucse.org
 *
 * Gyojun Youn
 * youngyojun [at] snu.ac.kr
 * 
 * College of Engineering
 * Dept. of Computer Science and Engineering
 *
 */
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");

/*
 * Process linked list structure.
 *
 * The data of the head is nullptr.
 * Next process is a child process.
 * Prev process is a parent process.
 */
struct ptree_node {
	struct task_struct *data;
	struct list_head list;
};

/* Directories */
static struct dentry *dir, *input_dir, *ptree_dir;

/*
 * Blob for output 'ptree'
 *
 * Contain PAGE_SIZE characters as data.
 */
static struct debugfs_blob_wrapper *ptree_blob;

static ssize_t write_pid_to_input(struct file *fp,
				  const char __user *user_buffer,
				  size_t length,
				  loff_t *position)
{

	/* Head of process linked list */
	struct ptree_node root = {
		.data = NULL,
		.list = LIST_HEAD_INIT(root.list),
	};

	struct task_struct *curr = NULL;	/* Current process to track */

	char *ptr = NULL;	/* Current position to write */

	/*
	 * End position of the blob data.
	 *	== data + size / sizeof(char)
	 *
	 * Written ptr must be in [ptree_blob->data, end).
	 */
	char *end = NULL;

	char *buffer = NULL;	/* Kernel space buffer */

	pid_t input_pid;	/* Given pid as input */

	int ok = 1;

	/*
	 * Check given pointers are valid.
	 */

	if (!user_buffer) {
		printk("Invalid user buffer\n");
		return -1;
	}

	if (!ptree_blob || !ptree_blob->data) {
		printk("Output blob is null\n");
		return -1;
	}

	ptr = ptree_blob->data;
	end = ptr + ptree_blob->size / sizeof(char);

	/*
	 * Copy user_buffer to kernel space buffer.
	 */

	if (ok && !(buffer = kmalloc(length, GFP_KERNEL))) {
		printk("kmalloc error\n");
		ok = 0;
	}

	if (ok && copy_from_user(buffer, user_buffer, length)) {
		printk("Cannot copy from user buffer\n");
		ok = 0;
	}

	/* Read pid in buffer. */
	if (ok && 1 != sscanf(buffer, "%u", &input_pid)) {
		printk("Cannot read pid\n");
		ok = 0;
	}

	kfree(buffer);
	buffer = NULL;

	if (!ok)
		return -1;

	/* Get process task of input_pid. */
	if (ok && !(curr = pid_task(find_vpid(input_pid), PIDTYPE_PID))) {
		printk("Cannot get process table\n");
		ok = 0;
	}

	/* Trace process tree and make a linked list. */
	while (ok && curr && 0 < curr->pid) {
		/* Allocate a node to store. */
		struct ptree_node *node = kmalloc(sizeof(*node), GFP_KERNEL);
		if (!node) {
			printk("kmalloc error\n");
			ok = 0;
			break;
		}

		/* Store the current task pointer. */
		node->data = curr;

		/* Insert the node right after the head. */
		list_add(&node->list, &root.list);

		/* Escape a self loop. */
		if (curr == curr->real_parent)
			break;

		/* Move to its parent process. */
		curr = curr->real_parent;
	}

	if (ok) {
		/*
		 * Trace the linked list to write output.
		 */

		struct ptree_node *node;
		struct task_struct *data;

		list_for_each_entry(node, &root.list, list) {
			/* Should not be happened, but I am not sure. */
			if (!node)
				continue;

			data = node->data;

			/* Why it is not happend? */
			if (!data)
				continue;

			/* Write output in a form of "process_command (process_id)". */
			ptr += snprintf(ptr, end - ptr, "%s (%d)\n", data->comm, data->pid);

			/* Lack of space, just break. */
			if (end <= ptr)
				break;
		}

		/* Free every node in the linked list. */
		while (!list_is_singular(&root.list)) {
			/* Get the very first node. */
			node = list_entry(root.list.next, struct ptree_node, list);

			/* Remove it from the linked list. */
			list_del(&node->list);

			/* Free the node. */
			kfree(node);
			node = NULL;
		}

		/* Check the output was trimmed. */
		if (end <= ptr && *(end - 1)) {
			printk("Too large output\n");
			ok = 0;
		}
	}

	/*
	 * Clean unused space.
	 */

	while (ptr < end) {
		*ptr = '\0';
		ptr++;
	}

	*(end - 1) = '\0';

	return ok ? length : -1;
}

static const struct file_operations dbfs_fops = {
	.write = write_pid_to_input,
};

static int __init dbfs_module_init(void)
{
	/*
	 * Data and size of a blob for output.
	 */
	void *data = NULL;
	const long size = PAGE_SIZE * sizeof(char);

	/* Create 'ptree' debug directory. */
	dir = debugfs_create_dir("ptree", NULL);
	if (!dir) {
		printk("Cannot create ptree dir\n");
		return -1;
	}

	/* Create 'input' debug file for input; only writable */
	input_dir = debugfs_create_file("input", 0222, dir, NULL, &dbfs_fops);
	if (!input_dir) {
		printk("Cannot create input file\n");
		return -1;
	}

	/* Allocate a blob structure. */
	ptree_blob = kmalloc(sizeof(*ptree_blob), GFP_KERNEL);
	if (!ptree_blob) {
		printk("kmalloc error\n");
		return -1;
	}

	/* Allocate data area. */
	data = kmalloc(size, GFP_KERNEL);
	if (!data) {
		printk("kmalloc error\n");
		
		kfree(ptree_blob);
		ptree_blob = NULL;

		return -1;
	}

	/* Set data and size of a blob. */
	ptree_blob->data = data;
	ptree_blob->size = size;

	/* Create 'ptree' blob for output; read-only. */
	ptree_dir = debugfs_create_blob("ptree", 0444, dir, ptree_blob);
	if (!ptree_dir) {
		printk("Cannot create blob for output\n");

		kfree(data);
		data = NULL;

		kfree(ptree_blob);
		ptree_blob = NULL;

		return -1;
	}

	printk("dbfs_ptree module initialize done\n");

	return 0;
}

static void __exit dbfs_module_exit(void)
{
	debugfs_remove_recursive(dir);

	if (ptree_blob) {
		kfree(ptree_blob->data);
		ptree_blob->data = NULL;
	}

	kfree(ptree_blob);
	ptree_blob = NULL;

	printk("dbfs_ptree module exit\n");
}

module_init(dbfs_module_init);
module_exit(dbfs_module_exit);
