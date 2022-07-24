/* diskperf: configuration
2022, Simon Zolin */

#include <util/cmdarg-scheme.h>

#define R_DONE  100
#define R_BADVAL  101

static int conf_infile(ffcmdarg_scheme *cs, struct diskperf *conf, char *s)
{
	ffmem_free(conf->dev_path);
	conf->dev_path = ffsz_dup(s);
	return 0;
}

static int conf_help()
{
	static const char help[] =
"diskperf v" DSPF_VER "\n\
Usage:\n\
 diskperf [OPTIONS] DEV\n\
\n\
OPTIONS:\n\
 -b, --block=N     Block size (=64k)\n\
 -s, --start=N     Starting offset\n\
 -i, --interval=N  Print statistics interval in msec (=500). 0:disable\n\
 -t, --time=N      Auto-stop after, msec\n\
 -h, --help        Show help\n\
";
	ffstdout_write(help, FFS_LEN(help));
	return R_DONE;
}

static const ffcmdarg_arg dspf_cmd_args[] = {
	{ 0, "",	FFCMDARG_TSTRZ | FFCMDARG_FNOTEMPTY, (ffsize)conf_infile },
	{ 'b', "block",	FFCMDARG_TSIZE32, FF_OFF(struct diskperf, block_size) },
	{ 's', "start",	FFCMDARG_TSIZE64, FF_OFF(struct diskperf, rd_blocks) },
	{ 'i', "interval",	FFCMDARG_TINT32, FF_OFF(struct diskperf, timer_interval) },
	{ 't', "time",	FFCMDARG_TINT32, FF_OFF(struct diskperf, timer_stop) },
	{ 'h', "help",	FFCMDARG_TSWITCH, (ffsize)conf_help },
	{}
};

int conf_check(struct diskperf *conf)
{
	if (g->dev_path == NULL) {
		fflog("Please specify device path");
		return 1;
	}
	g->rd_blocks /= g->block_size;
	return 0;
}

int conf_read(struct diskperf *conf, int argc, const char **argv)
{
	conf->block_size = 64*1024;
	conf->timer_interval = 500;

	if (argc < 2) {
		conf_help();
		return 1;
	}

	ffstr errmsg = {};
	int r = ffcmdarg_parse_object(dspf_cmd_args, conf, argv, argc, 0, &errmsg);
	if (r < 0) {
		if (r == -R_DONE)
			return -1;
		else if (r == -R_BADVAL)
			ffstderr_fmt("command line: bad value\n");
		else
			ffstderr_fmt("command line: %S\n", &errmsg);
		return -1;
	}

	if (0 != conf_check(conf))
		return -1;

	return 0;
}
