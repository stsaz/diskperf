diskperf is a small program for testing disks.


## Build

	git clone --depth=1 https://github.com/stsaz/ffbase
	git clone --depth=1 https://github.com/stsaz/ffos
	git clone --depth=1 https://github.com/stsaz/diskperf
	cd diskperf
	make -j8


## Example

	sudo ./diskperf /dev/sda

	abs time:                           9999 msec
	abs data read:                   2732928 KB
	op speed:                           4270 op/sec
	read speed:                       273320 KB/sec
	max read time:                     37303 usec
	avg read time:                       233 usec
	min read time:                       182 usec
	avg io_submit delay:                  26 usec
