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

const char *gpio_trig="/sys/class/gpio/gpio475/value"; // the trigger
const char *gpio_echo="/sys/class/gpio/gpio484/value"; // the echo

static inline long cycles_us(void)
{
	struct timeval t;

	gettimeofday(&t, NULL);
	return t.tv_sec * 1000000 + t.tv_usec;
}

int trig(char *onoff)
{
	int b;
	int fd;

	fd = open(gpio_trig, O_WRONLY);
	if (fd < 0){
		perror("Failed to open gpio485 file");
		return -1;
	}
	
	b = write(fd, onoff, 3);
	if (b < 0){
		perror("write:");
		return -1;
	}
	close(fd);
	return 0;

}

long wait_echo(char c)
{
	long t = 0;
        int b;
        int fd;
        char buf[32];
wait:
        fd = open(gpio_echo, O_RDONLY);
        if (fd < 0){
                perror("Failed to open gpio475 file");
                return ;
        }

        b = read(fd, buf, sizeof(buf));
        if (b <= 0){
                perror("read:");
                close(fd);
                goto wait;
        }
	t = cycles_us();
        close(fd);
        if (buf[0] == c) {
                goto wait;
        }
	return t;
}

int main(int argc,char *argv[])
{
	int i;
	int sleep_us;
	long s, e, dt_us, tmp;
	float supersonic_speed_us = 0.0343;// centimeter/microsecond;	
	float distance;

	if (argc < 2) {
		printf("%s <wait time us>\n",argv[0]);
		return -1;
	}

	sleep_us = atoi(argv[1]);
	trig("1\n");

	// wait trigger
	usleep(sleep_us);

	trig("0\n");
	s =  cycles_us();

	tmp = wait_echo('0');
	if (tmp != 0)
		s = tmp;
	e =  cycles_us();
	tmp  = wait_echo('1');
	if (tmp != 0)
		e = tmp;

	dt_us = (e - s);

	distance  = ((float)dt_us * supersonic_speed_us)/2;
	
	printf("distance %fcm dt=%ld\n",
		distance, dt_us);
}
