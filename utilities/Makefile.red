#BHEADER***********************************************************************
# (c) 1997   The Regents of the University of California
#
# See the file COPYRIGHT_and_DISCLAIMER for a complete copyright
# notice, contact person, and disclaimer.
#
# $Revision$
#EHEADER***********************************************************************

CC = cicc
F77 = ci77

CFLAGS = -O -DHYPRE_TIMING -DTIMER_USE_MPI -DTIMER_NO_SYS

FFLAGS = -O

MEMORY_EXTRA_FILES =

include Makefile.generic

