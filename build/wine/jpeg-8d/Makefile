
SRCDIR                = .
SUBDIRS               =
DLLS                  =
LIBS                  = libjpeg.a
EXES                  =

### Common settings

TARGETFLAGS           = -m32
CEXTRA                = $(TARGETFLAGS) -g -O2
CXXEXTRA              =
RCEXTRA               =
DEFINES               =
INCLUDE_PATH          = -I.
DLL_PATH              =
DLL_IMPORTS           =
LIBRARY_PATH          =
LIBRARIES             =

### libjpeg.a sources and settings

libjpeg_a_MODULE      = libjpeg.a
libjpeg_a_C_SRCS      = jaricom.c \
			jcapimin.c \
			jcapistd.c \
			jcarith.c \
			jccoefct.c \
			jccolor.c \
			jcdctmgr.c \
			jchuff.c \
			jcinit.c \
			jcmainct.c \
			jcmarker.c \
			jcmaster.c \
			jcomapi.c \
			jcparam.c \
			jcprepct.c \
			jcsample.c \
			jctrans.c \
			jdapimin.c \
			jdapistd.c \
			jdarith.c \
			jdatadst.c \
			jdatasrc.c \
			jdcoefct.c \
			jdcolor.c \
			jddctmgr.c \
			jdhuff.c \
			jdinput.c \
			jdmainct.c \
			jdmarker.c \
			jdmaster.c \
			jdmerge.c \
			jdpostct.c \
			jdsample.c \
			jdtrans.c \
			jerror.c \
			jfdctflt.c \
			jfdctfst.c \
			jfdctint.c \
			jidctflt.c \
			jidctfst.c \
			jidctint.c \
			jquant1.c \
			jquant2.c \
			jutils.c \
			jmemmgr.c \
			jmemnobs.c
libjpeg_a_CXX_SRCS    =
libjpeg_a_RC_SRCS     =
libjpeg_a_LDFLAGS     =
libjpeg_a_ARFLAGS     =
libjpeg_a_DLL_PATH    =
libjpeg_a_DLLS        =
libjpeg_a_LIBRARY_PATH=
libjpeg_a_LIBRARIES   =
libjpeg_a_OBJS        = $(libjpeg_a_C_SRCS:.c=.o) $(libjpeg_a_CXX_SRCS:.cpp=.o) $(libjpeg_a_RC_SRCS:.rc=.res)

### Global source lists

C_SRCS                = $(libjpeg_a_C_SRCS)
CXX_SRCS              = $(libjpeg_a_CXX_SRCS)
RC_SRCS               = $(libjpeg_a_RC_SRCS)

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

.cxx.o:
	$(CXX) -c $(CXXFLAGS) $(CXXEXTRA) $(DEFINCL) -o $@ $<

.rc.res:
	$(RC) $(RCFLAGS) $(RCEXTRA) $(DEFINCL) -fo$@ $<

# Rules for cleaning

#CLEAN_FILES     = y.tab.c y.tab.h lex.yy.c core *.orig *.rej \
#                  \\\#*\\\# *~ *% .\\\#*

clean:: $(SUBDIRS:%=%/__clean__) $(EXTRASUBDIRS:%=%/__clean__)
	$(RM) $(CLEAN_FILES) $(RC_SRCS:.rc=.res) $(C_SRCS:.c=.o) $(CXX_SRCS:.cpp=.o)
	$(RM) $(DLLS:%=%.so) $(LIBS) $(EXES) $(EXES:%=%.so)

$(SUBDIRS:%=%/__clean__): dummy
	cd `dirname $@` && $(MAKE) clean

$(EXTRASUBDIRS:%=%/__clean__): dummy
	-cd `dirname $@` && $(RM) $(CLEAN_FILES)

### Target specific build rules
DEFLIB = $(LIBRARY_PATH) $(LIBRARIES) $(DLL_PATH) $(DLL_IMPORTS:%=-l%)

$(libjpeg_a_MODULE): $(libjpeg_a_OBJS)
	$(AR) rs $@ $^

