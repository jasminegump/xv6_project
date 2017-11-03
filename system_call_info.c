// This is a user program that invokes the system call info with int parameters 1,2,3
// Used following reference on to figure out how to print in user programs
// http://recolog.blogspot.com/2016/03/adding-user-program-to-xv6.html

#include "types.h"
#include "stat.h"
#include "user.h"

int
main(void)
{
	int output, i;
	for(i = 1; i < 4; i ++){
		output = info(i);
		if (i == 1)
		{
			printf(1, "Number of processes in system: %d\n", output);
		}
		else if (i == 2)
		{
			printf(1, "Total number of system calls this process has done so far: %d\n", output);	
		}
		else if(i == 3)
		{
			printf(1, "Number of memory pages for this process: %d\n", output);
		}
		else
		{
			printf(1, "INVALID INPUT\n");
		}

	}
	exit();
}
