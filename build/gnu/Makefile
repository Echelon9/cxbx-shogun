# ******************************************************************
# *
# *    .,-:::::    .,::      .::::::::.    .,::      .:
# *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
# *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
# *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
# *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
# *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
# *
# *   Cxbx->Makefile
# *
# *  This file is part of the Cxbx project.
# *
# *  Cxbx and Cxbe are free software; you can redistribute them
# *  and/or modify them under the terms of the GNU General Public
# *  License as published by the Free Software Foundation; either
# *  version 2 of the license, or (at your option) any later version.
# *
# *  This program is distributed in the hope that it will be useful,
# *  but WITHOUT ANY WARRANTY; without even the implied warranty of
# *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# *  GNU General Public License for more details.
# *
# *  You should have recieved a copy of the GNU General Public License
# *  along with this program; see the file COPYING.
# *  If not, write to the Free Software Foundation, Inc.,
# *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
# *
# *  (c) 2002-2003 Aaron Robinson <caustik@caustik.com>
# *
# *  All rights reserved
# *
# ******************************************************************
# *  File Authors: Aaron Robinson, Edgar Hucek
# ******************************************************************
CC     ?= gcc
CXX    ?= g++
CFLAGS  = -g -O2 -Wall -I../../src -I../../src/Common/Linux -I../..src/Linux

OBJECTS	= ../../src/Common/Error.o \
	../../src/Common/Exe.o \
	../../src/Common/Xbe.o \
	../../src/Common/OpenXDK.o \
	../../src/Cxbe/Main.o

RESOURCES = 

# target:
all	: cxbe

clean	:
	${RM} *.o *~ core *.core ${OBJECTS} ${RESOURCES}
	${RM} *.o *~ core *.core ${OBJECTS} ${RESOURCES} cxbe

cxbe : ${OBJECTS} ${RESOURCES}
	${CXX} ${OBJECTS} ${RESOURCES} -o $@

### rules:

%.o	: %.cpp
	${CXX} ${CFLAGS} -o $@ -c $<

%.o	: %.c
	${CC} ${CFLAGS} -o $@ -c $<

%.o	: %.S
	${CC} -DASSEMBLER ${CFLAGS} -o $@ -c $<
