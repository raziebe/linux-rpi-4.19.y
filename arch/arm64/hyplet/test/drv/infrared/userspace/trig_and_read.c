#define _GNU_SOURCE
 
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
 #include <fcntl.h>

const char *gpio475="/sys/class/gpio/gpio475/value";
const char *gpio485="/sys/class/gpio/gpio485/value";

#include "utils.h"

int trig_off(void)
{
	int b;
	int fd;

	fd = open(gpio485, O_WRONLY);
	if (fd < 0){
		perror("Failed to open gpio485 file");
		return -1;
	}
	
	b = write(fd,"0\n", 2);
	if (b < 0){
		perror("write:");
		return -1;
	}
	close(fd);
	return 0;

}

void wait_for_blackness(void)
{
	int b;
	int fd;
	char buf[32];
wait:
	fd = open(gpio475, O_RDONLY);
	if (fd < 0){
		perror("Failed to open gpio475 file");
		return ;
	}

	b = read(fd, buf, sizeof(buf));
	if (b < 0){
		perror("read:");
		close(fd);
		goto wait;
	}

	if (buf[0] == '1') {
		close(fd);
		goto wait;
	}
	close(fd);
}

int main(int argc,char *argv[])
{
	int i;
	int count=10;
	int fd475,fd485;
	int bytes;
	char buf[32];
	long t1, t2;
	
/*
	system("echo 475 > /sys/class/gpio/export");
	system("echo in > /sys/class/gpio/gpio475/direction");
	system("echo 485 > /sys/class/gpio/export");
	system("echo out > /sys/class/gpio/gpio485/direction");
*/
trig:
	trig_off();
	wait_for_blackness();
	fd485 = open(gpio485, O_WRONLY);
	if (fd485 < 0){
		perror("Failed to open gpio485 file");
		return 0;
	}
	t1 = cycles_ns();
	bytes = write(fd485, "1\n",2);
	if (bytes < 0){
		perror("write:");
		return 0;
	}
	close(fd485);

	for (i  = 0; i < 100 ; i++) {

		fd475 = open(gpio475, O_RDONLY);
		if (fd475 < 0){
			perror("Failed to open gpio475 file");
			return 0;
		}

		bytes = read(fd475, buf, sizeof(buf));
		if (bytes < 0){
			perror("read:");
			return 0;
		}

		t2 = cycles_ns();
		if (buf[0] == '1') {
			printf("dt=%ld\n",(t2 - t1)/1000);
			break;
		}
		close(fd475);
	}
	count--;
	sleep(5);
	if (count >0)
		goto trig;
}
