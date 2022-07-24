/* diskperf
2022, Simon Zolin */

#include <diskperf.h>
#include <util/aio.h>
#include <FFOS/queue.h>
#include <FFOS/process.h>
#include <FFOS/time.h>
#include <FFOS/std.h>
#include <FFOS/perf.h>
#include <FFOS/file.h>
#include <FFOS/timer.h>
#include <FFOS/ffos-extern.h>
#include <ffbase/atomic.h>
#include <sys/eventfd.h>


#define die(ex)  echo_die(ex, __LINE__)

void echo_die(int ex, int line)
{
	if (!ex)
		return;
	fflog("error on line %L errno=%d %s"
		, line, fferr_last(), fferr_strptr(fferr_last()));
	ffps_exit(1);
}


typedef void (*handler_func)(void *param);
typedef void (*handler_io_func)(void *param, int result);

struct task {
	handler_func handler;
	handler_io_func handler_io;
	fftime tstart, tsubmit;
	struct iocb acb;
	uint64 off;
};


struct tm_stat {
	fftime tm_start;
	uint64 nblocks;
	uint max_usec;
	uint64 avg_usec;
	uint min_usec;
	uint64 avg_submit_delay_usec;
};

struct diskperf {
	char *dev_path;
	char *buf;
	aio_context_t aio;
	uint block_size;
	fffd efd;
	struct task ts_efd;
	ffkq kq;
	fftimer tmr;
	struct task ts_tmr;
	fftimer tmr2;
	struct task ts_tmr2;
	fffd fd;
	uint fin;

	uint timer_interval;
	uint timer_stop;
	uint64 rd_blocks;
	struct tm_stat session;
	struct tm_stat total;
};
struct diskperf *g;

struct diskperf* dspf_create()
{
	struct diskperf *p = ffmem_new(struct diskperf);
	p->kq = FFKQ_NULL;
	p->tmr = FFTIMER_NULL;
	return p;
}

void dspf_destroy(struct diskperf *p)
{
	ffmem_free(p->dev_path);
	ffmem_alignfree(p->buf);
	fffile_close(p->fd);
	if (p->tmr != FFTIMER_NULL)
		fftimer_close(p->tmr, p->kq);
	if (p->kq != FFKQ_NULL)
		ffkq_close(p->kq);
}


void dspf_status(struct diskperf *p);

void on_timer(void *param)
{
	fftimer_consume(g->tmr);
	dspf_status(g);
}
void on_timer_stop(void *param)
{
	fftimer_consume(g->tmr2);
	FFINT_WRITEONCE(g->fin, 1);
}

void task_begin(struct task *t);

void task_on_read(void *param, int result)
{
	struct task *t = param;

	fftime tstop = fftime_monotonic();
	fftime_sub(&tstop, &t->tstart);
	uint tm_usec = fftime_to_usec(&tstop);

	// fflog("read @%U: %d [%uusec]", t->off, result, tm_usec);

	if (tm_usec > g->session.max_usec)
		g->session.max_usec = tm_usec;
	g->session.avg_usec += tm_usec;
	if (tm_usec < g->session.min_usec)
		g->session.min_usec = tm_usec;

	fftime_sub(&t->tsubmit, &t->tstart);
	tm_usec = fftime_to_usec(&t->tsubmit);
	g->session.avg_submit_delay_usec += tm_usec;

	g->session.nblocks++;
	g->rd_blocks++;

	if (result != g->block_size) {
		if (result < 0)
			fflog("ERROR read @%U: %E", t->off, fferr_last());
		FFINT_WRITEONCE(g->fin, 1);
		return;
	}

	task_begin(t);
}

void task_begin(struct task *t)
{
	// fflog("read @%U", g->rd_blocks * g->block_size);
	t->off = g->rd_blocks * g->block_size;
	t->handler_io = task_on_read;
	ffaio_read_prepare(&t->acb, g->efd, t, g->fd, g->buf, g->block_size, t->off);
	t->tstart = fftime_monotonic();

	static struct iocb *cbs[1];
	cbs[0] = &t->acb;
	int r = ffaio_submit(g->aio, cbs, 1);
	die(r != 1);

	t->tsubmit = fftime_monotonic();
}

void efd_signal(void *param)
{
	struct task *t = param;
	uint64 n;
	for (;;) {
		int r = read(g->efd, &n, 8);
		if (r < 0 && errno == EAGAIN)
			break;

		for (;;) {

			struct io_event events[64];
			struct timespec timeout = {};
			r = io_getevents(g->aio, 1, 64, events, &timeout);
			if (r < 0) {
				if (errno == EINTR)
					continue;
				die(1);
			} else if (r == 0) {
				break; // no more events
			}

			for (int i = 0;  i != r;  i++) {
				struct task *t = (void*)(size_t)events[i].data;
				int result = events[i].res;
				if (result < 0) {
					errno = -result;
					result = -1;
				}
				t->handler_io(t, result);
			}
		}
	}
}

void tm_merge(struct tm_stat *d, struct tm_stat *s)
{
	if (s->max_usec > d->max_usec)
		d->max_usec = s->max_usec;
	if (s->min_usec < d->min_usec)
		d->min_usec = s->min_usec;
	d->nblocks += s->nblocks;
	d->avg_usec += s->avg_usec;
	d->avg_submit_delay_usec += s->avg_submit_delay_usec;
}

/** Protect against division by zero. */
#define FFINT_DIVSAFE(val, by) \
	((by) != 0 ? (val) / (by) : 0)

void dspf_status(struct diskperf *p)
{
	fftime now = fftime_monotonic();
	fftime tstop = now;
	fftime_sub(&tstop, &p->total.tm_start);

	tm_merge(&p->total, &p->session);

	struct tm_stat *ts = &p->session;
	if (p->fin) {
		ts = &p->total;
	}

	if (ts->nblocks != 0) {
		ts->avg_usec /= ts->nblocks;
		ts->avg_submit_delay_usec /= ts->nblocks;
	}

	fftime tstop2 = now;
	fftime_sub(&tstop2, &ts->tm_start);

	uint64 ms = fftime_to_msec(&tstop2);

	fflog("\n"
"abs time:           %20U msec\n"
"abs data read:      %20U KB\n"
"op speed:           %20U op/sec\n"
"read speed:         %20U KB/sec\n"
"max read time:      %20u usec\n"
"avg read time:      %20U usec\n"
"min read time:      %20u usec\n"
"avg io_submit delay:%20U usec\n"
		, fftime_to_msec(&tstop)
		, p->total.nblocks * p->block_size / 1024
		, FFINT_DIVSAFE(ts->nblocks * 1000, ms)
		, FFINT_DIVSAFE((ts->nblocks * p->block_size / 1024) * 1000, ms)
		, ts->max_usec
		, ts->avg_usec
		, ts->min_usec
		, ts->avg_submit_delay_usec
		);

	ffmem_zero_obj(&p->session);
	p->session.min_usec = (uint)-1;
	ts->tm_start = now;
}

int dspf_prepare(struct diskperf *p)
{
	p->buf = ffmem_align(p->block_size, 512);
	die(FFKQ_NULL == (p->kq = ffkq_create()));
	die(FFFILE_NULL == (p->fd = fffile_open(p->dev_path, FFFILE_DIRECT | FFFILE_READONLY)));

	if (p->timer_interval != 0) {
		die(FFTIMER_NULL == (p->tmr = fftimer_create(0)));
		p->ts_tmr.handler = on_timer;
		die(0 != fftimer_start(p->tmr, p->kq, &p->ts_tmr, p->timer_interval));
	}
	if (p->timer_stop != 0) {
		die(FFTIMER_NULL == (p->tmr2 = fftimer_create(0)));
		p->ts_tmr2.handler = on_timer_stop;
		die(0 != fftimer_start(p->tmr2, p->kq, &p->ts_tmr2, p->timer_stop));
	}

	die(0 != ffaio_init(&p->aio, 64));

	die(-1 == (p->efd = eventfd(0, EFD_NONBLOCK)));
	p->ts_efd.handler = efd_signal;
	die(0 != ffkq_attach(p->kq, p->efd, &p->ts_efd, FFKQ_READ));
	return 0;
}

void dspf_run(struct diskperf *p)
{
	p->total.min_usec = (uint)-1;
	p->session.min_usec = (uint)-1;
	p->total.tm_start = fftime_monotonic();
	p->session.tm_start = p->total.tm_start;

	struct task ts = {};
	task_begin(&ts);

	ffkq_time t;
	ffkq_time_set(&t, -1);
	while (!FFINT_READONCE(p->fin)) {
		ffkq_event ev;
		int r = ffkq_wait(p->kq, &ev, 1, t);
		if (r < 0 && fferr_last() == EINTR)
			break;
		die(r < 0);

		struct task *t = ffkq_event_data(&ev);
		t->handler(t);
	}

	dspf_status(g);
}

void on_sig(struct ffsig_info *i)
{
	FFINT_WRITEONCE(g->fin, 1);
}

#include <conf.h>

int main(int argc, char **argv)
{
	int ec = 1;

	g = dspf_create();

	if (0 != conf_read(g, argc, (const char**)argv))
		goto exit;

	if (0 != dspf_prepare(g))
		goto exit;

	static const int sigs[] = { SIGINT };
	ffsig_subscribe(on_sig, sigs, 1);

	dspf_run(g);
	ec = 1;

exit:
	dspf_destroy(g);
	return ec;
}
