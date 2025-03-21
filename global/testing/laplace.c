#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#include "macdecls.h"
#include "ga.h"
#include "mp3.h"

#define WRITE_VTK
#define CG_SOLVE 1
#define NDIM 128

/**
 *  Solve Laplace's equation on a cubic domain using the sparse matrix
 *  functionality in GA.
 */

#define MAX_FACTOR 1024
void grid_factor(int p, int xdim, int ydim, int zdim,
    int *idx, int *idy, int *idz) {
  int i, j, k; 
  int ip, ifac, pmax, prime[MAX_FACTOR];
  int fac[MAX_FACTOR];
  int ix, iy, iz, ichk;

  i = 1;
/**
 *   factor p completely
 *   first, find all prime numbers, besides 1, less than or equal to 
 *   the square root of p
 */
  ip = p;
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
 *    find three factors of p of approximately the same size
 */
  *idx = 1;
  *idy = 1;
  *idz = 1;
  for (i = ifac-1; i >= 0; i--) {
    ix = xdim/(*idx);
    iy = ydim/(*idy);
    iz = zdim/(*idz);
    if (ix >= iy && ix >= iz && ix > 1) {
      *idx = fac[i]*(*idx);
    } else if (iy >= ix && iy >= iz && iy > 1) {
      *idy = fac[i]*(*idy);
    } else if (iz >= ix && iz >= iy && iz > 1) {
      *idz = fac[i]*(*idz);
    } else {
      printf("Too many processors in grid factoring routine\n");
    }
  }
}

int main(int argc, char **argv) {
  int s_a, g_b, g_x, g_p, g_r, g_t;
  int g_s, g_v, g_rm;
  int one;
  int64_t one_64;
  int me, nproc;
  int idim, jdim, kdim;
  int64_t xdim, ydim, zdim;
  int64_t rdim, cdim, rdx, cdx;
  int64_t ldx, ldxy;
  int ipx, ipy, ipz, idx, idy, idz;
  int64_t ilo, ihi, jlo, jhi, klo, khi;
  int64_t i, j, k, ncnt;
  int  iproc, ld;
  double x, y, z, val, h, rxdim, rydim, rzdim;
  int64_t *ibuf, **iptr;
  double *vptr;
  double *vbuf;
  double t_beg, dot_time, dot_time_s;
  int ok;
  double one_r = 1.0;
  double m_one_r = -1.0;
  double ir, jr, ldr;
  double xinc_p, yinc_p, zinc_p;
  double xinc_m, yinc_m, zinc_m;
  double alpha, beta, rho, rho_m, omega, m_omega, residual;
  double rv,ts,tt;
  int nsave;
  int heap=20000000, stack=20000000;
  int iterations = 10000;
  double tol, twopi;
  FILE *PHI;
  char op[2];
  /* Intitialize a message passing library */
  one = 1;
  one_64 = 1;
  MP_INIT(argc,argv);

  /* Initialize GA */
  NGA_Initialize();

  /* Interior points of the grid run from 0 to NDIM-1, boundary points are located
   * at -1 and NDIM for each of the axes */
  idim = NDIM;
  jdim = NDIM;
  kdim = NDIM;
  xdim = NDIM;
  ydim = NDIM;
  zdim = NDIM;
  rxdim = 1.0;
  rydim = 1.0;
  rzdim = 1.0;
  h = rxdim/((double)NDIM);
  me = GA_Nodeid();
  nproc = GA_Nnodes();
  twopi = 8.0*atan(1.0);

  heap /= nproc;
  stack /= nproc;
  if(! MA_init(MT_F_DBL, stack, heap))
    GA_Error("MA_init failed",stack+heap);  /* initialize memory allocator*/

  /* factor array */
  grid_factor(nproc, idim, jdim, kdim, &ipx, &ipy, &ipz);
  if (me == 0) {
    printf("Solving Laplace's equation on %d processors\n",nproc);
    printf("\n    Using %d X %d X %d processor grid\n",ipx,ipy,ipz);
    printf("\n    Grid size is %d X %d X %d\n",idim,jdim,kdim);
  }
  /* figure out process location in proc grid */
  i = me;
  idx = me%ipx;
  i = (i-idx)/ipx;
  idy = i%ipy;
  idz = (i-idy)/ipy;
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
  klo = (zdim*idz)/ipz;
  if (idz < ipz-1) {
    khi = (zdim*(idz+1))/ipz-1;
  } else {
    khi = zdim-1;
  }
 
  /* create sparse array */
  rdim = xdim*ydim*zdim;
  cdim = xdim*ydim*zdim;
  ldx = xdim;
  ldxy = xdim*ydim;
  s_a = NGA_Sprs_array_create64(rdim, cdim, C_DBL);
  ncnt = 0;
  /* Set elements of Laplace operator. Use a global indexing scheme and don't
   * worry about setting elements locally. Count up values associated with
   * boundaries */
  for (i=ilo; i<=ihi; i++) {
    if (i == 0) {
      xinc_m = 2.0;
      xinc_p = 1.0;
    } else if (i == NDIM - 1) {
      xinc_m = 1.0;
      xinc_p = 2.0;
    } else {
      xinc_p = 1.0;
      xinc_m = 1.0;
    }
    for (j=jlo; j<=jhi; j++) {
      if (j == 0) {
        yinc_m = 2.0;
        yinc_p = 1.0;
      } else if (j == NDIM - 1) {
        yinc_m = 1.0;
        yinc_p = 2.0;
      } else {
        yinc_p = 1.0;
        yinc_m = 1.0;
      }
      for (k=klo; k<=khi; k++) {
        if (k == 0) {
          zinc_m = 2.0;
          zinc_p = 1.0;
        } else if (k == NDIM - 1) {
          zinc_m = 1.0;
          zinc_p = 2.0;
        } else {
          zinc_p = 1.0;
          zinc_m = 1.0;
        }
        rdx = i + j*ldx + k*ldxy;
        val = -(xinc_p+xinc_m+yinc_p+yinc_m+zinc_p+zinc_m)/(h*h);
        NGA_Sprs_array_add_element64(s_a,rdx,rdx,&val);
        if (i+1 < xdim) {
          cdx = i+1 + j*ldx + k*ldxy;
          val = xinc_p/(h*h);
          NGA_Sprs_array_add_element64(s_a,rdx,cdx,&val);
        } else {
          ncnt++;
        }
        if (i-1 >= 0) {
          cdx = i-1 + j*ldx + k*ldxy;
          val = xinc_m/(h*h);
          NGA_Sprs_array_add_element64(s_a,rdx,cdx,&val);
        } else {
          ncnt++;
        }
        if (j+1 < ydim) {
          cdx = i + (j+1)*ldx + k*ldxy;
          val = yinc_p/(h*h);
          NGA_Sprs_array_add_element64(s_a,rdx,cdx,&val);
        } else {
          ncnt++;
        }
        if (j-1 >= 0) {
          cdx = i + (j-1)*ldx + k*ldxy;
          val = yinc_m/(h*h);
          NGA_Sprs_array_add_element64(s_a,rdx,cdx,&val);
        } else {
          ncnt++;
        }
        if (k+1 < zdim) {
          cdx = i + j*ldx + (k+1)*ldxy;
          val = zinc_p/(h*h);
          NGA_Sprs_array_add_element64(s_a,rdx,cdx,&val);
        } else {
          ncnt++;
        }
        if (k-1 >= 0) {
          cdx = i + j*ldx + (k-1)*ldxy;
          val = zinc_m/(h*h);
          NGA_Sprs_array_add_element64(s_a,rdx,cdx,&val);
        } else {
          ncnt++;
        }
      }
    }
  }
  if (NGA_Sprs_array_assemble(s_a) && me == 0) {
    printf("\n    Sparse array assembly completed\n");
  }

  /* Construct RHS vector. Assume points on boundary are given by
   * the equation f(x,y,z) = cos(twopi*x) + cos(twopi*y) + cos(twopi*z) */
  ibuf = (int64_t*)malloc(ncnt*sizeof(int64_t));
  iptr = (int64_t**)malloc(ncnt*sizeof(int64_t*));
  vbuf = (double*)malloc(ncnt*sizeof(double));
  for (i=0; i<ncnt; i++) {
    iptr[i] = ibuf+i;
  }
  nsave = ncnt;
  ncnt = 0;
  /* Evaluate contributions for faces parallel to xy plane */
  if (klo == 0) {
    for (i=ilo; i<=ihi; i++) {
      for (j=jlo; j<=jhi; j++) {
        x = ((double)i+0.5)*h;
        y = ((double)j+0.5)*h;
        z = 0.0;
        vbuf[ncnt] = -2.0*(cos(twopi*x)+cos(twopi*y)+cos(twopi*z))/(h*h);
        ibuf[ncnt] = i + j*ldx;
        ncnt++;
      }
    }
  }
  if (khi == zdim-1) {
    for (i=ilo; i<=ihi; i++) {
      for (j=jlo; j<=jhi; j++) {
        x = ((double)i+0.5)*h;
        y = ((double)j+0.5)*h;
        z = 1.0;
        vbuf[ncnt] = -2.0*(cos(twopi*x)+cos(twopi*y)+cos(twopi*z))/(h*h);
        ibuf[ncnt] = i + j*ldx + (zdim-1)*ldxy;
        ncnt++;
      }
    }
  }
  /* Evaluate contributions for faces parallel to xz plane */
  if (jlo == 0) {
    for (i=ilo; i<=ihi; i++) {
      for (k=klo; k<=khi; k++) {
        x = ((double)i+0.5)*h;
        y = 0.0;
        z = ((double)k+0.5)*h;
        vbuf[ncnt] = -2.0*(cos(twopi*x)+cos(twopi*y)+cos(twopi*z))/(h*h);
        ibuf[ncnt] = i + k*ldxy;
        ncnt++;
      }
    }
  }
  if (jhi == ydim-1) {
    for (i=ilo; i<=ihi; i++) {
      for (k=klo; k<=khi; k++) {
        x = ((double)i+0.5)*h;
        y = 1.0;
        z = ((double)k+0.5)*h;
        vbuf[ncnt] = -2.0*(cos(twopi*x)+cos(twopi*y)+cos(twopi*z))/(h*h);
        ibuf[ncnt] = i + (ydim-1)*ldx + k*ldxy;
        ncnt++;
      }
    }
  }
  /* Evaluate contributions for faces parallel to yz plane */
  if (ilo == 0) {
    for (j=jlo; j<=jhi; j++) {
      for (k=klo; k<=khi; k++) {
        x = 0.0;
        y = ((double)j+0.5)*h;
        z = ((double)k+0.5)*h;
        vbuf[ncnt] = -2.0*(cos(twopi*x)+cos(twopi*y)+cos(twopi*z))/(h*h);
        ibuf[ncnt] = j*ldx + k*ldxy;
        ncnt++;
      }
    }
  }
  if (ihi == xdim-1) {
    for (j=jlo; j<=jhi; j++) {
      for (k=klo; k<=khi; k++) {
        x = 1.0;
        y = ((double)j+0.5)*h;
        z = ((double)k+0.5)*h;
        vbuf[ncnt] = -2.0*(cos(twopi*x)+cos(twopi*y)+cos(twopi*z))/(h*h);
        ibuf[ncnt] = (xdim-1) + j*ldx + k*ldxy;
        ncnt++;
      }
    }
  }

  /* allocate global array representing right hand side vector */
  g_b = NGA_Create_handle();
  NGA_Set_data64(g_b,one,&cdim,C_DBL);
  NGA_Allocate(g_b);
  GA_Zero(g_b);
  NGA_Scatter_acc64(g_b,vbuf,iptr,ncnt,&one_r);
  GA_Sync();
  free(ibuf);
  free(iptr);
  free(vbuf);
#if CG_SOLVE
  g_x = GA_Duplicate(g_b, "dup_x");
  g_r = GA_Duplicate(g_b, "dup_r");
  g_p = GA_Duplicate(g_b, "dup_p");
  g_t = GA_Duplicate(g_b, "dup_t");
  /* accumulate boundary values to right hand side vector */
  if (me == 0) {
    printf("\nRight hand side vector completed. Starting\n");
    printf("conjugate gradient iterations.\n\n");
  }
  dot_time = 0;

  /* Solve Laplace's equation using conjugate gradient method */
  one_r = 1.0;
  m_one_r = -1.0;
  GA_Zero(g_x);
  /* Initial guess is zero, so Ax = 0 and r = b */
  GA_Copy(g_b, g_r);
  GA_Copy(g_r, g_p);
  t_beg = GA_Wtime();
  residual = GA_Ddot(g_r,g_r);
  dot_time += GA_Wtime()-t_beg;
  /* GA_Norm_infinity(g_r, &tol); */
  tol = sqrt(residual);
  ncnt = 0;
  /* Start iteration loop */
  while (tol > 1.0e-5 && ncnt < iterations) {
    if (me==0) printf("Iteration: %d Tolerance: %e\n",(int)ncnt+1,tol);
    GA_Mask_sync(0,0);
    NGA_Sprs_array_matvec_multiply(s_a, g_p, g_t);
    t_beg = GA_Wtime();
    alpha = GA_Ddot(g_t,g_p);
    dot_time += GA_Wtime()-t_beg;
    alpha = residual/alpha;
    GA_Mask_sync(0,0);
    GA_Add(&one_r,g_x,&alpha,g_p,g_x);
    alpha = -alpha;
    GA_Mask_sync(0,0);
    GA_Add(&one_r,g_r,&alpha,g_t,g_r);
    /*GA_Norm_infinity(g_r, &tol);*/
    beta = residual;
    t_beg = GA_Wtime();
    residual = GA_Ddot(g_r,g_r);
    dot_time += GA_Wtime()-t_beg;
    tol = sqrt(residual);
    beta = residual/beta;
    GA_Mask_sync(0,0);
    GA_Add(&one_r,g_r,&beta,g_p,g_p); 
    ncnt++;
  }

  /* Evaluate time for dot products if no calculation takes place */
  t_beg = GA_Wtime();
  for (i=0; i<ncnt; i++) {
    alpha = GA_Ddot(g_t,g_p);
    residual = GA_Ddot(g_r,g_r);
  }
  dot_time_s = GA_Wtime()-t_beg;
  /* average time in dot product across processors */
  op[0] = '+';
  op[1] = '\0';
  GA_Dgop(&dot_time,1,op);
  GA_Dgop(&dot_time_s,1,op);
  dot_time /= ((double)nproc);
  dot_time_s /= ((double)nproc);
  if (me == 0) {
    printf("Time in dot products in CG algorithm: %f\n",dot_time);
    printf("Time in dot products in loop: %f\n",dot_time_s);
  }

  /*
  if (me==0) printf("RHS Vector\n");
  GA_Print(g_b);
  if (me==0) printf("Solution Vector\n");
  GA_Print(g_x);
  */
  
  if (ncnt == iterations) {
    if (me==0) printf("Solution failed to converge\n");
  } else {
    if (me==0) printf("Solution converged\n");
  }
  NGA_Destroy(g_r);
  NGA_Destroy(g_p);
  NGA_Destroy(g_t);
#else
  g_x = GA_Duplicate(g_b, "dup_x");
  g_r = GA_Duplicate(g_b, "dup_r");
  g_rm = GA_Duplicate(g_b, "dup_rm");
  g_p = GA_Duplicate(g_b, "dup_p");
  g_v = GA_Duplicate(g_b, "dup_v");
  g_s = GA_Duplicate(g_b, "dup_s");
  g_t = GA_Duplicate(g_b, "dup_t");
  /* accumulate boundary values to right hand side vector */
  if (me == 0) {
    printf("\nRight hand side vector completed. Starting\n");
    printf("BiCG-STAB iterations.\n\n");
  }

  /* Solve Laplace's equation using conjugate gradient method */
  one_r = 1.0;
  m_one_r = -1.0;
  GA_Zero(g_x);
  /* Initial guess is zero, so Ax = 0 and r = b */
  GA_Copy(g_b, g_r);
  GA_Copy(g_b, g_rm);
  ncnt = 0;
  GA_Norm_infinity(g_r, &tol);
  /* Start iteration loop */
  while (tol > 1.0e-5 && ncnt < iterations) {
    if (me==0) printf("Iteration: %d Tolerance: %e\n",(int)ncnt+1,tol);
    rho = GA_Ddot(g_r,g_rm);
    if (rho == 0.0) {
      GA_Error("BiCG-STAB method fails",0);
    }
    if (ncnt == 0) {
      GA_Copy(g_rm,g_p);
    } else {
      beta = (rho/rho_m)*(alpha/omega);
      m_omega = -omega;
      GA_Add(&one_r,g_p,&m_omega,g_v,g_p);
      GA_Add(&one_r,g_rm,&beta,g_p,g_p);
    }
    NGA_Sprs_array_matvec_multiply(s_a, g_p, g_v);
    rv = GA_Ddot(g_r,g_v);
    alpha = -rho/rv;
    GA_Add(&one_r,g_rm,&alpha,g_v,g_s);
    alpha = -alpha;
    GA_Norm_infinity(g_s, &tol);
    if (tol < 1.0e-05) {
      GA_Add(&one_r,g_x,&alpha,g_p,g_x);
    }
    NGA_Sprs_array_matvec_multiply(s_a, g_s, g_t);
    ts = GA_Ddot(g_t,g_s);
    tt = GA_Ddot(g_t,g_t);
    omega = ts/tt;
    m_omega = -omega;
    GA_Add(&one_r,g_x,&alpha,g_p,g_x);
    GA_Add(&one_r,g_x,&omega,g_s,g_x);
    GA_Add(&one_r,g_s,&m_omega,g_t,g_rm);
    GA_Norm_infinity(g_rm, &tol);
    if (tol < 1.0e-05) break;
    if (omega == 0) {
      GA_Error("BiCG-STAB method cannot continue",0);
    }
    ncnt++;
    rho_m = rho;
  }
  NGA_Destroy(g_r);
  NGA_Destroy(g_rm);
  NGA_Destroy(g_p);
  NGA_Destroy(g_v);
  NGA_Destroy(g_s);
  NGA_Destroy(g_t);
#endif

  /* Write solution to file */
#ifdef WRITE_VTK
  if (me == 0) {
    vbuf = (double*)malloc(xdim*ydim*sizeof(double));
    PHI = fopen("phi.vtk","w");
    fprintf(PHI,"# vtk DataFile Version 3.0\n");
    fprintf(PHI,"Laplace Equation Solution\n");
    fprintf(PHI,"ASCII\n");
    fprintf(PHI,"DATASET STRUCTURED_POINTS\n");
    fprintf(PHI,"DIMENSIONS %ld %ld %ld\n",xdim,ydim,zdim);
    fprintf(PHI,"ORIGIN %12.6f %12.6f %12.6f\n",0.5*h,0.5*h,0.5*h);
    fprintf(PHI,"SPACING %12.6f %12.6f %12.6f\n",h,h,h);
    fprintf(PHI," \n");    
    fprintf(PHI,"POINT_DATA %ld\n",xdim*ydim*zdim);
    fprintf(PHI,"SCALARS Phi float\n");
    fprintf(PHI,"LOOKUP_TABLE default\n");
    for (k=0; k<zdim; k++) {
      ilo = k*xdim*ydim;
      ihi = ilo + xdim*ydim - 1;
      NGA_Get64(g_x,&ilo,&ihi,vbuf,&one_64);
      for (j=0; j<ydim; j++) {
        for (i=0; i<xdim; i++) {
          fprintf(PHI," %12.6f",vbuf[i+j*xdim]);
          if (i%5 == 0) fprintf(PHI,"\n");
        }
        if ((xdim-1)%5 != 0) fprintf(PHI,"\n");
      }
    }
    fclose(PHI);
    free(vbuf);
  }
#endif

  NGA_Sprs_array_destroy(s_a);
  NGA_Destroy(g_b);
  NGA_Destroy(g_x);

  NGA_Terminate();
  /**
   *  Tidy up after message-passing library
   */
  MP_FINALIZE();
}
