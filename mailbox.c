#include "mailbox.h"
#include <linux/atomic.h>
#include "pci.h"

#define REG_MBOX_OUT_BASE 0x820
#define REG_MBOX_REPLY_COUNTER 0x108
#define REG_MBOX_REPLY_BASE 0x810

#define BCE_MBOX_TIMEOUT_MS 200

void bce_mailbox_init(struct bce_mailbox *mb, void __iomem *reg_mb)
{
    mb->reg_mb = reg_mb;
    init_completion(&mb->mb_completion);
}

int bce_mailbox_send(struct bce_mailbox *mb, u64 msg, u64* recv)
{
    u32 __iomem *regb;

    if (atomic_cmpxchg(&mb->mb_status, 0, 1) != 0) {
        return -EEXIST; // We don't support two messages at once
    }
    reinit_completion(&mb->mb_completion);

    pr_debug("bce_mailbox_send: %llx", msg);
    regb = (u32*) ((u8*) mb->reg_mb + REG_MBOX_OUT_BASE);
    iowrite32((u32) msg, regb);
    iowrite32((u32) (msg >> 32), regb + 1);
    iowrite32(0, regb + 2);
    iowrite32(0, regb + 3);

    wait_for_completion_timeout(&mb->mb_completion, msecs_to_jiffies(BCE_MBOX_TIMEOUT_MS));
    if (atomic_read(&mb->mb_status) != 2) { // Didn't get the reply
        atomic_set(&mb->mb_status, 0);
        return -ETIMEDOUT;
    }

    *recv = mb->mb_result;
    pr_debug("bce_mailbox_send: reply %llx", *recv);

    atomic_set(&mb->mb_status, 0);
    return 0;
}

static int bce_mailbox_retrive_response(struct bce_mailbox *mb)
{
    u32 __iomem *regb;
    u32 lo, hi;
    int count, counter;
    u32 res = ioread32((u8*) mb->reg_mb + REG_MBOX_REPLY_COUNTER);
    count = (res >> 20) & 0xf;
    counter = count;
    pr_debug("bce_mailbox_retrive_response count=%i", count);
    while (counter--) {
        regb = (u32*) ((u8*) mb->reg_mb + REG_MBOX_REPLY_BASE);
        lo = ioread32(regb);
        hi = ioread32(regb + 1);
        ioread32(regb + 2);
        ioread32(regb + 3);
        pr_debug("bce_mailbox_retrive_response %llx", ((u64) hi << 32) | lo);
        mb->mb_result = ((u64) hi << 32) | lo;
    }
    return count > 0 ? 0 : -ENODATA;
}

int bce_mailbox_handle_interrupt(struct bce_mailbox *mb)
{
    int status = bce_mailbox_retrive_response(mb);
    if (!status) {
        atomic_set(&mb->mb_status, 2);
        complete(&mb->mb_completion);
    }
    return status;
}