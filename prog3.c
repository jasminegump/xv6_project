
#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[]) {
	int i, k;
	const int loop= 5000;
	settickets(10);
	for(i = 0; i<loop; i++)
	{
		asm("nop"); //in order to prevent the compiler from optimizing the for loop
		for(k = 0; k<loop; k++)
		{
			asm("nop");
		}
	}
	exit();
}