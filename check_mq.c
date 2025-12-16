#include <stdio.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>

int main() {
    struct mq_attr attr;
    
    // Try to open existing queue and check its attributes
    mqd_t mq = mq_open("/mq_olsr_to_rrc", O_RDONLY);
    if (mq != -1) {
        if (mq_getattr(mq, &attr) == 0) {
            printf("Queue /mq_olsr_to_rrc attributes:\n");
            printf("  mq_maxmsg: %ld\n", attr.mq_maxmsg);
            printf("  mq_msgsize: %ld\n", attr.mq_msgsize);
            printf("  mq_curmsgs: %ld\n", attr.mq_curmsgs);
        }
        mq_close(mq);
    } else {
        printf("Queue /mq_olsr_to_rrc does not exist\n");
    }
    
    return 0;
}
