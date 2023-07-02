/* 
 * paddr - Kernel module to find physical address
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
#include <linux/module.h>
#include <linux/pagewalk.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/pgtable.h>

MODULE_LICENSE("GPL");

/* Packet structure defined in app.c. */
struct packet {
	pid_t pid;
	unsigned long vaddr;
	unsigned long paddr;
};

/* Directories */
static struct dentry *dir, *output;

static ssize_t read_output(struct file *fp,
			   char __user *user_buffer,
			   size_t length,
			   loff_t *position)
{
	struct task_struct *task = NULL;	/* Process task */

	struct packet *buffer = NULL;		/* Kernel space buffer */

	pid_t pid;		/* Given process id */
	unsigned long vaddr;	/* Given virtual address */
	unsigned long paddr;	/* Corresponding physical address to compute */

	/*
	 * 5-level page table.
	 *
	 * PGD -> P4D -> PUD -> PMD -> PTE
	 */

	pgd_t *pgd = NULL;	/* Page global directory */
	p4d_t *p4d = NULL;	/* Page 4th-level directory */
	pud_t *pud = NULL;	/* Page upper directory */
	pmd_t *pmd = NULL;	/* Page middle directory */
	pte_t *pte = NULL;	/* Page table entry */

	unsigned long ppn;	/* Physical page number */
	unsigned long ppo;	/* Physical page offset */

	int ok = 1;

	/* Check given pointer is valid. */
	if (!user_buffer) {
		printk("Invalid user buffer\n");
		return -1;
	}

	/*
	 * Copy user_buffer to kernel space buffer.
	 */

	buffer = kmalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer) {
		printk("kmalloc error\n");
		return -1;
	}

	if (ok && copy_from_user(buffer, user_buffer, sizeof(*buffer))) {
		printk("Cannot read user buffer\n");
		ok = 0;
	}


	/* Get given pid and virtual address. */
	if (ok) {
		pid = buffer->pid;
		vaddr = buffer->vaddr;
	}

	/* Get process task of pid. */
	if (ok && !(task = pid_task(find_vpid(pid), PIDTYPE_PID))) {
		printk("Cannot get process table\n");
		ok = 0;
	}

	/* Get PGD from task mm. */
	if (ok) {
		pgd = pgd_offset(task->mm, vaddr);
		if (!pgd || pgd_none(*pgd) || pgd_bad(*pgd)) {
			printk("No page global directory\n");
			ok = 0;
		}
	}

	/* PGD -> P4D */
	if (ok) {
		p4d = p4d_offset(pgd, vaddr);
		if (!p4d || p4d_none(*p4d) || p4d_bad(*p4d)) {
			printk("No page 4th-level directory\n");
			ok = 0;
		}
	}

	/* P4D -> PUD */
	if (ok) {
		pud = pud_offset(p4d, vaddr);
		if (!pud || pud_none(*pud) || pud_bad(*pud)) {
			printk("No page upper directory\n");
			ok = 0;
		}
	}

	/* PUD -> PMD */
	if (ok) {
		pmd = pmd_offset(pud, vaddr);
		if (!pmd || pmd_none(*pmd) || pmd_bad(*pmd)) {
			printk("No page middle directory\n");
			ok = 0;
		}
	}

	/* PMD -> PTE */
	if (ok) {
		pte = pte_offset_kernel(pmd, vaddr);
		if (!pte || pte_none(*pte)) {
			printk("No page table entry\n");
			ok = 0;
		}
	}

	/* Compute physical address. */
	if (ok) {
		/* Get page number from PTE. */
		ppn = pte_pfn(*pte);

		/* Get offset directly from virtual address. */
		ppo = vaddr & ~PAGE_MASK;

		/* Compute physical address by combining them. */
		paddr = (ppn << PAGE_SHIFT) | ppo;

		/* Write it down. */
		buffer->paddr = paddr;
	}

	/* Copy kernel space buffer to user_buffer. */
	if (ok && copy_to_user(user_buffer, buffer, sizeof(*buffer))) {
		printk("Cannot write user buffer\n");
		ok = 0;
	}

 	kfree(buffer);
 	buffer = NULL;

	return ok ? length : -1;
}

static const struct file_operations dbfs_fops = {
	.read = read_output,
};

static int __init dbfs_module_init(void)
{
	/* Create 'paddr' debug directory. */
	dir = debugfs_create_dir("paddr", NULL);
	if (!dir) {
		printk("Cannot create paddr dir\n");
		return -1;
	}

	/* Create 'output' debug file for output; readable + writable */
	output = debugfs_create_file("output", 0666, dir, NULL, &dbfs_fops);
	if (!output) {
		printk("Cannot create 'output' file\n");
		return -1;
	}

	printk("dbfs_paddr module initialize done\n");

	return 0;
}

static void __exit dbfs_module_exit(void)
{
	debugfs_remove_recursive(dir);

	printk("dbfs_paddr module exit\n");
}

module_init(dbfs_module_init);
module_exit(dbfs_module_exit);
