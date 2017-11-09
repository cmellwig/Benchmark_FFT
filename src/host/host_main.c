/**
 * MIT License
 *
 * Copyright (c) 2017 Kalray S.A
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pcie.h>

int main(int argc, char **argv)
{
	mppadesc_t fd;
	int mppa_ret;

	if(!(fd = pcie_open_device(0))) 
		exit(-1);

	if (argc >= 3) {
		if ( pcie_load_io_exec_mb(fd, argv[1], argv[2], PCIE_LOAD_FULL ) ) {
			printf ("Boot of MPPA failed\n");
			exit(1);
		}
	}
	#ifdef DEBUG_DUMP
	printf("# [HOST] pcie queue init\n");
	#endif
	pcie_queue_init(fd);
	/* pcie_queue init needs to be called to enable pcie communication via queues */ 
	#ifdef DEBUG_DUMP
	printf("# [HOST] init queue ok\n");	
	#endif
	pcie_register_console(fd, stdin, stdout);
	#ifdef DEBUG_DUMP
	printf("# [HOST] pcie_register_console ok\n");
	#endif
	int status;
	int local_status = 0;
	#ifdef DEBUG_DUMP
	printf("# [HOST] waits\n");	
	#endif
	pcie_queue_barrier(fd, local_status, &status);
	pcie_queue_exit(fd, 0xDEAD, &mppa_ret);
	if(mppa_ret != 0)
		return -1;
	#ifdef DEBUG_DUMP
	printf("# [HOST] MPPA exited with status %d\n", status);
	#endif
	pcie_close(fd);
	#ifdef DEBUG_DUMP
	printf("# [HOST] Goodbye\n");
	#endif
 	return 0;
}
