/*$Id: synch.c,v 1.2 1995-02-02 23:25:57 d3g681 Exp $*/
/* $Header: /tmp/hpctools/ga/tcgmsg/ipcv4.0/synch.c,v 1.2 1995-02-02 23:25:57 d3g681 Exp $ */

#include "sndrcv.h"

#ifdef OLDSYNC
void SYNCH_(type)
     long *type;
/*
  Synchronize by forcing all process to exchange a zero length message
  of given type with process 0.
*/
{
  long me = NODEID_();
  long nproc = NNODES_();
  char *buf = "";
  long zero = 0;
  long sync = 1;
  long from, lenmes, i;

  /* First everyone sends null message to zero */

  if (me == 0)
    for (i=1; i<nproc; i++)
      RCV_(type, buf, &zero, &lenmes, &i, &from, &sync);
  else
    SND_(type, buf, &zero, &zero, &sync);

  /* Zero broadcasts message null message to everyone */

  BRDCST_(type, buf, &zero, &zero);
}
#else
/*ARGSUSED*/
void SYNCH_(type)
     long *type;
/*
  Synchronize by doing a global sum of a single integer variable
  ... as long type is unique there should be no problems.
*/
{
  long junk = 0, n = 1;

  IGOP_(type, &junk, &n, "+");
}
#endif
