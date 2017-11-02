// Used following reference: http://recolog.blogspot.com/2016/03/adding-user-program-to-xv6.html
#include "types.h"
#include "stat.h"
#include "user.h"

int
main(void)
{
	int output, i;
	printf(1, "Jasmine's first user program on xv6.\n");
	for(i = 1; i < 4; i ++){
		output = info(i);
		if (i == 1)
		{
			printf(1, "Num of processes in system: %d\n", output);
		}
		else if (i == 2)
		{
			printf(1, "Num of system calls for this process: %d\n", output);	
		}/*
		else if(i == 3)
		{
			printf(1, "Num of memory pages of this process: %d\n", output);
		}*/
		else
		{
			printf(1, "INVALID INPUT\n");
		}

	}
	exit();
}
