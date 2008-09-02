if ENABLE_SHIPPED_ZLIB
# no indent here! - BSD make issue
noinst_LTLIBRARIES = libzlib.la
endif

libzlib_la_SOURCES = adler32.c compress.c crc32.c deflate.c \
gzio.c infback.c inffast.c inflate.c \
inftrees.c trees.c uncompr.c zutil.c

noinst_HEADERS = \
crc32.h deflate.h inffast.h \
inffixed.h inflate.h inftrees.h \
trees.h zconf.h zlib.h zutil.h 
