// sysinfo_plus.c — System Information Kernel Module
// Creates /proc/sysinfo_plus exposing:
//   CPU count, Total RAM, Free RAM, Kernel version, Uptime, Load average
//
// Build:  make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
// Load:   sudo insmod sysinfo_plus.ko
// Read:   cat /proc/sysinfo_plus
// Remove: sudo rmmod sysinfo_plus

#include <linux/module.h>       // MODULE_LICENSE, module_init/exit
#include <linux/kernel.h>       // printk, UTS_RELEASE
#include <linux/proc_fs.h>      // proc_create, proc_remove, proc_ops
#include <linux/seq_file.h>     // seq_file, seq_printf, single_open
#include <linux/cpumask.h>      // num_online_cpus()
#include <linux/mm.h>           // si_meminfo(), struct sysinfo
#include <linux/sched/loadavg.h>// avenrun[], LOAD_INT(), LOAD_FRAC(), FIXED_1
#include <linux/utsname.h>      // utsname() → kernel release string
#include <linux/jiffies.h>      // jiffies, HZ — for uptime calculation
#include <linux/time.h>         // struct timespec64 (uptime formatting)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("sysinfo_plus");
MODULE_DESCRIPTION("System Information Module — /proc/sysinfo_plus");
MODULE_VERSION("1.0");

#define PROC_ENTRY  "sysinfo_plus"
#define KB_PER_PAGE (PAGE_SIZE / 1024)   /* usually 4 — pages → KiB */

/* ─────────────────────────────────────────────────────────────────────────
 * format_uptime()
 *   Converts a raw jiffies count into a human-readable "Xd Xh Xm Xs" string.
 *
 *   jiffies  — the kernel's monotonic tick counter, incremented HZ times/sec.
 *   HZ       — ticks per second (typically 250 or 1000, CONFIG-dependent).
 *   jiffies_to_timespec64() — safe conversion to seconds + nanoseconds,
 *              handles jiffies wraparound on 32-bit kernels.
 * ──────────────────────────────────────────────────────────────────────── */
static void format_uptime(struct seq_file *m)
{
    struct timespec64 ts;
    unsigned long days, hours, mins, secs;

    /* jiffies is the total uptime in ticks; subtract INITIAL_JIFFIES to
     * get elapsed ticks since boot, then convert to wall-clock seconds. */
    jiffies_to_timespec64(jiffies - INITIAL_JIFFIES, &ts);

    secs  = ts.tv_sec;
    days  = secs / 86400; secs %= 86400;
    hours = secs / 3600;  secs %= 3600;
    mins  = secs / 60;    secs %= 60;

    seq_printf(m, "Uptime          : %lu days, %lu hours, %lu min, %lu sec\n",
               days, hours, mins, secs);
}

/* ─────────────────────────────────────────────────────────────────────────
 * format_loadavg()
 *   Reads the kernel's global avenrun[] array — three unsigned longs holding
 *   the 1-, 5-, and 15-minute exponential moving averages of the run-queue
 *   length, in fixed-point format (scaled by FIXED_1 = 2048).
 *
 *   LOAD_INT(x)  → integer part  = x >> FSHIFT
 *   LOAD_FRAC(x) → fractional part (2 decimal places) via:
 *                  ((x & (FIXED_1-1)) * 100 + FIXED_1/2) / FIXED_1
 *
 *   avenrun[] is updated every 5 ticks by the scheduler's calc_global_load().
 * ──────────────────────────────────────────────────────────────────────── */
static void format_loadavg(struct seq_file *m)
{
    unsigned long avnrun[3];

    /* get_avenrun() copies the array under a read lock — safer than reading
     * avenrun[] directly which could race with the scheduler's update. */
    get_avenrun(avnrun, FIXED_1 / 200, 0);

    seq_printf(m, "Load average    : %lu.%02lu  %lu.%02lu  %lu.%02lu"
                  "  (1 min / 5 min / 15 min)\n",
               LOAD_INT(avnrun[0]), LOAD_FRAC(avnrun[0]),
               LOAD_INT(avnrun[1]), LOAD_FRAC(avnrun[1]),
               LOAD_INT(avnrun[2]), LOAD_FRAC(avnrun[2]));
}

/* ─────────────────────────────────────────────────────────────────────────
 * sysinfo_show()
 *   The seq_file show function — called once per open().
 *   Writes all fields to the seq_file buffer via seq_printf().
 *
 *   struct sysinfo (from <linux/sysinfo.h>, populated by si_meminfo()):
 *     .totalram  — total usable RAM pages
 *     .freeram   — free RAM pages
 *     .bufferram — pages used for buffer cache
 *     .sharedram — shared memory pages
 *     .mem_unit  — size of each unit in bytes (= PAGE_SIZE on all archs)
 *
 *   All values are in pages; multiply by mem_unit / 1024 to get KiB.
 * ──────────────────────────────────────────────────────────────────────── */
static int sysinfo_show(struct seq_file *m, void *v)
{
    struct sysinfo si;
    unsigned long total_mb, free_mb, used_mb;

    /* ── 1. populate sysinfo ── */
    si_meminfo(&si);

    /* pages → MiB: (pages * page_size) / (1024 * 1024) */
    total_mb = (si.totalram * si.mem_unit) >> 20;
    free_mb  = (si.freeram  * si.mem_unit) >> 20;
    used_mb  = total_mb - free_mb;

    /* ── 2. header ── */
    seq_puts(m, "==============================================\n");
    seq_puts(m, "  /proc/sysinfo_plus — kernel system info\n");
    seq_puts(m, "==============================================\n\n");

    /* ── 3. CPU ──
     * num_online_cpus() counts CPUs currently online (present + not hotplug-
     * removed). num_possible_cpus() gives the hardware maximum.
     * nr_cpu_ids is the upper bound of the cpumask bitmap. */
    seq_printf(m, "CPU cores       : %d online  (%d possible)\n",
               num_online_cpus(), num_possible_cpus());

    /* ── 4. Kernel version ──
     * utsname() returns a pointer to the current UTS namespace's
     * new_utsname struct. The .release field is the same string that
     * 'uname -r' prints — set at build time from the Makefile. */
    seq_printf(m, "Kernel version  : %s\n", utsname()->release);

    /* ── 5. RAM ── */
    seq_printf(m, "Total RAM       : %lu MiB\n", total_mb);
    seq_printf(m, "Free RAM        : %lu MiB\n", free_mb);
    seq_printf(m, "Used RAM        : %lu MiB  (%.1lu%%)\n",
               used_mb,
               total_mb ? (used_mb * 100) / total_mb : 0);
    seq_printf(m, "Buffer cache    : %lu MiB\n",
               (si.bufferram * si.mem_unit) >> 20);

    seq_puts(m, "\n");

    /* ── 6. Uptime ── */
    format_uptime(m);

    /* ── 7. Load average ── */
    format_loadavg(m);

    seq_puts(m, "\n");
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * seq_file plumbing
 * ──────────────────────────────────────────────────────────────────────── */
static int sysinfo_open(struct inode *inode, struct file *file)
{
    return single_open(file, sysinfo_show, NULL);
}

/* proc_ops replaces file_operations for /proc entries since kernel 5.6 */
static const struct proc_ops sysinfo_fops = {
    .proc_open    = sysinfo_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/* ─────────────────────────────────────────────────────────────────────────
 * Module lifecycle
 * ──────────────────────────────────────────────────────────────────────── */
static struct proc_dir_entry *proc_entry;

static int __init sysinfo_init(void)
{
    proc_entry = proc_create(PROC_ENTRY, 0444, NULL, &sysinfo_fops);
    if (!proc_entry) {
        pr_err("sysinfo_plus: failed to create /proc/%s\n", PROC_ENTRY);
        return -ENOMEM;
    }
    pr_info("sysinfo_plus: /proc/%s created\n", PROC_ENTRY);
    return 0;
}

static void __exit sysinfo_exit(void)
{
    proc_remove(proc_entry);
    pr_info("sysinfo_plus: /proc/%s removed\n", PROC_ENTRY);
}

module_init(sysinfo_init);
module_exit(sysinfo_exit);
