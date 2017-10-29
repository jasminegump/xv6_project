// Used following reference: http://recolog.blogspot.com/2016/03/adding-user-program-to-xv6.html
#include "types.h"
#include "stat.h"
#include "user.h"

int
main(void)
{
	printf(1, "Jasmine's first user program on xv6.\n");
	info(1);
	exit();
}
