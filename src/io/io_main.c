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
#include <mppa_power.h>
#include <mppa_routing.h>
#include <stdlib.h>
#include <stdio.h>
#include <mppa_remote.h>
#include <mppa_async.h>
#include <assert.h>
#include <utask.h>
#include <HAL/hal/board/boot_args.h>
#include <mppa_async.h>
#include <math.h>
#include "config.h"
#include "fft_kernels.h"


/** Error threshold for comparison between computed value and reference */
#define TEST_THRESHOLD (0.1)

void
fft_radix_2_float_reference(cplx_float_t *in, int len)
{
    float  x_reel, x_im,
           t_reel, t_im,
           u_reel, u_im;
    int i, k, j, m;
    float tx, ty;

    /* Do the bit reversal */
    int i2 = len >> 1;
    j = 0;
    for (i=0;i<len-1;i++)
    {
        if (i < j)
        {
            tx = in[i].x;
            ty = in[i].y;
            in[i].x = in[j].x;
            in[i].y = in[j].y;
            in[j].x = tx;
            in[j].y = ty;
        }

        k = i2;
        while (k <= j)
        {
            j -= k;
            k >>= 1;
        }

        j += k;
    }
    /* do fft */
    for (m = 2; m <= len; m *= 2)
    {
        for (k = 0; k < len; k += m)
        {
            x_reel = 1.0f;
            x_im   = 0.0f;
            i=0;
            for (j = 0; j < m / 2; j++)
            {
                x_reel =   (double)cos(2*M_PI*(double)i/(double)m);
                x_im   = (double)-sin(2*M_PI*(double)i/(double)m);
                i++;

                t_reel = x_reel * in[k + j + m/2].x - x_im * in[k + j + m/2].y;
                t_im   = x_reel * in[k + j + m/2].y + x_im * in[k + j + m/2].x;

                u_reel = in[k + j].x;
                u_im   = in[k + j].y;

                in[k + j].x = u_reel + t_reel;
                in[k + j].y = u_im   + t_im;

                in[k + j + m/2].x = u_reel - t_reel;
                in[k + j + m/2].y = u_im   - t_im;
            }
        }
    }
}

/** Check if absolute difference between matrix_out coefficients and matrix_check
 *  ones exceed TEST_THRESHOLD
 *  @param[inout] real_diff value of the maximal absolute diff between
 *                          real coeffs (MUST be init with 0.f)
 *  @param[inout] im_diff value of the maximal absolute diff between
 *                          imaginary coeffs (MUST be init with 0.f)
 */
int check_result_matrix(cplx_float_t* matrix_out, cplx_float_t* matrix_check,
                        float* real_diff, float* im_diff)
{
    // number of differences
    int diff = 0;

    for(int ii=0;ii<HEIGHT;ii++)
    {
        for(int jj=0;jj<WIDTH;jj++)
        {
            float abs_diff = fabs(matrix_out[ii*HEIGHT + jj].x-matrix_check[ii*HEIGHT + jj].x);
            if( abs_diff > TEST_THRESHOLD || isnan(matrix_out[ii*HEIGHT + jj].x) )
            {
                diff++;
            }
            if(abs_diff > *real_diff)
                *real_diff = diff;
        }
    }
    for(int ii=0;ii<HEIGHT;ii++)
    {
        for(int jj=0;jj<WIDTH;jj++)
        {
            float abs_diff =  fabs(matrix_out[ii*HEIGHT + jj].y -matrix_check[ii*HEIGHT + jj].y);
            if( abs_diff > TEST_THRESHOLD || isnan(matrix_out[ii*HEIGHT + jj].y) )
            {
                diff++;
            }
            if(abs_diff > *im_diff)
                *im_diff = diff;
        }
    }

    return diff;
}


int main() {
    mppadesc_t pcie_fd = 0;
    if (__k1_spawn_type() == __MPPA_PCI_SPAWN) {
        pcie_fd = pcie_open(0);
        pcie_queue_init(pcie_fd);
        pcie_register_console(pcie_fd, stdin, stdout);
    }
    mppa_rpc_server_init(1, 0, NB_CLUSTER);
    mppa_async_server_init();
    mppa_remote_server_init(pcie_fd, NB_CLUSTER);

    for(unsigned i=0;i<NB_CLUSTER;i++){
        if (mppa_power_base_spawn(i, "cluster_bin", NULL, NULL, MPPA_POWER_SHUFFLING_ENABLED) == -1)
            printf("# [IODDR0] Fail to Spawn cluster %d\n", i);
    }

    utask_t t;
    utask_create(&t, NULL, (void*)mppa_rpc_server_start, NULL);

    int matrix_size = sizeof(cplx_float_t)*WIDTH*HEIGHT;

    cplx_float_t *matrix = NULL;
    cplx_float_t *matrix_out = NULL;
    cplx_float_t *matrix_check = NULL;
    posix_memalign((void*)&matrix, 1<<13, matrix_size);
    posix_memalign((void*)&matrix_out, 1<<13, matrix_size);
    posix_memalign((void*)&matrix_check, 1<<13, matrix_size);

    if (!matrix) {
        printf("ERROR: failed to allocate matrix\n");
        return -1;
    }
    if (!matrix_out) {
        printf("ERROR: failed to allocate matrix_out\n");
        return -1;
    }
    if (!matrix_check) {
        printf("ERROR: failed to allocate matrix_check\n");
        return - 1;
    }

    {
        float v = 0;
        for(int i=0;i<WIDTH*HEIGHT;i++)
        {
            v = (float)rand()/(RAND_MAX/32);
            matrix[i].x = (float)v;
            matrix[i].y = (float)0.0f;
            matrix_check[i].x = (float)v;
            matrix_check[i].y = (float)0.0f;
        }
    }
    __builtin_k1_wpurge();
    __builtin_k1_fence();

    mppa_async_segment_t matrix_segment;
    mppa_async_segment_t matrix_segment_out;
    mppa_async_segment_create(&matrix_segment, MATRIX_SEGMENT_ID, matrix,
                              matrix_size, 0, 0, NULL);
    mppa_async_segment_create(&matrix_segment_out, MATRIX_SEGMENT_ID+1,
                              matrix_out, matrix_size, 0, 0, NULL);


    int status = 0;
    for(unsigned i=0;i<NB_CLUSTER;i++){
        int ret;
        if (mppa_power_base_waitpid(i, &ret, 0) < 0) {
            printf("# [IODDR0] Waitpid failed on cluster %d\n", i);
        }
        status += ret;
    }
    if(status != 0)
        return -1;

    printf("# IO%d starts checking. Please wait.\n", __k1_get_cluster_id());
    mOS_dinval();
    assert(HEIGHT == WIDTH);
    fft_radix_2_float_reference(matrix_check, WIDTH*HEIGHT);

    float im_diff = 0.f;
    float real_diff = 0.f;
    int diff = check_result_matrix(matrix_out, matrix_check, &real_diff, &im_diff);

    char string[30];
    if(diff)
    {
        sprintf(string, "FAILED");
    }else
    {
        sprintf(string, "SUCCESS");
    }
    if(diff)
    {
        printf("# [IODDR0] real_diff %e im_diff %e diff %d %s\n", real_diff, im_diff, diff, string);
        return -1;
    }

    /* Send an exit message on pcie interface */
    if (__k1_spawn_type() == __MPPA_PCI_SPAWN) {
        int remote_status;
        pcie_queue_barrier(pcie_fd, 0, &remote_status);
        pcie_unregister_console(pcie_fd);
        pcie_queue_exit(pcie_fd, 0, NULL);
    }
    printf("# [IODDR0] Goodbye\n");
    return 0;
}
