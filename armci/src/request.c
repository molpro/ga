/* $Id: request.c,v 1.18 2000-10-20 18:22:40 d3h325 Exp $ */
#include "armcip.h"
#include "request.h"
#include "memlock.h"
#include "copy.h"
#include <stdio.h>

#define DEBUG_ 0

#if !defined(GM) && !defined(VIA) 
  double _armci_snd_buf[MSG_BUFLEN_DBL], _armci_rcv_buf[MSG_BUFLEN_DBL];
  char* MessageRcvBuffer = (char*)_armci_rcv_buf;
  char* MessageSndBuffer = (char*)_armci_snd_buf;
#endif

#define ADDBUF(buf,type,val) *(type*)(buf) = (val); (buf) += sizeof(type)
#define GETBUF(buf,type,var) (var) = *(type*)(buf); (buf) += sizeof(type)



/*\ send request to server to LOCK MUTEX
\*/
void armci_rem_lock(int mutex, int proc, int *ticket)      
{
request_header_t *msginfo = (request_header_t*)MessageSndBuffer;
 
    GET_SEND_BUFFER;

    msginfo->datalen = msginfo->dscrlen= 0;
    msginfo->from  = armci_me;
    msginfo->to    = proc;
    msginfo->operation = LOCK;
    msginfo->format  = mutex;

    msginfo->bytes = msginfo->datalen + msginfo->dscrlen;

    armci_send_req(proc);

    msginfo->datalen = sizeof(int); /* receive ticket from server */

    *ticket = *(int*)armci_rcv_data(proc);
    
    if(DEBUG_)fprintf(stderr,"%d receiving ticket %d\n",armci_me, *ticket);
}


void armci_server_lock(request_header_t *msginfo)
{
    int mutex = msginfo->format;
    int proc  = msginfo->from;
    int ticket;

    /* acquire lock on behalf of requesting process */
    ticket = armci_server_lock_mutex(mutex, proc, msginfo->tag);
    
    if(ticket >-1){
       /* got lock */
       msginfo->datalen = sizeof(int);
       armci_send_data(msginfo, &ticket);
    }
}
       
    


/*\ send request to server to UNLOCK MUTEX
\*/
void armci_rem_unlock(int mutex, int proc, int ticket)
{
request_header_t *msginfo = (request_header_t*)MessageSndBuffer;
char *buf = (char*)(msginfo+1);

    GET_SEND_BUFFER;

    msginfo->dscrlen = msginfo->bytes = sizeof(ticket); 
    msginfo->datalen = 0; 
    msginfo->from  = armci_me;
    msginfo->to    = proc;
    msginfo->operation = UNLOCK;
    msginfo->format  = mutex;

    ADDBUF(buf, int, ticket);

    if(DEBUG_)fprintf(stderr,"%d sending unlock\n",armci_me);
    armci_send_req(proc);
}
    


/*\ server unlocks mutex and passes lock to the next waiting process
\*/
void armci_server_unlock(request_header_t *msginfo, char* dscr)
{
    int ticket = *(int*)dscr;
    int mutex  = msginfo->format;
    int proc   = msginfo->to;
    int waiting;
    
    waiting = armci_server_unlock_mutex(mutex,proc,ticket,&msginfo->tag);

    if(waiting >-1){ /* -1 means that nobody is waiting */

       ticket++;
       /* pass ticket to the waiting process */
       msginfo->from = waiting;
       msginfo->datalen = sizeof(ticket);
       armci_send_data(msginfo, &ticket);

    }
}


void armci_unlock_waiting_process(msg_tag_t tag, int proc, int ticket)
{
request_header_t header;
request_header_t *msginfo = &header;

       msginfo->datalen = sizeof(int);
       msginfo->tag     = tag;
       msginfo->from      = proc;
       msginfo->to    = armci_me;
       armci_send_data(msginfo, &ticket); 
}


/*\ send RMW request to server
\*/
void armci_rem_rmw(int op, int *ploc, int *prem, int extra, int proc)
{
request_header_t *msginfo = (request_header_t*)MessageSndBuffer;
char *buf = (char*)(msginfo+1);
void *buffer;
 
    GET_SEND_BUFFER;

    msginfo->dscrlen = sizeof(void*);
    msginfo->from  = armci_me;
    msginfo->to    = proc; 
    msginfo->format  = msginfo->operation = op;
    msginfo->datalen = sizeof(int); /* extra */

    ADDBUF(buf, void*, prem); /* pointer is shipped as descriptor */

    /* data field: extra argument in fetch&add and local value in swap */
    if(op==ARMCI_SWAP){
       ADDBUF(buf, int, *ploc); 
    }else if(op==ARMCI_SWAP_LONG) {
       ADDBUF(buf, long, *((long*)ploc) ); 
       msginfo->datalen = sizeof(long);
    }else {
      ADDBUF(buf, int, extra);
    }

    msginfo->bytes   = msginfo->datalen+msginfo->dscrlen ;

    armci_send_req(proc);

    /* need to adjust datalen for long datatype version */
    if(op==ARMCI_FETCH_AND_ADD || op== ARMCI_SWAP)
        msginfo->datalen = sizeof(int);
    else
        msginfo->datalen = sizeof(long);

    buffer = armci_rcv_data(proc);  /* receive response */

    if(op==ARMCI_FETCH_AND_ADD || op== ARMCI_SWAP)
        *ploc = *(int*)buffer;
    else
        *(long*)ploc = *(long*)buffer;
    

}


/*\ server response to RMW 
\*/
void armci_server_rmw(request_header_t* msginfo,void* ptr, void* pextra)
{
     long lold;
     int iold;
     void *pold=0;
     int op = msginfo->operation;

     if(DEBUG_){
        printf("%d server: executing RMW from %d\n",armci_me,msginfo->from);
        fflush(stdout);
     }

     /* for swap operations *pextra has the  value to swap
      * for fetc&add it carries the increment argument
      */

     switch(op){
     case ARMCI_SWAP:
        iold = *(int*) pextra;
     case ARMCI_FETCH_AND_ADD:
        if(msginfo->datalen != sizeof(int))
          armci_die("armci_server_rmw: bad datalen=",msginfo->datalen);
        pold = &iold;
        msginfo->datalen = sizeof(int);
        break;

     case ARMCI_SWAP_LONG:
        lold = *(long*) pextra;
     case ARMCI_FETCH_AND_ADD_LONG:
        if(msginfo->datalen != sizeof(int))
          armci_die("armci_server_rmw: long bad datalen=",msginfo->datalen);
        pold = &lold;
        msginfo->datalen = sizeof(long);
        break;

     default:
          armci_die("armci_server_rmw: bad operation code=",op);
     }

     armci_generic_rmw(op, pold, *(int**)ptr, *(int*) pextra, msginfo->to);

     armci_send_data(msginfo, pold);
}


int armci_rem_vector(int op, void *scale, armci_giov_t darr[],int len,int proc)
{
    char *buf = MessageSndBuffer;
    request_header_t *msginfo = (request_header_t*)MessageSndBuffer;
    int bytes =0, s, slen=0;
    void *rem_ptr;
    size_t adr;

    GET_SEND_BUFFER;

    /* fill vector descriptor */
    buf += sizeof(request_header_t);
    ADDBUF(buf,int,len); /* number of sets */
    for(s=0;s<len;s++){

        bytes += darr[s].ptr_array_len * darr[s].bytes;
        ADDBUF(buf,int,darr[s].ptr_array_len); /* number of elements */
        ADDBUF(buf,int,darr[s].bytes);         /* sizeof element */

        if(op == GET) rem_ptr = darr[s].src_ptr_array;   
        else rem_ptr = darr[s].dst_ptr_array;        
        armci_copy(rem_ptr,buf, darr[s].ptr_array_len * sizeof(void*)); 
        buf += darr[s].ptr_array_len*sizeof(void*);
    }

    /* align buf for doubles (8-bytes) before copying data */
    adr = (size_t)buf;
    adr >>=3;
    adr <<=3;
    adr +=8;
    buf = (char*)adr;

    /* fill message header */
    msginfo->dscrlen = buf - MessageSndBuffer - sizeof(request_header_t);
    /*printf("VECTOR len=%d dscrlen=%d\n",len, msginfo->dscrlen);*/
    msginfo->from  = armci_me;
    msginfo->to    = proc;
    msginfo->operation  = op;
    msginfo->format  = VECTOR;

    msginfo->datalen = bytes;

    /* put scale for accumulate */
    switch(op){
    case ARMCI_ACC_INT:
               *(int*)buf = *(int*)scale; slen= sizeof(int); break;
    case ARMCI_ACC_DCP:
               ((double*)buf)[0] = ((double*)scale)[0];
               ((double*)buf)[1] = ((double*)scale)[1];
               slen=2*sizeof(double);break;
    case ARMCI_ACC_DBL:
               *(double*)buf = *(double*)scale; slen = sizeof(double); break;
    case ARMCI_ACC_CPL:
               ((float*)buf)[0] = ((float*)scale)[0];
               ((float*)buf)[1] = ((float*)scale)[1];
               slen=2*sizeof(float);break; 
    case ARMCI_ACC_FLT:
               *(float*)buf = *(float*)scale; slen = sizeof(float); break;
    default: slen=0;
    }
    buf += slen;
    msginfo->datalen += slen;
    msginfo->bytes = msginfo->datalen+msginfo->dscrlen;

    /* for put and accumulate copy data into buffer */
    if(op != GET){
/*       fprintf(stderr,"sending %lf\n",*(double*)darr[0].src_ptr_array[0]);*/

       armci_vector_to_buf(darr, len, buf);
/*       fprintf(stderr,"sending first=%lf last =%lf in buffer\n",*/
/*                     *((double*)buf),((double*)buf)[99]);*/
    }

    armci_send_req(proc);

    if(op == GET){
        armci_rcv_vector_data(proc, MessageSndBuffer, darr, len);
        
    }
    return 0;
}



/*\ client version of remote strided operation
\*/
int armci_rem_strided(int op, void* scale, int proc,
                       void *src_ptr, int src_stride_arr[],
                       void* dst_ptr, int dst_stride_arr[],
                       int count[], int stride_levels, int flag)
{
    char *buf = MessageSndBuffer;
    request_header_t *msginfo = (request_header_t*)MessageSndBuffer;
    int  i;
    size_t adr;
    int slen=0;
    void *rem_ptr;
    int  *rem_stride_arr;

    GET_SEND_BUFFER;

    if(op == GET){
       rem_ptr = src_ptr;
       rem_stride_arr = src_stride_arr;
    }else{
       rem_ptr = dst_ptr;
       rem_stride_arr = dst_stride_arr;
    }
    
    for(i=0, msginfo->datalen=1;i<=stride_levels;i++)msginfo->datalen*=count[i];

    /* fill strided descriptor */
                                       buf += sizeof(request_header_t);
    *(void**)buf = rem_ptr;            buf += sizeof(void*);
    *(int*)buf = stride_levels;        buf += sizeof(int);
    for(i=0;i<stride_levels;i++)((int*)buf)[i] = rem_stride_arr[i];
                                       buf += stride_levels*sizeof(int);
    for(i=0;i< stride_levels+1;i++)((int*)buf)[i] = count[i];
                                       buf += (1+stride_levels)*sizeof(int);

#   ifdef CLIENT_BUF_BYPASS
      if(flag && armci_gm_bypass){
         /* to bypass the client MessageSnd buffer in get we need to add source
            pointer and stride info - server will put data directly there */
         ADDBUF(buf,void*,dst_ptr);
         for(i=0;i<stride_levels;i++)((int*)buf)[i] = dst_stride_arr[i];
                                       buf += stride_levels*sizeof(int);
         msginfo->bypass=1;
      }else msginfo->bypass=0;
#   endif


    /* align buf for doubles (8-bytes) before copying data */
    adr = (size_t)buf;
    adr >>=3; 
    adr <<=3;
    adr +=8; 
    buf = (char*)adr;

    /* fill message header */
    msginfo->from  = armci_me;
    msginfo->to    = proc;
    msginfo->operation  = op;
    msginfo->format  = STRIDED;

    /* put scale for accumulate */
    switch(op){
    case ARMCI_ACC_INT: 
               *(int*)buf = *(int*)scale; slen= sizeof(int); break;
    case ARMCI_ACC_DCP:
               ((double*)buf)[0] = ((double*)scale)[0];
               ((double*)buf)[1] = ((double*)scale)[1];
               slen=2*sizeof(double);break; 
    case ARMCI_ACC_DBL: 
               *(double*)buf = *(double*)scale; slen = sizeof(double); break;
    case ARMCI_ACC_CPL:
               ((float*)buf)[0] = ((float*)scale)[0];
               ((float*)buf)[1] = ((float*)scale)[1];
               slen=2*sizeof(float);break; 
    case ARMCI_ACC_FLT:
               *(float*)buf = *(float*)scale; slen = sizeof(float); break;
    default: slen=0;
    }
	
	/*
	if(ACC(op)) fprintf(stderr,"%d in client len=%d alpha=%lf)\n",
	             armci_me, buf - (char*)msginfo , ((double*)buf)[0]); 
	*/

    buf += slen;
    msginfo->dscrlen = buf - MessageSndBuffer - sizeof(request_header_t);
    msginfo->bytes = msginfo->datalen+msginfo->dscrlen;

    if(op == GET){
#      ifdef CLIENT_BUF_BYPASS
         if(msginfo->bypass){

             if(!armci_pin_memory(dst_ptr,dst_stride_arr,count, stride_levels))
                                         return 1; /* failed:cannot do bypass */
             armci_send_req(proc);
             armci_rcv_strided_data_bypass(proc, msginfo->datalen,
                                           dst_ptr, stride_levels);
             armci_unpin_memory(dst_ptr,dst_stride_arr,count, stride_levels);

         }else
#      endif             
       {
          armci_send_req(proc);
          armci_rcv_strided_data(proc, MessageSndBuffer, msginfo->datalen,
                                 dst_ptr, stride_levels, dst_stride_arr, count);
       }

    } else{
       /* for put and accumulate send data */
       armci_send_strided(proc,msginfo, buf, 
                          src_ptr, stride_levels, src_stride_arr, count); 
    }

    return 0;
}




void armci_server(request_header_t *msginfo, char *dscr, char* buf, int buflen)
{
    int  buf_stride_arr[MAX_STRIDE_LEVEL+1];
    int  *loc_stride_arr,slen; 
    int  *count, stride_levels;
    void *buf_ptr, *loc_ptr;
    void *scale;
    char *dscr_save = dscr;
    int  rc, i,proc;
#   ifdef CLIENT_BUF_BYPASS
      int  *client_stride_arr=0; 
      void *client_ptr=0;
#   endif

    /* unpack descriptor record */
    loc_ptr = *(void**)dscr;           dscr += sizeof(void*);
    stride_levels = *(int*)dscr;       dscr += sizeof(int);
    loc_stride_arr = (int*)dscr;       dscr += stride_levels*sizeof(int);
    count = (int*)dscr;                

    /* compute stride array for buffer */
    buf_stride_arr[0]=count[0];
    for(i=0; i< stride_levels; i++)
        buf_stride_arr[i+1]= buf_stride_arr[i]*count[i+1];

#   ifdef CLIENT_BUF_BYPASS
       if(msginfo->bypass){
          dscr += (1+stride_levels)*sizeof(int); /* move past count */
          GETBUF(dscr,void*,client_ptr);
          client_stride_arr = (int*)dscr; dscr += stride_levels*sizeof(int);
        }
#   endif

    /* get scale for accumulate, adjust buf to point to data */
    switch(msginfo->operation){
    case ARMCI_ACC_INT:     slen = sizeof(int); break;
    case ARMCI_ACC_DCP:     slen = 2*sizeof(double); break;
    case ARMCI_ACC_DBL:     slen = sizeof(double); break;
    case ARMCI_ACC_CPL:     slen = 2*sizeof(float); break;
    case ARMCI_ACC_FLT:     slen = sizeof(float); break;
	default:				slen=0;
    }

	scale = dscr_save+ (msginfo->dscrlen - slen);
/*
    if(ACC(msginfo->operation))
      fprintf(stderr,"%d in server len=%d slen=%d alpha=%lf\n", armci_me,
				 msginfo->dscrlen, slen, *(double*)scale); 
*/

    buf_ptr = buf; /*  data in buffer */

    proc = msginfo->to;

    if(msginfo->operation == GET){
    
#      ifdef CLIENT_BUF_BYPASS
         if(msginfo->bypass)
             armci_send_strided_data_bypass(proc, msginfo, buf, buflen,
                       loc_ptr, loc_stride_arr, 
                       client_ptr, client_stride_arr, count, stride_levels);

         else
#      endif

       armci_send_strided_data(proc, msginfo, buf,
                               loc_ptr, stride_levels, loc_stride_arr, count); 

    } else{

       if((rc = armci_op_strided(msginfo->operation, scale, proc,
               buf_ptr, buf_stride_arr, loc_ptr, loc_stride_arr,
               count, stride_levels, 1)))
               armci_die("server_strided: op from buf failed",rc);
    }
}



void armci_server_vector( request_header_t *msginfo, 
                          char *dscr, char* buf, int buflen)
{
    int  len,proc;
    void *scale;
    int  i,s;
    char *sbuf = buf;

    /* unpack descriptor record */
    GETBUF(dscr, int, len);
    
    /* get scale for accumulate, adjust buf to point to data */
    scale = buf;
    switch(msginfo->operation){
    case ARMCI_ACC_INT:     buf += sizeof(int); break;
    case ARMCI_ACC_DCP:     buf += 2*sizeof(double); break;
    case ARMCI_ACC_DBL:     buf += sizeof(double); break;
    case ARMCI_ACC_CPL:     buf += 2*sizeof(float); break;
    case ARMCI_ACC_FLT:     buf += sizeof(float); break;
    }


    proc = msginfo->to;

    /*fprintf(stderr,"scale=%lf\n",*(double*)scale);*/
    /* execute the operation */

    switch(msginfo->operation) {
    case GET:
 
      for(i = 0; i< len; i++){
        int parlen, bytes;
        void **ptr;
        GETBUF(dscr, int, parlen);
        GETBUF(dscr, int, bytes);
/*        fprintf(stderr,"len=%d bytes=%d parlen=%d\n",len,bytes,parlen);*/
        ptr = (void**)dscr; dscr += parlen*sizeof(char*);
        for(s=0; s< parlen; s++){
          armci_copy(ptr[s], buf, bytes);
          buf += bytes;
        }
      }
    
/*      fprintf(stderr,"server sending buffer %lf\n",*(double*)sbuf);*/
      armci_send_data(msginfo, sbuf);
      break;

    case PUT:

/*    fprintf(stderr,"received in buffer %lf\n",*(double*)buf);*/
      for(i = 0; i< len; i++){
        int parlen, bytes;
        void **ptr;
        GETBUF(dscr, int, parlen);
        GETBUF(dscr, int, bytes);
        ptr = (void**)dscr; dscr += parlen*sizeof(char*);
        for(s=0; s< parlen; s++){
          armci_copy(buf, ptr[s], bytes);
          buf += bytes;
        }
      }
      break;

     default:

      /* this should be accumulate */
      if(!ACC(msginfo->operation))
               armci_die("v server: wrong op code",msginfo->operation);

/*      fprintf(stderr,"received first=%lf last =%lf in buffer\n",*/
/*                     *((double*)buf),((double*)buf)[99]);*/

      for(i = 0; i< len; i++){
        int parlen, bytes;
        void **ptr;
        GETBUF(dscr, int, parlen);
        GETBUF(dscr, int, bytes);
        ptr = (void**)dscr; dscr += parlen*sizeof(char*);
        armci_lockmem_scatter(ptr, parlen, bytes, proc); 
        for(s=0; s< parlen; s++){
          armci_acc_2D(msginfo->operation, scale, proc, buf, ptr[s],
                       bytes, 1, bytes, bytes, 0);
          buf += bytes;
        }
        ARMCI_UNLOCKMEM(proc);
      }
    }
}
