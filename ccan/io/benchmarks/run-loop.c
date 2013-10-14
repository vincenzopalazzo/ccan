#include <ccan/io/io.h>
#include <ccan/time/time.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <err.h>
#include <signal.h>

#define NUM 500
#define NUM_ITERS 10000

struct buffer {
	int iters;
	struct io_conn *reader, *writer;
	char buf[32];
};

static struct io_plan *poke_writer(struct io_conn *conn, struct buffer *buf);
static struct io_plan *poke_reader(struct io_conn *conn, struct buffer *buf);

static struct io_plan *do_read(struct io_conn *conn, struct buffer *buf)
{
	assert(conn == buf->reader);

	return io_read(&buf->buf, sizeof(buf->buf),
		       io_next(conn, poke_writer, buf));
}

static struct io_plan *do_write(struct io_conn *conn, struct buffer *buf)
{
	assert(conn == buf->writer);

	return io_write(&buf->buf, sizeof(buf->buf),
			io_next(conn, poke_reader, buf));
}

static struct io_plan *poke_writer(struct io_conn *conn, struct buffer *buf)
{
	assert(conn == buf->reader);

	if (buf->iters == NUM_ITERS)
		return io_close(conn, NULL);

	/* You write. */
	io_wake(buf->writer, do_write, buf);

	/* I'll wait until you wake me. */
	return io_idle(conn);
}

static struct io_plan *poke_reader(struct io_conn *conn, struct buffer *buf)
{
	assert(conn == buf->writer);
	/* You read. */
	io_wake(buf->reader, do_read, buf);

	if (++buf->iters == NUM_ITERS)
		return io_close(conn, NULL);

	/* I'll wait until you tell me to write. */
	return io_idle(conn);
}

static struct io_plan *reader(struct io_conn *conn, struct buffer *buf)
{
	assert(conn == buf->reader);

	/* Wait for writer to tell us to read. */
	return io_idle(conn);
}

int main(void)
{
	unsigned int i;
	int fds[2], last_read, last_write;
	struct timespec start, end;
	struct buffer buf[NUM];

	if (pipe(fds) != 0)
		err(1, "pipe");
	last_read = fds[0];
	last_write = fds[1];

	for (i = 1; i < NUM; i++) {
		buf[i].iters = 0;
		if (pipe(fds) < 0)
			err(1, "pipe");
		memset(buf[i].buf, i, sizeof(buf[i].buf));
		sprintf(buf[i].buf, "%i-%i", i, i);

		buf[i].reader = io_new_conn(last_read, reader, NULL, &buf[i]);
		if (!buf[i].reader)
			err(1, "Creating reader %i", i);
		buf[i].writer = io_new_conn(fds[1], do_write, NULL, &buf[i]);
		if (!buf[i].writer)
			err(1, "Creating writer %i", i);
		last_read = fds[0];
	}

	/* Last one completes the cirle. */
	i = 0;
	buf[i].iters = 0;
	sprintf(buf[i].buf, "%i-%i", i, i);
	buf[i].reader = io_new_conn(last_read, reader, NULL, &buf[i]);
	if (!buf[i].reader)
		err(1, "Creating reader %i", i);
	buf[i].writer = io_new_conn(last_write, do_write, NULL, &buf[i]);
	if (!buf[i].writer)
		err(1, "Creating writer %i", i);

	/* They should eventually exit */
	start = time_now();
	if (io_loop() != NULL)
		errx(1, "io_loop?");
	end = time_now();

	for (i = 0; i < NUM; i++) {
		char b[sizeof(buf[0].buf)];
		memset(b, i, sizeof(b));
		sprintf(b, "%i-%i", i, i);
		if (memcmp(b, buf[(i + NUM_ITERS) % NUM].buf, sizeof(b)) != 0)
			errx(1, "Buffer for %i was '%s' not '%s'",
			     i, buf[(i + NUM_ITERS) % NUM].buf, b);
	}

	printf("run-many: %u %u iterations: %llu usec\n",
	       NUM, NUM_ITERS, (long long)time_to_usec(time_sub(end, start)));
	return 0;
}
