# Minimal Makefile: Mesa 7.8.2 OSMesa (classic swrast) for AmigaOS 4.1 PPC, newlib.
# Run inside walkero/amigagccondocker:os4-gcc11 (see build.sh).

MESA   ?= Mesa-7.8.2
OUT    ?= out
OBJDIR ?= obj

CC     = ppc-amigaos-gcc
AR     = ppc-amigaos-ar
RANLIB = ppc-amigaos-ranlib

# NOTE: plain -mcpu=440 makes this GCC 11 emit SOFT-float code (the 440
# core has no FPU), which then fails to link against the hard-float
# libgcc/newlib ("uses hard float, ... uses soft float"). The sam460ex's
# 460EX has an FPU, so use -mcpu=440fp (== -mcpu=440 -mhard-float).
CPU    ?= -mcpu=440fp
CFLAGS ?= -mcrt=newlib -O2 $(CPU) -D__USE_INLINE__ \
          -DUSE_EXTERNAL_DXTN_LIB=0 -DNDEBUG \
          -fno-strict-aliasing -Wall -Wno-unused -Wno-sign-compare -Wno-array-parameter
INCLUDES = -I$(MESA)/include -I$(MESA)/src/mesa -I$(MESA)/src/mesa/main

M = $(MESA)/src/mesa
# Mesa's own source lists (MAIN_SOURCES, MATH_SOURCES, ...)
include $(M)/sources.mak

# Everything a classic software driver needs, minus asm helpers and the
# GLSL compiler (slang), which is replaced by shims/slang_stub.c.
SRCS_REL = $(MAIN_SOURCES) $(MATH_SOURCES) $(MATH_XFORM_SOURCES) \
           $(VBO_SOURCES) $(TNL_SOURCES) $(SHADER_SOURCES) \
           $(SWRAST_SOURCES) $(SWRAST_SETUP_SOURCES) \
           $(COMMON_DRIVER_SOURCES) $(GLAPI_SOURCES) \
           drivers/osmesa/osmesa.c

MESA_OBJS = $(addprefix $(OBJDIR)/,$(SRCS_REL:.c=.o))
SHIM_SRCS = $(wildcard shims/*.c)
SHIM_OBJS = $(addprefix $(OBJDIR)/,$(SHIM_SRCS:.c=.o))

LIB = $(OUT)/lib/libOSMesa.a

all: $(LIB) headers

$(OBJDIR)/%.o: $(M)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR)/shims/%.o: shims/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(LIB): $(MESA_OBJS) $(SHIM_OBJS)
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) rcs $@ $^
	$(RANLIB) $@

headers:
	mkdir -p $(OUT)/include/GL
	cp $(MESA)/include/GL/gl.h $(MESA)/include/GL/glext.h \
	   $(MESA)/include/GL/osmesa.h $(OUT)/include/GL/

TEST = test/osmesa_test
test: $(TEST)
$(TEST): test/osmesa_test.c $(LIB) headers
	$(CC) -mcrt=newlib -O2 $(CPU) -Wall -I$(OUT)/include $< -o $@ \
	    -L$(OUT)/lib -lOSMesa -lm -lauto

clean:
	rm -rf $(OBJDIR) $(OUT) $(TEST)

.PHONY: all headers test clean
