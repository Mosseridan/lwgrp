/* Copyright (c) 2012, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Written by Adam Moody <moody20@llnl.gov>.
 * LLNL-CODE-568372.
 * All rights reserved.
 * This file is part of the LWGRP library.
 * For details, see https://github.com/hpc/lwgrp
 * Please also read this file: LICENSE.TXT. */

#include <stdlib.h>

#include "mpi.h"
#include "lwgrp.h"
#include "lwgrp_internal.h"

enum bin_values {
  INDEX_COUNT   = 0,
  INDEX_CLOSEST = 1,
};

/* given a specified number of bins, an index into those bins, and a
 * input group, create and return a new group consisting of all ranks
 * belonging to the same bin, runs in O(num_bins * log N) time */
int lwgrp_ring_split_bin(int num_bins, int my_bin, const lwgrp_ring* in, lwgrp_ring* out)
{
  /* With this function, we split the "in" group into up to "num_bins"
   * subgroups.  A process is grouped with all other processes that
   * specify the same value for "my_bin".  The descriptor for the new
   * group is returned in "out".  If my_bin is less than 0, an empty
   * (NULL) group is returned.
   *
   * Implementation:
   * We run two exclusive scans, one scanning from left to right, and
   * another scanning from right to left.  As the output of the
   * left-going scan, a process acquires the number of ranks to its
   * left that are in its bin as well as the rank of the process that
   * is immediately to its left that is also in its bin.  Similarly,
   * the right-going scan provides the process with the number of ranks
   * to the right and the rank of the process immediately to its right
   * that is in the same bin.  With this info, a process can determine
   * its rank and the number of ranks in its group, as well as, the
   * ranks of its left and right partners, which is sufficient to fully
   * define the "out" group. */
  int i;

  if (my_bin >= num_bins) {
    /* TODO: fail */
  }

  /* define some frequently used indicies into our arrays */
  int my_bin_index = 2 * my_bin;
  int rank_index   = 2 * num_bins;

  /* allocate space for our send and receive buffers */
  int elements = 2 * num_bins + 1;
  int* bins = (int*) lwgrp_malloc(4 * elements * sizeof(int), sizeof(int), __FILE__, __LINE__);
  if (bins == NULL) {
    /* TODO: fail */
  }

  /* set up pointers to our send and receive buffers */
  int* send_left_bins  = bins + (0 * elements);
  int* recv_left_bins  = bins + (1 * elements);
  int* send_right_bins = bins + (2 * elements);
  int* recv_right_bins = bins + (3 * elements);

  /* initialize our send buffers,
   * set all ranks to MPI_PROC_NULL and set all counts to 0 */
  for(i = 0; i < 2*num_bins; i += 2) {
    send_left_bins[i+INDEX_COUNT]    = 0;
    send_right_bins[i+INDEX_COUNT]   = 0;
    send_left_bins[i+INDEX_CLOSEST]  = MPI_PROC_NULL;
    send_right_bins[i+INDEX_CLOSEST] = MPI_PROC_NULL;
  }

  /* for the bin we are in, set the rank to our rank and set the count to 1 */
  if (my_bin >= 0) {
    send_left_bins[my_bin_index+INDEX_COUNT]    = 1;
    send_right_bins[my_bin_index+INDEX_COUNT]   = 1;
    send_left_bins[my_bin_index+INDEX_CLOSEST]  = in->comm_rank;
    send_right_bins[my_bin_index+INDEX_CLOSEST] = in->comm_rank;
  }

  /* execute double, inclusive scan, one going left-to-right,
   * and another right-to-left */
  MPI_Request request[4];
  MPI_Status  status[4];
  MPI_Comm comm  = in->comm;
  int comm_rank  = in->comm_rank;
  int left_rank  = in->comm_left;
  int right_rank = in->comm_right;
  int rank       = in->group_rank;
  int ranks      = in->group_size;
  int my_left    = MPI_PROC_NULL;
  int my_right   = MPI_PROC_NULL;
  int dist = 1;
  while (dist < ranks) {
    /* left-to-right shift:
     * inform rank to our right about the rank on our left,
     * recv data from left and send data to the right */
    send_right_bins[rank_index] = left_rank;
    MPI_Irecv(recv_left_bins,  elements, MPI_INT, left_rank,  LWGRP_MSG_TAG_0, comm, &request[0]);
    MPI_Isend(send_right_bins, elements, MPI_INT, right_rank, LWGRP_MSG_TAG_0, comm, &request[1]);

    /* right-to-left shift:
     * inform rank to our left about the rank on our right
     * recv data from right and send data to the left */
    send_left_bins[rank_index] = right_rank;
    MPI_Irecv(recv_right_bins, elements, MPI_INT, right_rank, LWGRP_MSG_TAG_0, comm, &request[2]);
    MPI_Isend(send_left_bins,  elements, MPI_INT, left_rank,  LWGRP_MSG_TAG_0, comm, &request[3]);

    /* wait for all communication to complete */
    MPI_Waitall(4, request, status);

    /* make note of the rightmost rank in our bin
     * to the left if we haven't already found one */
    if (my_left == MPI_PROC_NULL && my_bin >= 0) {
      my_left = recv_left_bins[my_bin_index+INDEX_CLOSEST];
    }

    /* make note of the leftmost rank in our bin to the
     * right if we haven't already found one */
    if (my_right == MPI_PROC_NULL && my_bin >= 0) {
      my_right = recv_right_bins[my_bin_index+INDEX_CLOSEST];
    }

    /* merge data from left into our right-going data */
    for(i = 0; i < 2*num_bins; i += 2) {
      /* if we haven't wrapped, add the counts for this bin */
      if (rank - dist >= 0) {
        send_right_bins[i+INDEX_COUNT] += recv_left_bins[i+INDEX_COUNT];
      }

      /* if we haven't already defined the rightmost rank for this bin,
       * set it to the value defined in the message from the left */
      if (send_right_bins[i+INDEX_CLOSEST] == MPI_PROC_NULL) {
        send_right_bins[i+INDEX_CLOSEST] = recv_left_bins[i+INDEX_CLOSEST];
      }
    }

    /* merge data from right into our left-going data */
    for(i = 0; i < 2*num_bins; i += 2) {
      /* if we haven't wrapped, add the counts for this bin */
      if (rank + dist < ranks) {
        send_left_bins[i+INDEX_COUNT] += recv_right_bins[i+INDEX_COUNT];
      }

      /* if we haven't already defined the leftmost rank for this bin,
       * set it to the value defined in the message from the left */
      if (send_left_bins[i+INDEX_CLOSEST] == MPI_PROC_NULL) {
        send_left_bins[i+INDEX_CLOSEST] = recv_right_bins[i+INDEX_CLOSEST];
      }
    }

    /* get next processes on the left and right sides */
    left_rank  = recv_left_bins[rank_index];
    right_rank = recv_right_bins[rank_index];
    dist <<= 1;
  }

  /* if we're the only rank, set our ourself as our left and right neighbor */
  if (ranks == 1) {
    my_left  = comm_rank;
    my_right = comm_rank;
  }

  if (my_bin >= 0) {
    /* get count of number of ranks in our bin to our left and right sides */
    int count_left  = send_right_bins[my_bin_index + INDEX_COUNT] - 1;
    int count_right = send_left_bins[my_bin_index + INDEX_COUNT]  - 1;

    /* the number of ranks to our left defines our rank, while we add
     * the number of ranks to our left with the number of ranks to our
     * right plus ourselves to get the total number of ranks in our bin */
    out->comm       = in->comm;
    out->comm_rank  = in->comm_rank;
    out->comm_left  = my_left;
    out->comm_right = my_right;
    out->group_rank = count_left;
    out->group_size = count_left + count_right + 1;
  } else {
    /* create an empty group */
    lwgrp_ring_set_null(out);
  }

  /* free memory */
  lwgrp_free(&bins);

  return LWGRP_SUCCESS; 
}

/* simply send to each process in turn in a ring fashion,
 * starting with ourself and moving to the right one rank
 * at a time */
int lwgrp_ring_alltoallv_linear(
  const void* sendbuf,
  const int sendcounts[],
  const int senddispls[],
  void* recvbuf,
  const int recvcounts[],
  const int recvdispls[],
  MPI_Datatype datatype,
  const lwgrp_ring* group)
{
  /* TODO: we could just fire off a bunch of issends */

  /* get group info */
  MPI_Comm comm = group->comm;
  int rank      = group->group_rank;
  int ranks     = group->group_size;

  /* execute the alltoall operation */
  MPI_Request request[6];
  MPI_Status status[6];
  int dist = 0;
  int src = group->comm_left;
  int dst = group->comm_right;
  int src_next, dst_next;
  while (dist < ranks) {
    /* receive data from src */
    void* recv_ptr = lwgrp_type_dtbuf_from_dtbuf(recvbuf, recvdispls[src], datatype);
    int recv_count = recvcounts[src];
    MPI_Irecv(recv_ptr, recv_count, datatype, src, LWGRP_MSG_TAG_0, comm, &request[0]);

    /* send data to dst */
    void* send_ptr = lwgrp_type_dtbuf_from_dtbuf(sendbuf, senddispls[dst], datatype);
    int send_count = sendcounts[dst];
    MPI_Isend(send_ptr, send_count, datatype, dst, LWGRP_MSG_TAG_0, comm, &request[1]);

    /* exchange addresses, send our current src to our current dst, etc */
    MPI_Irecv(&src_next, 1, MPI_INT, src, LWGRP_MSG_TAG_0, comm, &request[2]);
    MPI_Irecv(&dst_next, 1, MPI_INT, dst, LWGRP_MSG_TAG_0, comm, &request[3]);
    MPI_Isend(&src,      1, MPI_INT, dst, LWGRP_MSG_TAG_0, comm, &request[4]);
    MPI_Isend(&dst,      1, MPI_INT, src, LWGRP_MSG_TAG_0, comm, &request[5]);

    /* wait for communication to complete */
    MPI_Waitall(6, request, status);

    /* update addresses for next step */
    src = src_next;
    dst = dst_next;

    /* go on to next iteration */
    dist++;
  }
    
  return 0;
}
