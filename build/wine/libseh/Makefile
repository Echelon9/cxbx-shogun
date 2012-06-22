
SRCDIR                = .
SUBDIRS               =
DLLS                  =
LIBS                  = libseh.a
EXES                  =

### Common settings

TARGETFLAGS           = -m32
CEXTRA                = $(TARGETFLAGS) -g -O2 -Wall
CXXEXTRA              = $(CEXTRA)
ASEXTRA               = $(CEXTRA) -xassembler-with-cpp
RCEXTRA               =
DEFINES               = -DLIBSEH_BUILD
INCLUDE_PATH          =
DLL_PATH              =
DLL_IMPORTS           =
LIBRARY_PATH          =
LIBRARIES             =

### libseh.a sources and settings

libseh_a_MODULE       = libseh.a
libseh_a_C_SRCS       = common/common.c \
			os/windows/seh-support.c
libseh_a_CXX_SRCS     = common/sehpp.cpp
libseh_a_S_SRCS       = os/windows/arch/x86/seh.s
libseh_a_RC_SRCS      =
libseh_a_LDFLAGS      =
libseh_a_ARFLAGS      =
libseh_a_DLL_PATH     =
libseh_a_DLLS         =
libseh_a_LIBRARY_PATH =
libseh_a_LIBRARIES    =
libseh_a_OBJS         = $(libseh_a_C_SRCS:.c=.o) $(libseh_a_CXX_SRCS:.cpp=.o) $(libseh_a_S_SRCS:.s=.o) $(libseh_a_RC_SRCS:.rc=.res)

### Global source lists

C_SRCS                = $(libseh_a_C_SRCS)
CXX_SRCS              = $(libseh_a_CXX_SRCS)
S_SRCS                = $(libseh_a_S_SRCS)
RC_SRCS               = $(libseh_a_RC_SRCS)

### Tools

WINEBUILD             = winebuild
CC                    = winegcc
CXX                   = wineg++
RC                    = wrc
AR                    = ar

### Generic targets

all: $(SUBDIRS) $(DLLS:%=%.so) $(LIBS) $(EXES)

### Build rules

.PHONY: all clean dummy

$(SUBDIRS): dummy
	@cd $@ && $(MAKE)

# Implicit rules

.SUFFIXES: .cpp .cxx .rc .res
DEFINCL = $(INCLUDE_PATH) $(DEFINES) $(OPTIONS)

.c.o:
	$(CC) -c $(CFLAGS) $(CEXTRA) $(DEFINCL) -o $@ $<

.cpp.o:
	$(CXX) -c $(CXXFLAGS) $(CXXEXTRA) $(DEFINCL) -o $@ $<

.s.o:
	$(CC) -c $(ASFLAGS) $(ASEXTRA) $(DEFINCL) -o $@ $<

.cxx.o:
	$(CXX) -c $(CXXFLAGS) $(CXXEXTRA) $(DEFINCL) -o $@ $<

.rc.res:
	$(RC) $(RCFLAGS) $(RCEXTRA) $(DEFINCL) -fo$@ $<

# Rules for cleaning

#CLEAN_FILES     = y.tab.c y.tab.h lex.yy.c core *.orig *.rej \
#                  \\\#*\\\# *~ *% .\\\#*

clean:: $(SUBDIRS:%=%/__clean__) $(EXTRASUBDIRS:%=%/__clean__)
	$(RM) $(CLEAN_FILES) $(RC_SRCS:.rc=.res) $(C_SRCS:.c=.o) $(CXX_SRCS:.cpp=.o) $(S_SRCS:.s=.o)
	$(RM) $(DLLS:%=%.so) $(LIBS) $(EXES) $(EXES:%=%.so)

$(SUBDIRS:%=%/__clean__): dummy
	cd `dirname $@` && $(MAKE) clean

$(EXTRASUBDIRS:%=%/__clean__): dummy
	-cd `dirname $@` && $(RM) $(CLEAN_FILES)

### Target specific build rules
DEFLIB = $(LIBRARY_PATH) $(LIBRARIES) $(DLL_PATH) $(DLL_IMPORTS:%=-l%)

$(libseh_a_MODULE): $(libseh_a_OBJS)
	$(AR) rs $@ $^

