#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#include "macdecls.h"
#include "ga.h"
#include "mp3.h"

#define NDIM 16384

#define MAX_FACTOR 1024
void grid_factor(int p, int xdim, int ydim, int *idx, int *idy) {
  int i, j; 
  int ip, ifac, pmax, prime[MAX_FACTOR];
  int fac[MAX_FACTOR];
  int ix, iy, ichk;

  i = 1;
/**
 *   factor p completely
 *   first, find all prime numbers, besides 1, less than or equal to 
 *   the square root of p
 */
  ip = (int)(sqrt((double)p))+1;
  pmax = 0;
  for (i=2; i<=ip; i++) {
    ichk = 1;
    for (j=0; j<pmax; j++) {
      if (i%prime[j] == 0) {
        ichk = 0;
        break;
      }
    }
    if (ichk) {
      pmax = pmax + 1;
      if (pmax > MAX_FACTOR) printf("Overflow in grid_factor\n");
      prime[pmax-1] = i;
    }
  }
/**
 *   find all prime factors of p
 */
  ip = p;
  ifac = 0;
  for (i=0; i<pmax; i++) {
    while(ip%prime[i] == 0) {
      ifac = ifac + 1;
      fac[ifac-1] = prime[i];
      ip = ip/prime[i];
    }
  }
/**
 *  p is prime
 */
  if (ifac==0) {
    ifac++;
    fac[0] = p;
  }
/**
 *    find two factors of p of approximately the same size
 */
  *idx = 1;
  *idy = 1;
  for (i = ifac-1; i >= 0; i--) {
    ix = xdim/(*idx);
    iy = ydim/(*idy);
    if (ix >= iy && ix > 1) {
      *idx = fac[i]*(*idx);
    } else if (iy >= ix && iy > 1) {
      *idy = fac[i]*(*idy);
    } else {
      printf("Too many processors in grid factoring routine\n");
    }
  }
}

int main(int argc, char **argv) {
  int s_a, g_v, g_av;
  int one;
  int64_t one_64;
  int me, nproc;
  int idim, jdim;
  int64_t xdim, ydim;
  int ipx, ipy, idx, idy;
  int64_t ilo, ihi, jlo, jhi;
  int64_t i, j;
  int  iproc, ld, ncols, ncnt;
  double val;
  double *vptr;
  double *vbuf, *vsum;
  int64_t *iptr = NULL, *jptr = NULL;
  int ok;
  double r_one = 1.0;
  double ir, jr, ldr;
  /* Intitialize a message passing library */
  one = 1;
  one_64 = 1;
  MP_INIT(argc,argv);
  /* Initialize GA */
  NGA_Initialize();

  xdim = NDIM;
  ydim = NDIM;
  idim = NDIM;
  jdim = NDIM;
  me = GA_Nodeid();
  nproc = GA_Nnodes();

  /* factor array */
  grid_factor(nproc, idim, jdim, &ipx, &ipy);
  if (me == 0) {
    printf("Testing sparse array on %d processors\n",nproc);
    printf("\n    Using %d X %d processor grid\n",ipx,ipy);
    printf("\n    Matrix size is %d X %d\n",idim,jdim);
  }
  /* figure out process location in proc grid */
  idx = me%ipx;
  idy = (me-idx)/ipx;
  /* find bounding indices for this processor */
  ilo = (xdim*idx)/ipx;
  if (idx < ipx-1) {
    ihi = (xdim*(idx+1))/ipx-1;
  } else {
    ihi = xdim-1;
  }
  jlo = (ydim*idy)/ipy;
  if (idy < ipy-1) {
    jhi = (ydim*(idy+1))/ipy-1;
  } else {
    jhi = ydim-1;
  }
 
  /* create sparse array */
  s_a = NGA_Sprs_array_create64(xdim, ydim, C_DBL);
  if (ydim%2 == 0) {
    ld = ydim/2;
  } else {
    ld = (ydim-1)/2+1;
  }
  ldr = (double)ld;
  /* add elements to array. Every other element is zero */
  for (i=ilo; i<=ihi; i++) {
    ir = (double)(i/2);
    for (j=jlo; j<=jhi; j++) {
      jr = (double)(j/2);
      if (i%2 == 0 && j%2 == 0) {
        val = (ir)*ldr+jr;
        NGA_Sprs_array_add_element64(s_a,i,j,&val);
      }
    }
  }
  if (NGA_Sprs_array_assemble(s_a) && me == 0) {
    printf("\n    Sparse array assembly completed\n");
  }

  /* construct vector */
  g_v = NGA_Create_handle();
  NGA_Set_data64(g_v,one,&ydim,C_DBL);
  NGA_Allocate(g_v);
  g_av = GA_Duplicate(g_v, "dup");
  GA_Zero(g_av);

  /* set vector values */
  NGA_Distribution64(g_v,me,&ilo,&ihi);
  NGA_Access64(g_v,&ilo,&ihi,&vptr,&one_64);
  for (i=ilo;i<=ihi;i++) {
    vptr[i-ilo] = (double)i;
  }
  if (me == 0) {
    printf("\n    Vector initialized\n");
  }
  NGA_Release64(g_v,&ilo,&ihi);


  /* access array blocks and check values for correctness */
  NGA_Sprs_array_row_distribution64(s_a,me,&ilo,&ihi);
  ok = 1;
  ncnt = 0;
  for (iproc=0; iproc<nproc; iproc++) {
    NGA_Sprs_array_column_distribution64(s_a,iproc,&jlo,&jhi);
    void *tptr;
    NGA_Sprs_array_access_col_block64(s_a,iproc,&iptr,&jptr,&tptr);
    vptr = (double*)tptr;
    if (vptr != NULL) {
      for (i=ilo; i<=ihi; i++) {
        ncols = iptr[i+1-ilo]-iptr[i-ilo];
        for (j=0; j<ncols; j++) {
          ncnt++;
          idy = jptr[iptr[i-ilo]+j];
          if (i%2 != 0 || idy%2 != 0) ok = 0;
          ir = (double)(i/2);
          jr = (double)(idy/2);
          val = ir*ldr+jr;
          if (fabs(val-vptr[iptr[i-ilo]+j]) > 1.0e-5) {
            ok = 0;
            printf("p[%d] i: %d j: %d val: %f\n",me,(int)i,
                (int)jptr[iptr[i-ilo]+j],vptr[iptr[i-ilo]+j]);
          }
        }
      }
    }
  }
  GA_Igop(&ncnt,one,"+");
  if (ncnt != (idim/2)*(jdim/2)) ok = 0;
  if (ok && me==0) {
    printf("\n    Values in sparse array are correct\n");
  }

  /* multiply sparse matrix by sparse vector */
  vsum = (double*)malloc((ihi-ilo+1)*sizeof(double));
  for (i=ilo; i<=ihi; i++) {
    vsum[i-ilo] = 0.0;
  }
  for (iproc=0; iproc<nproc; iproc++) {
    NGA_Sprs_array_column_distribution64(s_a,iproc,&jlo,&jhi);
    void *tptr;
    NGA_Sprs_array_access_col_block64(s_a,iproc,&iptr,&jptr,&tptr);
    vptr = (double*)tptr;
    if (vptr != NULL) {
      vbuf = (double*)malloc((jhi-jlo+1)*sizeof(double));
      NGA_Get64(g_v,&jlo,&jhi,vbuf,&one_64);
      for (i=ilo; i<=ihi; i++) {
        ncols = iptr[i+1-ilo]-iptr[i-ilo];
        for (j=0; j<ncols; j++) {
          vsum[i-ilo] += vptr[iptr[i-ilo]+j]*vbuf[jptr[iptr[i-ilo]+j]-jlo];
          /*
          printf("i: %d j: %d a: %f v: %f j': %d tot: %f\n",i,jptr[iptr[i-ilo]+j],
              vptr[iptr[i-ilo]+j],vbuf[jptr[iptr[i-ilo]+j]-1-jlo],
              jptr[iptr[i-ilo]+j]-1-jlo,vsum[i-ilo]);
              */
        }
        /*
        printf("i: %d vsum: %f\n",i,vsum[i-ilo]);
        */
      }
      free(vbuf);
    }
  }
  if (ihi>=ilo) NGA_Acc64(g_av,&ilo,&ihi,vsum,&one_64,&r_one);
  GA_Sync();
  free(vsum);

  /* check product vector */
  ok = 1;
  NGA_Distribution64(g_av,me,&ilo,&ihi);
  NGA_Access64(g_av,&ilo,&ihi,&vptr,&one_64);
  /*
  printf("ilo: %d ihi: %d\n",ilo,ihi);
  */
  for (i=ilo; i<=ihi; i++) {
    val = 0.0;
    if (i%2 == 0) {
      for (j=0; j<ydim; j++) {
        if (j%2 == 0) {
          /*
          printf("i: %d j: %d a: %d v: %d\n",i+1,j+1,(i/2)*ld+(j/2),j);
          */
          ir = (double)i;
          jr = (double)j;
          val += (ir*ldr+jr)*jr*0.5;
        }
      }
      if (fabs(val-vptr[i-ilo]) >= 1.0e-5) {
        ok = 0;
        printf("Error for element %d expected: %f actual: %f\n",
            (int)i,val,vptr[i-ilo]);
      }
    } else {
      if (fabs(vptr[i-ilo]) >= 1.0e-5) {
        ok = 0;
        printf("Error for element %d expected: 0.00000 actual: %f\n",
            (int)i,vptr[i-ilo]);
      }
    }
  }
  if (ok && me==0) {
    printf("\n    Matrix-vector product is correct\n\n");
  }

  NGA_Sprs_array_destroy(s_a);
  NGA_Destroy(g_v);
  NGA_Destroy(g_av);

  NGA_Terminate();
  /**
   *  Tidy up after message-passing library
   */
  MP_FINALIZE();
}
