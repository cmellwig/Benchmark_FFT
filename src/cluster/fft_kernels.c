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

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <utask.h>
#include <HAL/hal/hal_ext.h>
#include <mOS_vcore_u.h>
#include "config.h"
#include "fft_kernels.h"

float*
fft_radix2_get_twiddle_float(int size)
{
	int m, k, i, j;
	i=0;
	for (m = 2; m <= size; m *= 2)
	{
		for (k = 0; k < size; k += m)
		{
			for (j = 0; j < m / 2; j++)
			{
				i += 2;
			}
		}
	}
	float *twiddle = NULL;
	posix_memalign((void**)&twiddle, 64, i*sizeof(*twiddle));
	if(twiddle == NULL)
	{
		printf("Cluster %d fft_radix2_get_twiddle_float failed to alloc twiddle lut of size %d\n", __k1_get_cluster_id(), sizeof(*twiddle)*i);
		mOS_exit(1,-1);
	}
	/* fill twiddle */
	int f = 0;
	for (m = 2; m <= size; m *= 2)
	{
		for (k = 0; k < size; k += m)
		{
			i=0;
			for (j = 0; j < m / 2; j++)
			{
				float x_reel = (float)cos(2*M_PI*(double)i/(double)m);
				float x_im = (float)-sin(2*M_PI*(double)i/(double)m);
				twiddle[f+0] = x_reel;
				twiddle[f+1] = x_im;
				f += 2;
				i++;
			}
		}
	}
	return twiddle;
}

static int fft_radix_count_bit_reverse = 0;

int*
fft_radix2_get_bitreverse(int size)
{
	int count = 0;
	int *lut = NULL;
	posix_memalign((void**)&lut, 64, size*sizeof(*lut));
	if(lut == NULL)
	{
		printf("Cluster %d fft_radix2_get_bitreverse failed to alloc lut\n", __k1_get_cluster_id());
		mOS_exit(1,-1);
	}
    /* Do the bit reversal */
    int i2 = size >> 1;
    int k, i, j = 0;
	for (i=0;i<size-1;i++)
	{
		if (i < j)
		{
			lut[count] = i;
			count++;
			lut[count] = j;
			count++;
		}

		k = i2;

		while (k <= j)
		{
			j -= k;
			k >>= 1;
		}

		j += k;

	}
	fft_radix_count_bit_reverse = count;
	if(count >= size)
	{
		printf("fft_radix2_get_bitreverse failed\n");
		mOS_exit(1,-1);
	}
	return lut;
}

void
fft_radix2_float(cplx_float_t * restrict in, const float *twiddle, const int *array_bit_reverse, const int size)
{
	int i=0, j, k, m;
	uint64_t dword;
	for (i=0;i<fft_radix_count_bit_reverse;i+=2)
	{
		dword								= in[array_bit_reverse[i+0]].dword;
		in[array_bit_reverse[i+0]].dword	= in[array_bit_reverse[i+1]].dword;
		in[array_bit_reverse[i+1]].dword	= dword;
	}
	i = 0;
	for (m = 2; m <= size; m *= 2)
	{
		for (k = 0; k < size; k += m)
		{
			for (j = 0; j < m / 2; j++)
			{
				register float x_reel = twiddle[i+0];
				register float x_im = twiddle[i+1];
				i += 2;

				register float t_reel = x_reel * in[k + j + m/2].x - x_im * in[k + j + m/2].y;
				register float t_im   = x_reel * in[k + j + m/2].y + x_im * in[k + j + m/2].x;

				register float u_reel = in[k + j].x;
				register float u_im   = in[k + j].y;

				in[k + j].x = u_reel + t_reel;
				in[k + j].y = u_im   + t_im;

				in[k + j + m/2].x = u_reel - t_reel;
				in[k + j + m/2].y = u_im   - t_im;

			}
		}
	}
}

float*
fft_get_correction_twiddle(int w, int h)
{
	int cid = __k1_get_cluster_id();
	int i_base = cid * (TILE_HEIGHT);
	float *correction_twiddle = NULL;
	posix_memalign((void**)&correction_twiddle, 64, sizeof(*correction_twiddle)*TILE_HEIGHT*2);
	assert(correction_twiddle != NULL && "correction_twiddle alloc failed\n");
	int i, j=0;
	for(i=0;i<TILE_HEIGHT;i++)
	{
		float omega_c = (float) cos(2*M_PI*((float)(i+i_base))/((float)(w*h)));
		float omega_s = (float)-sin(2*M_PI*((float)(i+i_base))/((float)(w*h)));
		correction_twiddle[j+0] = omega_c;
		correction_twiddle[j+1] = omega_s;
		j += 2;
	}
	__builtin_k1_wpurge();
	return correction_twiddle;
}

