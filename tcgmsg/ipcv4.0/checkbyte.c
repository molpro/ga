/*$Id: checkbyte.c,v 1.2 1995-02-02 23:24:53 d3g681 Exp $*/
/* $Header: /tmp/hpctools/ga/tcgmsg/ipcv4.0/checkbyte.c,v 1.2 1995-02-02 23:24:53 d3g681 Exp $ */

unsigned char CheckByte(c, n)
    unsigned char *c;
    long n;
{
/*
  unsigned char sum = (char) 0;
  while (n-- > 0)
    sum  = sum ^ *c++;

  return sum;
*/

  unsigned int sum = 0;
  unsigned int mask = 0xff;

  while (n-- > 0)
    sum += (int) *c++;

  sum = (sum + (sum>>8) + (sum>>16) + (sum>>24)) & mask;
  return (unsigned char) sum;
}
