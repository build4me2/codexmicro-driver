/**************************************************************
* Class::  CSC-415-01 Summer 2026
* Name:: Manisha Chand
* Student ID:: 924844476
* GitHub-Name:: build4me2
* Project:: Assignment 6 – Device Driver
*
* File:: codexmicro.c
*
* Description:: Loadable Linux character device driver that simulates the
* OpenAI Codex Micro agent-control macro pad as a virtual, LLM-agnostic
* device. It claims a character-device identity and publishes /dev/codexmicro,
* accepts per-agent status updates by writing and reports the whole device
* picture by reading, and exposes a control interface (reasoning dial, mode,
* key presses, key remapping, reset) over ioctl. A key press is funnelled
* through a single seam so the resulting keystroke can later be delivered to a
* real terminal either from user space or directly from the kernel.
*
**************************************************************/

#include <linux/module.h>   /* lets this code be built as a loadable module     */
#include <linux/init.h>     /* markers for the load/unload entry points         */
#include <linux/kernel.h>   /* kernel logging facility                          */
#include <linux/fs.h>       /* character-device identity + operations table     */
#include <linux/cdev.h>     /* the object that binds our identity to our ops    */
#include <linux/device.h>   /* lets the OS auto-materialize the /dev entry      */
#include <linux/version.h>  /* used to stay compatible across kernel releases   */
#include <linux/uaccess.h>  /* safe copying of data across the user boundary    */
#include <linux/spinlock.h> /* serializes access to shared device state         */
#include <linux/string.h>   /* word matching when parsing incoming messages     */
#include <linux/kfifo.h>    /* the queue that holds pressed keys until drained  */
#include <linux/input-event-codes.h> /* KEY_* codes used as key defaults        */

#include "codexmicro.h"     /* the control interface shared with user programs  */

/*
 * A single, stable name is reused for the device identity, the /dev entry, and
 * the log prefix. Keeping it in one place means a future rename cannot leave
 * one of those three out of step with the others.
 */
#define DEVICE_NAME "codexmicro"
#define CLASS_NAME  "codexmicro"

/*
 * These handles are remembered at file scope because unloading must release
 * *exactly* the resources that loading acquired. If they were local to the
 * load routine, the unload routine would have nothing to hand back, and the
 * kernel would keep leaking them until the machine reboots.
 */
static dev_t          codex_devno;   /* the (major,minor) identity we are granted */
static struct cdev    codex_cdev;    /* connects that identity to our operations  */
static struct class  *codex_class;   /* the category that triggers /dev creation  */
static struct device *codex_device;  /* the concrete /dev entry itself            */

/*
 * The device presents itself as a small fleet of coding agents whose live
 * status the user watches. A fixed ceiling is used instead of dynamic growth
 * because the count is tiny and bounded by the real hardware's six status
 * lights; a fixed array also removes any need to allocate or free memory while
 * a call is in flight, which makes leaks impossible and the logic easier to
 * reason about.
 */
#define MAX_AGENTS 6

/*
 * The life of one agent as the user perceives it, ordered from "no work"
 * through active work to the two terminal outcomes so the set reads as a
 * progression. Callers depend on the names, never the underlying numbers.
 */
enum agent_status {
	AGENT_IDLE,
	AGENT_THINKING,
	AGENT_NEEDS_INPUT,
	AGENT_DONE,
	AGENT_ERROR,
};

/* Depth of the queue that holds pressed keys until a user program drains them.
 * A handful of presses may pile up between drains; the value is a power of two
 * because the queue implementation requires it, and small because presses are
 * consumed almost immediately. */
#define KEY_QUEUE_DEPTH 16

/*
 * All mutable device state lives in one structure behind one lock. A single
 * lock is sufficient and preferred here because every operation touches only a
 * few fields briefly, so finer-grained locking would add complexity without
 * relieving any real contention. More than one program may reach the device at
 * once (for example a status feeder and a monitor), and the lock is what keeps
 * a reader from ever observing a half-applied update.
 */
struct codex_state {
	int dial;                            /* reasoning effort, 0..100          */
	enum codex_mode mode;                /* overall control-surface posture   */
	enum agent_status agent[MAX_AGENTS]; /* latest status reported per agent  */
	int last_key;                        /* most recent key, for the log/read */
	int keymap[CODEX_KEY_COUNT];         /* keystroke each key slot emits     */
	DECLARE_KFIFO(keys, int, KEY_QUEUE_DEPTH); /* pressed keys awaiting drain */
	spinlock_t lock;                     /* serializes access to the above    */
};

static struct codex_state state;

/*
 * Turning stored codes back into words keeps the human-readable format in one
 * place, so the text a reader sees stays stable even if the internal numbering
 * is ever reordered. An unrecognized value falls back to the resting state
 * rather than exposing a blank, so the snapshot is always well-formed.
 */
static const char *status_name(enum agent_status s)
{
	switch (s) {
	case AGENT_THINKING:    return "THINKING";
	case AGENT_NEEDS_INPUT: return "NEEDS_INPUT";
	case AGENT_DONE:        return "DONE";
	case AGENT_ERROR:       return "ERROR";
	default:                return "IDLE";
	}
}

static const char *mode_name(enum codex_mode m)
{
	switch (m) {
	case CODEX_MODE_STEERING: return "STEERING";
	case CODEX_MODE_REVIEW:   return "REVIEW";
	default:                  return "IDLE";
	}
}

/*
 * Accepts only the agreed status words and rejects anything else, so a client's
 * typo surfaces as a clear error instead of silently corrupting state. Reports
 * success and delivers the matched code through the out-parameter.
 */
static bool parse_status(const char *word, enum agent_status *out)
{
	if      (!strcmp(word, "THINKING"))    *out = AGENT_THINKING;
	else if (!strcmp(word, "NEEDS_INPUT")) *out = AGENT_NEEDS_INPUT;
	else if (!strcmp(word, "DONE"))        *out = AGENT_DONE;
	else if (!strcmp(word, "ERROR"))       *out = AGENT_ERROR;
	else if (!strcmp(word, "IDLE"))        *out = AGENT_IDLE;
	else return false;
	return true;
}

/*
 * load_defaults - put every field into its documented resting posture: a
 * middling reasoning level, idle mode, all agents idle, no key seen yet, and
 * each key slot mapped to a sensible default keystroke. Kept in one place so
 * both first-time setup and an explicit reset produce an identical, known
 * state. The caller is responsible for any locking, because the two callers
 * have different needs: setup runs before the device is reachable, while reset
 * runs while it is live.
 */
static void load_defaults(void)
{
	int i;

	state.dial = 50;
	state.mode = CODEX_MODE_IDLE;
	state.last_key = -1;
	for (i = 0; i < MAX_AGENTS; i++)
		state.agent[i] = AGENT_IDLE;

	/*
	 * The defaults pair each command with the keystroke a terminal user would
	 * expect it to stand in for: confirm with Enter, dismiss with Escape, and
	 * mnemonic letters for the rest. Any of these can be repointed later.
	 */
	state.keymap[CODEX_KEY_ACCEPT]    = KEY_ENTER;
	state.keymap[CODEX_KEY_REJECT]    = KEY_ESC;
	state.keymap[CODEX_KEY_PUSH_TALK] = KEY_T;
	state.keymap[CODEX_KEY_NEW_CHAT]  = KEY_N;
	state.keymap[CODEX_KEY_CUSTOM]    = KEY_C;
}

/*
 * emit_key - THE SEAM. Every key press is funnelled through this one function
 * so there is a single place that decides what "pressing a key" physically
 * means. Today it resolves the slot to its keystroke and queues that keystroke
 * for a user-space helper to inject into the focused terminal; a later phase
 * can change only this body to report the keystroke straight to the kernel
 * input layer instead, with nothing else in the driver affected.
 *
 * Returns 0 once the press is recorded, or -EINVAL if the slot names no key.
 */
static int emit_key(int slot)
{
	int keycode;

	if (slot < 0 || slot >= CODEX_KEY_COUNT)
		return -EINVAL;

	spin_lock(&state.lock);
	keycode = state.keymap[slot];
	/*
	 * The resolved keystroke is enqueued rather than acted on directly so the
	 * decision of how to deliver it lives entirely outside this locked region
	 * and outside the kernel for now. If the queue is full the newest press is
	 * dropped, which is acceptable because presses are consumed almost at once
	 * and losing one is preferable to blocking a caller inside the lock.
	 */
	kfifo_in(&state.keys, &keycode, 1);
	state.last_key = slot;
	spin_unlock(&state.lock);

	printk(KERN_INFO "codexmicro: key slot %d pressed, keystroke %d queued\n",
	       slot, keycode);
	return 0;
}

/*
 * dev_open / dev_release - a user program attaching to and detaching from the
 * device. No per-connection resources are required, so these only record the
 * event: the log is the simplest evidence during the demo that a program
 * actually reached the driver, which the assignment asks to be shown.
 */
static int dev_open(struct inode *inode, struct file *file)
{
	printk(KERN_INFO "codexmicro: opened\n");
	return 0;
}

static int dev_release(struct inode *inode, struct file *file)
{
	printk(KERN_INFO "codexmicro: closed\n");
	return 0;
}

/* Bound on the assembled snapshot; comfortably fits the dial line plus one
 * line per agent, so the text is never truncated in practice. */
#define SNAPSHOT_MAX 256

/*
 * dev_read - hand the caller a text picture of the whole device: the dial, the
 * mode, the last key, and every agent's status. This is the "reading" side of
 * the device and the required use of copy_to_user.
 *
 * Returns the number of bytes delivered, 0 once the whole picture has been
 * read, or a negative error code.
 */
static ssize_t dev_read(struct file *file, char __user *ubuf,
			size_t len, loff_t *ppos)
{
	char snapshot[SNAPSHOT_MAX];
	int written;
	int i;

	/*
	 * The picture is assembled in kernel memory while the lock is held so it
	 * captures one consistent instant, then handed to the caller only after the
	 * lock is released. Delivering bytes to user memory may pause the calling
	 * thread, and a lock must never be held across anything that can pause.
	 */
	spin_lock(&state.lock);
	written = scnprintf(snapshot, sizeof(snapshot),
			    "dial=%d mode=%s last_key=%d\n",
			    state.dial, mode_name(state.mode), state.last_key);
	for (i = 0; i < MAX_AGENTS; i++)
		written += scnprintf(snapshot + written, sizeof(snapshot) - written,
				     "agent[%d]=%s\n", i, status_name(state.agent[i]));
	spin_unlock(&state.lock);

	/*
	 * A reader such as `cat` calls again and again until told nothing remains.
	 * The running position lets the full picture be delivered once and then
	 * reported as finished on the next call, which is what stops the reader
	 * from looping forever over the same text.
	 */
	if (*ppos >= written)
		return 0;
	if (len > written - *ppos)
		len = written - *ppos;
	if (copy_to_user(ubuf, snapshot + *ppos, len))
		return -EFAULT;
	*ppos += len;
	return len;
}

/* Bound on an incoming message; a single status report is short, so anything
 * longer is a mistake and is refused rather than trusted. */
#define WRITE_MAX 64

/*
 * dev_write - accept a status report of the shape "<agent-id>:<STATUS>" and
 * record it. This is the "writing" side of the device and the required use of
 * copy_from_user: it is how an external agent tells the device what is
 * happening so a later read reflects it.
 *
 * Returns the count the caller offered on success, or a negative error code.
 */
static ssize_t dev_write(struct file *file, const char __user *ubuf,
			 size_t len, loff_t *ppos)
{
	char kbuf[WRITE_MAX];
	size_t offered = len;           /* what the caller asked to write        */
	char word[16];
	int id;
	enum agent_status s;

	/*
	 * The message is copied into a small fixed buffer whose size caps the
	 * amount trusted from user space; an over-long write is clipped instead of
	 * being allowed to overrun the buffer. A terminator is planted so the text
	 * can be scanned as a string.
	 */
	if (len == 0)
		return 0;
	if (len >= sizeof(kbuf))
		len = sizeof(kbuf) - 1;
	if (copy_from_user(kbuf, ubuf, len))
		return -EFAULT;
	kbuf[len] = '\0';

	/*
	 * Both halves of the message are validated before any state changes: the
	 * id must name a real agent and the word must be a known status. Rejecting
	 * a malformed message up front guarantees a bad write can never leave the
	 * device partially updated.
	 */
	if (sscanf(kbuf, "%d:%15s", &id, word) != 2)
		return -EINVAL;
	if (id < 0 || id >= MAX_AGENTS)
		return -EINVAL;
	if (!parse_status(word, &s))
		return -EINVAL;

	spin_lock(&state.lock);
	state.agent[id] = s;
	spin_unlock(&state.lock);

	/*
	 * The whole offered length is reported as consumed so a writer such as
	 * `echo` considers its line fully delivered and does not re-send the tail
	 * of a message that was intentionally clipped above.
	 */
	return offered;
}

/*
 * dev_ioctl - the control channel for everything that is not a stream of bytes:
 * adjusting the reasoning dial, switching mode, pressing and remapping keys,
 * draining a queued keystroke, and resetting. Each request either carries a
 * value in from the caller or hands one back, so every case copies across the
 * user boundary in the matching direction and refuses malformed input before
 * touching state. An unknown request is rejected so a caller learns at once
 * that it asked for something this device does not offer.
 *
 * Returns 0 on success or a negative error code.
 */
static long dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *uarg = (void __user *)arg;
	struct codex_remap remap;
	int value;
	int key;

	switch (cmd) {
	case CODEX_SET_DIAL:
		if (copy_from_user(&value, uarg, sizeof(value)))
			return -EFAULT;
		/* The dial is a percentage of effort; a value outside 0..100 is
		 * meaningless and is refused rather than clamped so the caller
		 * learns its request was wrong. */
		if (value < 0 || value > 100)
			return -EINVAL;
		spin_lock(&state.lock);
		state.dial = value;
		spin_unlock(&state.lock);
		break;

	case CODEX_GET_DIAL:
		spin_lock(&state.lock);
		value = state.dial;
		spin_unlock(&state.lock);
		if (copy_to_user(uarg, &value, sizeof(value)))
			return -EFAULT;
		break;

	case CODEX_SET_MODE:
		if (copy_from_user(&value, uarg, sizeof(value)))
			return -EFAULT;
		/* Only the defined postures are accepted; anything else would leave
		 * the mode naming a state that does not exist. */
		if (value < CODEX_MODE_IDLE || value > CODEX_MODE_REVIEW)
			return -EINVAL;
		spin_lock(&state.lock);
		state.mode = value;
		spin_unlock(&state.lock);
		break;

	case CODEX_PRESS_KEY:
		if (copy_from_user(&value, uarg, sizeof(value)))
			return -EFAULT;
		/* All press handling, including validating the slot, lives in the
		 * one seam so there is a single definition of what a press does. */
		return emit_key(value);

	case CODEX_REMAP_KEY:
		if (copy_from_user(&remap, uarg, sizeof(remap)))
			return -EFAULT;
		if (remap.slot < 0 || remap.slot >= CODEX_KEY_COUNT)
			return -EINVAL;
		spin_lock(&state.lock);
		state.keymap[remap.slot] = remap.keycode;
		spin_unlock(&state.lock);
		break;

	case CODEX_GET_KEY:
		/* Hand back the next queued keystroke, or a sentinel when the queue
		 * is empty, so a poller can tell "nothing pending" from a real key
		 * without a separate call. */
		spin_lock(&state.lock);
		if (!kfifo_out(&state.keys, &key, 1))
			key = -1;
		spin_unlock(&state.lock);
		if (copy_to_user(uarg, &key, sizeof(key)))
			return -EFAULT;
		break;

	case CODEX_RESET:
		/* Restoring defaults and emptying the key queue together return the
		 * device to exactly the state it had at load time. */
		spin_lock(&state.lock);
		load_defaults();
		kfifo_reset(&state.keys);
		spin_unlock(&state.lock);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * The kernel refuses to register a character device without an operations
 * table. Declaring ownership lets the kernel block unloading while a program
 * still holds the device open, which prevents this code from vanishing out
 * from under an in-flight call. Each entry names the routine that services one
 * kind of file operation on /dev/codexmicro.
 */
static const struct file_operations codex_fops = {
	.owner          = THIS_MODULE,
	.open           = dev_open,
	.release        = dev_release,
	.read           = dev_read,
	.write          = dev_write,
	.unlocked_ioctl = dev_ioctl,
};

/*
 * codex_init - runs once when the module is loaded.
 *
 * Brings the virtual device into existence in the order each step depends on
 * the previous one, and returns 0 on success or a negative error code on
 * failure. On any failure it unwinds only the steps that already succeeded,
 * so a half-built device never lingers in the kernel.
 */
static int __init codex_init(void)
{
	int result;

	/*
	 * The state is brought to a known resting posture before the device is
	 * reachable: the lock and the key queue are made ready, then every field
	 * is set to its default. Doing this before the /dev entry exists guarantees
	 * no program can ever observe uninitialized state, so no locking is needed
	 * here yet.
	 */
	spin_lock_init(&state.lock);
	INIT_KFIFO(state.keys);
	load_defaults();

	/*
	 * A character device needs a unique identity before it can own anything
	 * else. The identity is requested dynamically rather than hard-coded so
	 * it can never collide with a number another driver already occupies.
	 */
	result = alloc_chrdev_region(&codex_devno, 0, 1, DEVICE_NAME);
	if (result < 0) {
		printk(KERN_ERR "codexmicro: could not obtain a device identity\n");
		return result;
	}

	/*
	 * Bind our operations table to the granted identity and announce it to
	 * the kernel. After this point the kernel may route file operations aimed
	 * at our identity into this module, so it is done only once the identity
	 * is secured.
	 */
	cdev_init(&codex_cdev, &codex_fops);
	codex_cdev.owner = THIS_MODULE;
	result = cdev_add(&codex_cdev, codex_devno, 1);
	if (result < 0) {
		printk(KERN_ERR "codexmicro: could not register the device\n");
		goto undo_region;
	}

	/*
	 * A device class is the prerequisite that lets the OS's hotplug machinery
	 * auto-create the /dev entry, sparing the user from making the node by
	 * hand. The call's signature was simplified in a later kernel release, so
	 * the form is chosen by version to keep the same source building on both.
	 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	codex_class = class_create(CLASS_NAME);
#else
	codex_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
	if (IS_ERR(codex_class)) {
		printk(KERN_ERR "codexmicro: could not create the device class\n");
		result = PTR_ERR(codex_class);
		goto undo_cdev;
	}

	/*
	 * This is the step that actually makes /dev/codexmicro appear, giving user
	 * programs a filename to open. It comes last because it depends on both the
	 * identity and the class being in place.
	 */
	codex_device = device_create(codex_class, NULL, codex_devno, NULL, DEVICE_NAME);
	if (IS_ERR(codex_device)) {
		printk(KERN_ERR "codexmicro: could not create the /dev entry\n");
		result = PTR_ERR(codex_device);
		goto undo_class;
	}

	/*
	 * A visible load confirmation is required by the assignment and is the
	 * first thing to check when verifying the module in the kernel log.
	 */
	printk(KERN_INFO "codexmicro: loaded and ready at /dev/%s\n", DEVICE_NAME);
	return 0;

	/*
	 * The failure paths release resources in the reverse of the order they were
	 * acquired: the most recently obtained resource is the safest to drop first
	 * because nothing built later depends on it.
	 */
undo_class:
	class_destroy(codex_class);
undo_cdev:
	cdev_del(&codex_cdev);
undo_region:
	unregister_chrdev_region(codex_devno, 1);
	return result;
}

/*
 * codex_exit - runs once when the module is unloaded.
 *
 * Dismantles the device in the exact reverse order of construction so that no
 * resource is freed while something the kernel still tracks depends on it, and
 * leaves the system exactly as it was before loading.
 */
static void __exit codex_exit(void)
{
	device_destroy(codex_class, codex_devno);
	class_destroy(codex_class);
	cdev_del(&codex_cdev);
	unregister_chrdev_region(codex_devno, 1);

	/*
	 * A visible unload confirmation is required so the grader can see the
	 * module removed itself cleanly rather than being force-killed.
	 */
	printk(KERN_INFO "codexmicro: unloaded\n");
}

/* Register the two routines above as the module's load and unload entry points. */
module_init(codex_init);
module_exit(codex_exit);

/*
 * The license is declared as GPL because some kernel facilities this driver
 * will rely on are only offered to GPL-compatible modules; without it the
 * build is flagged as tainting the kernel and some symbols become unavailable.
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Manisha Chand");
MODULE_DESCRIPTION("Virtual Codex Micro agent-control character device");
MODULE_VERSION("0.3");
