#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int main(int argc, char *argv[])
{
	int v;
    if (argc > 1)
    {
        v=1;
        printf(v, "Error!!\nCorrect Usage: ps\n\n");
    }
    else
    {
       v += ps();
    }
    exit();
}
