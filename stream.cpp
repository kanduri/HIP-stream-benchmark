/*
  STREAM benchmark implementation in HIP.

    COPY:       a(i) = b(i)                 
    SCALE:      a(i) = q*b(i)               
    SUM:        a(i) = b(i) + c(i)          
    TRIAD:      a(i) = b(i) + q*c(i)        

  It measures the memory system on the device.
  The implementation is in double precision.

  Code based on the code developed by John D. McCalpin
  http://www.cs.virginia.edu/stream/FTP/Code/stream.c

  Written by: Massimiliano Fatica, NVIDIA Corporation

  Further modifications by: Ben Cumming, CSCS; Andreas Herten (JSC/FZJ);
  Prashanth Kanduri, CSCS
*/

#define NTIMES  20

#include <string>
#include <vector>

#include <stdio.h>
#include <float.h>
#include <limits.h>
#include <unistd.h>
#include <sys/time.h>

#include <sys/time.h>

#include "hip/hip_runtime.h"

# ifndef MIN
# define MIN(x,y) ((x)<(y)?(x):(y))
# endif
# ifndef MAX
# define MAX(x,y) ((x)>(y)?(x):(y))
# endif

typedef double real;

static double   avgtime[4] = {0}, maxtime[4] = {0},
        mintime[4] = {FLT_MAX,FLT_MAX,FLT_MAX,FLT_MAX};


void print_help()
{
    printf(
        "Usage: stream [-s] [-n <elements>] [-b <blocksize>]\n\n"
        "  -s\n"
        "        Print results in SI units (by default IEC units are used)\n\n"
        "  -n <elements>\n"
        "        Put <elements> values in the arrays\n"
        "        (defaults to 1<<26)\n\n"
        "  -b <blocksize>\n"
        "        Use <blocksize> as the number of threads in each block\n"
        "        (defaults to 192)\n"
    );
}

void parse_options(int argc, char** argv, bool& SI, int& N, int& blockSize)
{
    // Default values
    SI = false;
    N = 1<<26;
    blockSize = 192;

    int c;

    while ((c = getopt (argc, argv, "sn:b:h")) != -1)
        switch (c)
        {
            case 's':
                SI = true;
                break;
            case 'n':
                N = std::atoi(optarg);
                break;
            case 'b':
                blockSize = std::atoi(optarg);
                break;
            case 'h':
                print_help();
                std::exit(0);
                break;
            default:
                print_help();
                std::exit(1);
        }
}

/* A gettimeofday routine to give access to the wall
   clock timer on most UNIX-like systems.  */


double mysecond()
{
    struct timeval tp;
    struct timezone tzp;
    int i = gettimeofday(&tp,&tzp);
    return ( (double) tp.tv_sec + (double) tp.tv_usec * 1.e-6 );
}


template <typename T>
__global__ void set_array(T * __restrict__ const a, T value, int len)
{
    int idx = threadIdx.x + blockIdx.x * blockDim.x;
    if (idx < len)
        a[idx] = value;
}

template <typename T>
__global__ void STREAM_Copy(T const * __restrict__ const a, T * __restrict__ const b, int len)
{
    int idx = threadIdx.x + blockIdx.x * blockDim.x;
    if (idx < len)
        b[idx] = a[idx];
}

template <typename T>
__global__ void STREAM_Scale(T const * __restrict__ const a, T * __restrict__ const b, T scale,  int len)
{
    int idx = threadIdx.x + blockIdx.x * blockDim.x;
    if (idx < len)
        b[idx] = scale * a[idx];
}

template <typename T>
__global__ void STREAM_Add(T const * __restrict__ const a, T const * __restrict__ const b, T * __restrict__ const c, int len)
{
    int idx = threadIdx.x + blockIdx.x * blockDim.x;
    if (idx < len)
        c[idx] = a[idx] + b[idx];
}

template <typename T>
__global__ void STREAM_Triad(T const * __restrict__ a, T const * __restrict__ b, T * __restrict__ const c, T scalar, int len)
{
    int idx = threadIdx.x + blockIdx.x * blockDim.x;
    if (idx < len)
        c[idx] = a[idx] + scalar * b[idx];
}

int main(int argc, char** argv)
{
    real *d_a, *d_b, *d_c;
    int j,k;
    float times[4][NTIMES];
    real scalar;
    std::vector<std::string> label{"Copy:      ", "Scale:     ", "Add:       ", "Triad:     "};

    // Parse arguments
    bool SI;
    int N, blockSize;
    parse_options(argc, argv, SI, N, blockSize);

    printf(" STREAM Benchmark implementation in HIP\n");
    printf(" Array size (%s precision) =%7.2f MB\n", sizeof(double)==sizeof(real)?"double":"single", double(N)*double(sizeof(real))/1.e6);

    /* Allocate memory on device */
    hipMalloc((void**)&d_a, sizeof(real)*N);
    hipMalloc((void**)&d_b, sizeof(real)*N);
    hipMalloc((void**)&d_c, sizeof(real)*N);

    /* Compute execution configuration */
    dim3 dimBlock(blockSize);
    dim3 dimGrid(N/dimBlock.x );
    if( N % dimBlock.x != 0 ) dimGrid.x+=1;

    printf(" using %d threads per block, %d blocks\n",dimBlock.x,dimGrid.x);

    if (SI)
        printf(" output in SI units (KB = 1000 B)\n");
    else
        printf(" output in IEC units (KiB = 1024 B)\n");

    /* Initialize memory on the device */
    hipLaunchKernelGGL((set_array<real>), dim3(dimGrid), dim3(dimBlock), 0, 0, d_a, 2.f, N);
    hipLaunchKernelGGL((set_array<real>), dim3(dimGrid), dim3(dimBlock), 0, 0, d_b, .5f, N);
    hipLaunchKernelGGL((set_array<real>), dim3(dimGrid), dim3(dimBlock), 0, 0, d_c, .5f, N);

    /*  --- MAIN LOOP --- repeat test cases NTIMES times --- */
    hipEvent_t start, stop;
    hipEventCreate(&start);
    hipEventCreate(&stop);


    scalar=3.0f;
    for (k=0; k<NTIMES; k++)
    {
        hipEventRecord(start);
        hipLaunchKernelGGL((STREAM_Copy<real>), dim3(dimGrid), dim3(dimBlock), 0, 0, d_a, d_c, N);
        hipEventRecord(stop);
        hipEventSynchronize(stop);
        hipEventElapsedTime(&times[0][k], start, stop);

        hipEventRecord(start);
        hipLaunchKernelGGL((STREAM_Scale<real>), dim3(dimGrid), dim3(dimBlock), 0, 0, d_b, d_c, scalar,  N);
        hipEventRecord(stop);
        hipEventSynchronize(stop);
        hipEventElapsedTime(&times[1][k], start, stop);

        hipEventRecord(start);
        hipLaunchKernelGGL((STREAM_Add<real>), dim3(dimGrid), dim3(dimBlock), 0, 0, d_a, d_b, d_c,  N);
        hipEventRecord(stop);
        hipEventSynchronize(stop);
        hipEventElapsedTime(&times[2][k], start, stop);

        hipEventRecord(start);
        hipLaunchKernelGGL((STREAM_Triad<real>), dim3(dimGrid), dim3(dimBlock), 0, 0, d_b, d_c, d_a, scalar,  N);
        hipEventRecord(stop);
        hipEventSynchronize(stop);
        hipEventElapsedTime(&times[3][k], start, stop);

    }
    /*  --- SUMMARY --- */

    for (k=1; k<NTIMES; k++) /* note -- skip first iteration */
    {
        for (j=0; j<4; j++)
        {
            avgtime[j] = avgtime[j] + times[j][k];
            mintime[j] = MIN(mintime[j], times[j][k]);
            maxtime[j] = MAX(maxtime[j], times[j][k]);
        }
    }

    double bytes[4] = {
        2 * sizeof(real) * (double)N,
        2 * sizeof(real) * (double)N,
        3 * sizeof(real) * (double)N,
        3 * sizeof(real) * (double)N
    };

    // Use right units
    const double G = SI ? 1.e9 : static_cast<double>(1<<30);

    printf("\nFunction      Rate %s  Avg time(s)  Min time(s)  Max time(s)\n",
           SI ? "(GB/s) " : "(GiB/s)" );
    printf("-----------------------------------------------------------------\n");
    for (j=0; j<4; j++) {
        avgtime[j] = avgtime[j]/(double)(NTIMES-1);

        printf("%s%11.4f     %11.8f  %11.8f  %11.8f\n", label[j].c_str(),
                1e3*bytes[j]/mintime[j] / G,
                1e-3*avgtime[j],
                1e-3*mintime[j],
                1e-3*maxtime[j]);
    }


    /* Free memory on device */
    hipFree(d_a);
    hipFree(d_b);
    hipFree(d_c);
}

