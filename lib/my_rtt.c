/* include rtt1 */
#include    "my_unprtt.h"

int rtt_d_flag = 0;        /* debug flag; can be set by caller */

/*
 * Calculate the RTO value based on current estimators:
 *        smoothed RTT plus four times the deviation
 */
#define RTT_RTOCALC(ptr) ((ptr)->rtt_srtt + (4 * (ptr)->rtt_rttvar))

static float rtt_minmax(long rto)
{
    if (rto < RTT_RXTMIN)
        rto = RTT_RXTMIN;
    else if (rto > RTT_RXTMAX)
        rto = RTT_RXTMAX;
    return(rto);
}

void rtt_init(struct rtt_info *ptr)
{
    struct timeval  tv;

    Gettimeofday(&tv, NULL);
    ptr->rtt_base   = (uint64_t)tv.tv_sec *1000000.0 + tv.tv_usec;
    ptr->rtt_rtt    = 0;
    ptr->rtt_srtt   = 0;
    ptr->rtt_rttvar = 750000;
    ptr->rtt_rto    = rtt_minmax(RTT_RTOCALC(ptr));
    /* first RTO at (srtt + (4 * rttvar)) = 3 seconds */
}
/* end rtt1 */

/*
 * Return the current timestamp.
 * Our timestamps are 32-bit integers that count milliseconds since
 * rtt_init() was called.
 */

/* include rtt_ts */
uint32_t rtt_ts(struct rtt_info *ptr)
{
    uint32_t        ts;
    struct timeval  tv;

    gettimeofday(&tv, NULL);
    ts = ((tv.tv_sec) * 1000000) + (tv.tv_usec) - ptr->rtt_base;
    return(ts);
}

void rtt_newpack(struct rtt_info *ptr)
{
    ptr->rtt_nrexmt = 0;
}


int rtt_start(struct rtt_info *ptr)
{
    return ptr->rtt_rto / 1000;
}
/* end rtt_ts */

/*
 * A response was received.
 * Stop the timer and update the appropriate values in the structure
 * based on this packet's RTT.  We calculate the RTT, then update the
 * estimators of the RTT and its mean deviation.
 * This function should be called right after turning off the
 * timer with alarm(0), or right after a timeout occurs.
 */

/* include rtt_stop */
void rtt_stop(struct rtt_info *ptr, uint32_t ms)
{
    long delta;

    ptr->rtt_rtt = ms;                  /* measured RTT in micro-seconds */

    /*
     * Update our estimators of RTT and mean deviation of RTT.
     * See Jacobson's SIGCOMM '88 paper, Appendix A, for the details.
     * We use floating point here for simplicity.
     */

    delta = ptr->rtt_rtt - ptr->rtt_srtt;
    ptr->rtt_srtt += delta >> 3;        /* g = 1/8 */

    if (delta < 0)
        delta = -delta;                 /* |delta| */

    ptr->rtt_rttvar += (delta - ptr->rtt_rttvar) >> 2;    /* h = 1/4 */

    ptr->rtt_rto = rtt_minmax(RTT_RTOCALC(ptr));
}
/* end rtt_stop */

/*
 * A timeout has occurred.
 * Return -1 if it's time to give up, else return 0.
 */

/* include rtt_timeout */
int rtt_timeout(struct rtt_info *ptr)
{
    ptr->rtt_rto *= 2;                  /* next RTO */

    ptr->rtt_rto = rtt_minmax(ptr->rtt_rto);

    if (++ptr->rtt_nrexmt > RTT_MAXNREXMT)
        return(-1);                     /* time to give up for this packet */
    return(0);
}
/* end rtt_timeout */

/*
 * Print debugging information on stderr, if the "rtt_d_flag" is nonzero.
 */
void rtt_debug(struct rtt_info *ptr)
{
    if (rtt_d_flag == 0)
        return;

    fprintf(stdout, "rtt = %ld , srtt = %ld, rttvar = %ld, rto = %ldus\n",
            ptr->rtt_rtt, ptr->rtt_srtt, ptr->rtt_rttvar, ptr->rtt_rto);
    fflush(stdout);
}
