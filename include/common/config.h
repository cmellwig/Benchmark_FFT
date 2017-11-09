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
#ifndef CONFIG_H
#define CONFIG_H

/* dynamic segment id */
#define MATRIX_SEGMENT_ID (10)

/* tile */
#define TILE (256) 	/* to configure the matrix size of transpose in-chip */
#define TILE_WIDTH (TILE)
#define TILE_HEIGHT (TILE/NB_CLUSTER)

/* global matrix */
#define WIDTH (TILE_WIDTH)
#define HEIGHT (TILE_HEIGHT*NB_CLUSTER)

/* tile buffer */
#define N (1)

/* nb fft iteration */
#define NB_FFT_ITER (500)

#if !(NB_CLUSTER==1 || NB_CLUSTER==2 || NB_CLUSTER==4 || NB_CLUSTER==8 || NB_CLUSTER==16)
#error "Please only 1, 2, 4, 8 or 16 cluster(s) implementations are supported\n"
#endif

#if (N_CORES<=0 || N_CORES>16)
#error "Please the number of core(s) must be in range [1,16]\n"
#endif

#endif
