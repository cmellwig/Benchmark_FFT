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

#include "mOS_common_types_c.h"
#include "mOS_vcore_u.h"
#include "mOS_segment_manager_u.h"
#include "stdlib.h"
#include "stdio.h"
#include "vbsp.h"
#include "utask.h"
#include <math.h>
#include <stdlib.h>
#include <mppa_power.h>
#include <mppa_async.h>
#include <mppa_remote.h>
#include <vbsp.h>
#include <string.h>
#include <assert.h>
#include "config.h"
#include "fft_kernels.h"

#define min(a,b) (a<b?a:b)

static long long go = 0;
static off64_t go_offset = 0;
static cplx_float_t submatrix_a[N][TILE_HEIGHT][TILE_WIDTH] __attribute__((aligned(64)));
static cplx_float_t submatrix_b[N][TILE_HEIGHT][TILE_WIDTH] __attribute__((aligned(64)));
static int *lut = NULL;
static float *twiddle = NULL;
static float *correction_twiddle_coef = NULL;
static int nb_job_dma = 0;


/** utility function to dump a complex float sub-matrix of size
 *  @p width x @p height
 *  @param m sub-matrix to dump
 *  @param width sub-matrix width
 *  @param height sub-matrix height
 */
void dump_submatrix(cplx_float_t *m, int width, int height)
{
	mppa_rpc_barrier_all();
	int i;
	int cid = __k1_get_cluster_id();
	for(i=0;i<cid;i++)
	{
		mppa_rpc_barrier_all();
	}
	printf("# Cluster %d Dump mat %p\n", cid, m);
	for (i = 0; i < height; i++)
	{
		printf("%d\t", i);
		int j;
		for (j = 0; j < width; j++)
		{
			printf("(%.1f %.1f) ", m[i*width+j].x, m[i*width+j].y);
		}
		printf("\n");
	}
	printf("\n");
	for(i=cid;i<NB_CLUSTER;i++)
	{
		mppa_rpc_barrier_all();
	}
	mppa_rpc_barrier_all();
}

void
flat_transpose(void* local, void *target)
{
	cplx_float_t (*sub_local_a)[TILE_WIDTH] = local;
	cplx_float_t (*sub_local_b)[TILE_WIDTH] = target;
	off64_t offset;
	int cid = __k1_get_cluster_id();
	mppa_async_offset(mppa_async_default_segment(0), (void*)target, &offset);
	mppa_async_event_t evt;
	int i;
	for(i=cid;i<NB_CLUSTER+cid;i++)
	{
		int target_cid = i%NB_CLUSTER;
		if(i != cid)
		{
			int j;
			for(j=0;j<TILE_HEIGHT;j++)
			{
				if(mppa_async_sput_spaced(((void*)sub_local_a) + sizeof(submatrix_a[0][0][0])*TILE_WIDTH/NB_CLUSTER*target_cid + sizeof(submatrix_a[0][0][0])*j,
					mppa_async_default_segment(target_cid), offset + sizeof(submatrix_a[0][0][0])*TILE_WIDTH/NB_CLUSTER*cid + sizeof(submatrix_a[0][0][0])*TILE_WIDTH*j,
					sizeof(submatrix_a[0][0][0]), TILE_WIDTH/NB_CLUSTER, sizeof(submatrix_a[0][0][0])*TILE_WIDTH, sizeof(submatrix_a[0][0][0]), &evt) != 0)
				{
					printf("mppa_async_sput_spaced cid %d failed\n", cid);
				}
				nb_job_dma++;
			}
		}
	}
	int x, y;
	i = cid;
	for (y = 0; y < TILE_HEIGHT; y++)
	{
		for (x = 0; x < TILE_WIDTH/NB_CLUSTER; x++)
		{
			sub_local_b[x][TILE_WIDTH/NB_CLUSTER*i + y] = sub_local_a[y][TILE_WIDTH/NB_CLUSTER*i + x];
		}
	}
	for(i=0;i<NB_CLUSTER;i++)
	{
		mppa_async_postadd(mppa_async_default_segment(i), go_offset, 1);
	}
	mppa_async_event_wait(&evt);
	mppa_async_evalcond(&go, NB_CLUSTER, MPPA_ASYNC_COND_GE, NULL);
	__builtin_k1_afdau(&go, -NB_CLUSTER);
}

typedef struct{
	cplx_float_t * restrict in;
	float *twiddle;
	int *array_bit_reverse;
	int size;
	int height;
}ffts_t;

static void*
ffts_(void *args)
{
	int i;
	ffts_t *fft = (void*)args;
	__builtin_k1_dinval();
	for (i = 0; i < fft->height; i++)
	{
		fft_radix2_float(&(fft->in[i*TILE_WIDTH]), fft->twiddle, fft->array_bit_reverse, fft->size);
	}
	__builtin_k1_wpurge();
	__builtin_k1_fence();
	return NULL;
}

#define NB_FFT_CORE (N_CORES)
static pthread_t t[NB_FFT_CORE];
static ffts_t fft[NB_FFT_CORE];

void
ffts(cplx_float_t * restrict in, const float *twiddle, const int *array_bit_reverse, const int size)
{
	int i;
	for (i = 0; i < NB_FFT_CORE; i++)
	{
		int nb_fft = TILE_HEIGHT/NB_FFT_CORE + (((TILE_HEIGHT%NB_FFT_CORE) > i) ? 1 : 0);
		fft[i].in = (void*)&in[TILE_WIDTH*(i*(TILE_HEIGHT/NB_FFT_CORE) + min(i,TILE_HEIGHT%NB_FFT_CORE))];
		fft[i].twiddle = (float*)twiddle;
		fft[i].array_bit_reverse = (int*)array_bit_reverse;
		fft[i].size = size;
		fft[i].height = nb_fft;
		if(i<NB_FFT_CORE-1)
		{
	 		pthread_create(&t[i], NULL, (void*)ffts_, (void*)&fft[i]); // PE1 -> PE(N-1)
		}else
		{
			ffts_((void*)&fft[i]); // PE0 work
		}
	}
	for (i = 0; i < NB_FFT_CORE-1; i++)
	{
		pthread_join(t[i], NULL); // join PE1 -> PE(N-1)
	}
}

typedef struct{
	cplx_float_t * restrict in;
	int start_twid;
	int height;
}twiddle_correction_t;

static twiddle_correction_t twid[NB_FFT_CORE];

static void*
twiddle_correction_(void *args)
{
	twiddle_correction_t *twid = (void*)args;
	__builtin_k1_dinval();
	int i, j, k = twid->start_twid*2;
	cplx_float_t *restrict in = twid->in;
	for(i=0;i<twid->height;i++)
	{
		float c = 1;
		float s = 0;
		float omega_c = correction_twiddle_coef[k+0];
		float omega_s = correction_twiddle_coef[k+1];
		k += 2;
		for(j=0;j<TILE_WIDTH;j++)
		{

			float x = in[i*TILE_WIDTH + j].x;
			float y = in[i*TILE_WIDTH + j].y;
			in[i*TILE_WIDTH + j].x = x * c - y * s;
			in[i*TILE_WIDTH + j].y = y * c + x * s;

			float x_ = c;
			c = x_ * omega_c - s * omega_s;
			s = x_ * omega_s + s * omega_c;

		}
	}
	__builtin_k1_wpurge();
	__builtin_k1_fence();
	return NULL;
}


void
twiddle_correction(cplx_float_t * restrict in)
{
	int i;
	for (i = 0; i < NB_FFT_CORE; i++)
	{
		int nb_twid = TILE_HEIGHT/NB_FFT_CORE + (((TILE_HEIGHT%NB_FFT_CORE) > i) ? 1 : 0);
		int start_twid = (i*(TILE_HEIGHT/NB_FFT_CORE) + min(i,TILE_HEIGHT%NB_FFT_CORE));
		twid[i].in = (void*)&in[start_twid*TILE_WIDTH];
		twid[i].start_twid = start_twid;
		twid[i].height = nb_twid;
		if(i < NB_FFT_CORE-1)
		{
			pthread_create(&t[i], NULL, (void*)twiddle_correction_, (void*)&twid[i]);  // PE1 -> PE(N-1)
		}else
		{
			twiddle_correction_((void*)&twid[i]); // PE0 work
		}
	}
	for (i = 0; i < NB_FFT_CORE-1; i++)
	{
		pthread_join(t[i], NULL); // join PE1 -> PE(N-1)
	}
}

/* main on PE 0 */
int main(void/*unused*/)
{
	mppa_rpc_client_init();
	mppa_async_init();
	mppa_remote_client_init();

	int cid = __k1_get_cluster_id();
	int buffer = 0;
	lut = fft_radix2_get_bitreverse(TILE_WIDTH);
	twiddle = fft_radix2_get_twiddle_float(TILE_WIDTH);
	correction_twiddle_coef = fft_get_correction_twiddle(WIDTH, HEIGHT);

	mppa_async_segment_t matrix_segment;
	mppa_async_segment_t matrix_segment_out;
	mppa_async_segment_clone(&matrix_segment, MATRIX_SEGMENT_ID, 0, 0, NULL); // input fft samples
	mppa_async_segment_clone(&matrix_segment_out, MATRIX_SEGMENT_ID+1, 0, 0, NULL); // input fft samples

	mppa_async_offset(mppa_async_default_segment(0), (void*)&go, &go_offset);

	#ifdef DEBUG_DUMP
	mppa_rpc_barrier_all();
	if(cid == 0)
	{
		printf("# MPPA - NB_CLUSTER %d in-chip flat FFT %d points. Matrix dim: %d %d. Matrix size: %d\n", NB_CLUSTER, WIDTH*HEIGHT, WIDTH, HEIGHT, WIDTH*HEIGHT*sizeof(submatrix_a[0][0][0]));
	}
	mppa_rpc_barrier_all();

	printf("# Cluster %d NB_CLUSTER %d N %d TILE_WIDTH %d TILE_HEIGHT %d ==> Total %d\n", cid, NB_CLUSTER, N, TILE_WIDTH, TILE_HEIGHT, N*TILE_HEIGHT*TILE_WIDTH*sizeof(submatrix_a[0][0][0]));
	uint64_t s0,s1,s2,s3,s4;
	#endif

	mppa_rpc_barrier_all();

	mppa_async_event_t fence;
	uint64_t start, end, total = 0;
	uint64_t comm = 0;


	start = __k1_read_dsu_timestamp();

	int i;
	for(i=0;i<NB_FFT_ITER;i++)
	{
		uint64_t tmp_dsu = __k1_read_dsu_timestamp();
		mppa_async_get_spaced(submatrix_a[buffer], &matrix_segment, cid*TILE_WIDTH*TILE_HEIGHT*sizeof(submatrix_a[0][0][0]), 
					TILE_WIDTH*sizeof(submatrix_a[0][0][0]), TILE_HEIGHT, TILE_WIDTH*sizeof(submatrix_a[0][0][0]), NULL);
		comm += __k1_read_dsu_timestamp() - tmp_dsu;

		flat_transpose(submatrix_a[buffer], submatrix_b[buffer]);
		#ifdef DEBUG_DUMP
		dump_submatrix((void*)submatrix_b[buffer], TILE_WIDTH, TILE_HEIGHT);
		s0 = __k1_read_dsu_timestamp();
		#endif

		ffts((void*)submatrix_b[buffer], twiddle, lut, TILE_WIDTH);
		#ifdef DEBUG_DUMP
		dump_submatrix((void*)submatrix_b[buffer], TILE_WIDTH, TILE_HEIGHT);
		s1 = __k1_read_dsu_timestamp();
		#endif

		flat_transpose(submatrix_b[buffer], submatrix_a[buffer]);
		#ifdef DEBUG_DUMP
		dump_submatrix((void*)submatrix_a[buffer], TILE_WIDTH, TILE_HEIGHT);
		s2 = __k1_read_dsu_timestamp();
		#endif

		twiddle_correction((void*)submatrix_a[buffer]);
		#ifdef DEBUG_DUMP
		dump_submatrix((void*)submatrix_a[buffer], TILE_WIDTH, TILE_HEIGHT);
		s3 = __k1_read_dsu_timestamp();
		#endif

		ffts((void*)submatrix_a[buffer], twiddle, lut, TILE_WIDTH);
		#ifdef DEBUG_DUMP
		dump_submatrix((void*)submatrix_a[buffer], TILE_WIDTH, TILE_HEIGHT);
		s4 = __k1_read_dsu_timestamp();
		#endif

		flat_transpose(submatrix_a[buffer], submatrix_b[buffer]);

		tmp_dsu = __k1_read_dsu_timestamp();
		mppa_async_put_spaced(submatrix_b[buffer], &matrix_segment_out, cid*TILE_WIDTH*TILE_HEIGHT*sizeof(submatrix_a[0][0][0]), 
					TILE_WIDTH*sizeof(submatrix_a[0][0][0]), TILE_HEIGHT, TILE_WIDTH*sizeof(submatrix_a[0][0][0]), &fence);
		mppa_async_fence(&matrix_segment, &fence);
		mppa_async_event_wait(&fence);
		comm += __k1_read_dsu_timestamp() - tmp_dsu;

		mppa_rpc_barrier_all();
	}

	end = __k1_read_dsu_timestamp();

	total = end - start;

	/* write backresult */

	#ifdef DEBUG_DUMP
	dump_submatrix(submatrix_b[buffer], WIDTH, HEIGHT);
	#endif

	#define CHIP_FREQ ((float)__bsp_frequency/1000.0f)
	float time_ms = (float)total/CHIP_FREQ;
	time_ms /= NB_FFT_ITER;
	float comm_ms __attribute__((unused))= (float)comm/CHIP_FREQ;
	comm_ms /= NB_FFT_ITER;

	#ifdef DEBUG_DUMP
	uint64_t transpose_time = (end-s4) + (s2-s1) + (s0 - start);
	uint64_t ffts_time = (s4-s3) + (s1-s0);
	uint64_t twiddle_time = s3-s2;
	float nb_bytes = (float)(sizeof(submatrix_a[0][0][0])*WIDTH*HEIGHT*2);
	float bw_gbs = (nb_bytes/1000000000.0f) / (time_ms/1000);
	printf("# Cluster %d nb_job_dma %d cycle %lld time_ms %.4f ms Total in-chip memory bandwidth %.3f GB/s\n", cid, nb_job_dma, total, time_ms, bw_gbs);
	mppa_rpc_barrier_all();
	float transpose_time_ms = (float)transpose_time/CHIP_FREQ;
	float ffts_time_ms = (float)ffts_time/CHIP_FREQ;
	float twiddle_time_ms = (float)twiddle_time/CHIP_FREQ;
	mppa_rpc_barrier_all();
	printf("# Cluster %d total %.3f ms transpose %.3f ms (%.1f) ffts %.3f ms (%.1f) twiddle %.3f ms (%.1f) \n", cid, time_ms, transpose_time_ms, transpose_time_ms/time_ms*100.0f, ffts_time_ms, ffts_time_ms/time_ms*100.0f, twiddle_time_ms, twiddle_time_ms/time_ms*100.0f);
	#endif
	static float com_average[NB_CLUSTER];
	off64_t off;
	mppa_async_offset(MPPA_ASYNC_SMEM_0, &com_average[__k1_get_cluster_id()], &off);
	mppa_async_put(&comm_ms, MPPA_ASYNC_SMEM_0, off, sizeof(comm_ms), NULL);
	mppa_async_fence(MPPA_ASYNC_SMEM_0, NULL);
	mppa_rpc_barrier_all();
	if(cid == 0)
	{
		comm_ms = 0;
		for(int i=0;i<NB_CLUSTER;i++)
		{
			comm_ms += com_average[i];
		}
		comm_ms /= NB_CLUSTER;
		printf("Freq %.1f MHz %d Cluster(s) %d Core(s) FFT %d x %d = %d Total Time %.2f ms Comm. Time %.2f ms Compute Time %.2f ms - %.1f FFT / s\n", CHIP_FREQ/1000, NB_CLUSTER, N_CORES, WIDTH, HEIGHT, WIDTH*HEIGHT, time_ms, comm_ms, time_ms-comm_ms, 1/time_ms*1000);
	}
	mppa_rpc_barrier_all();
	mppa_async_final();
	return 0;
}
