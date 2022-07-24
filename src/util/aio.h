
/*
ffaio_init
ffaio_read_prepare ffaio_write_prepare
ffaio_submit
*/

#include <linux/aio_abi.h>
#include <sys/syscall.h>

static inline int io_setup(unsigned nr_events, aio_context_t *ctx_idp)
{
	return syscall(SYS_io_setup, nr_events, ctx_idp);
}
static inline int io_destroy(aio_context_t ctx_id)
{
	return syscall(SYS_io_destroy, ctx_id);
}
static inline int io_submit(aio_context_t ctx_id, long nr, struct iocb **iocbpp)
{
	return syscall(SYS_io_submit, ctx_id, nr, iocbpp);
}
static inline int io_getevents(aio_context_t ctx_id, long min_nr, long nr, struct io_event *events, struct timespec *timeout)
{
	return syscall(SYS_io_getevents, ctx_id, min_nr, nr, events, timeout);
}

static inline int ffaio_init(aio_context_t *aio, uint workers)
{
	*aio = 0;
	return io_setup(workers, aio);
}

static void _ffaio_op_prepare(struct iocb *acb, fffd ev_fd, void *ev_data, fffd fd, void *buf, ffsize n, ffuint64 off)
{
	ffmem_zero_obj(acb);
	acb->aio_data = (ffsize)ev_data;
	acb->aio_flags = IOCB_FLAG_RESFD;
	acb->aio_resfd = ev_fd;

	acb->aio_fildes = fd;
	acb->aio_buf = (ffsize)buf;
	acb->aio_nbytes = n;
	acb->aio_offset = off;
}

static inline void ffaio_read_prepare(struct iocb *acb, fffd ev_fd, void *ev_data, fffd fd, void *buf, ffsize n, ffuint64 off)
{
	_ffaio_op_prepare(acb, ev_fd, ev_data, fd, buf, n, off);
	acb->aio_lio_opcode = IOCB_CMD_PREAD;
}

static inline void ffaio_write_prepare(struct iocb *acb, fffd ev_fd, void *ev_data, fffd fd, void *buf, ffsize n, ffuint64 off)
{
	_ffaio_op_prepare(acb, ev_fd, ev_data, fd, buf, n, off);
	acb->aio_lio_opcode = IOCB_CMD_PWRITE;
}

static inline int ffaio_submit(aio_context_t aio, struct iocb **cbs, ffsize n)
{
	return io_submit(aio, n, cbs);
}
