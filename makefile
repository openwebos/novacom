#/* @@@LICENSE
#*
#*      Copyright (c) 2008-2013 LG Electronics, Inc.
#*
#* Licensed under the Apache License, Version 2.0 (the "License");
#* you may not use this file except in compliance with the License.
#* You may obtain a copy of the License at
#*
#* http://www.apache.org/licenses/LICENSE-2.0
#*
#* Unless required by applicable law or agreed to in writing, software
#* distributed under the License is distributed on an "AS IS" BASIS,
#* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#* See the License for the specific language governing permissions and
#* limitations under the License.
#*
#* LICENSE@@@ */
TARGET := novacom
BUILDDIR := build-$(TARGET)

# pull in the build version from OE if it's set
ifneq ($(BUILDVERSION),)
BUILDVERSION := \"$(TARGET)-$(BUILDVERSION)\"
else
BUILDVERSION ?= \"..local..$(shell whoami)@$(shell hostname)..$(shell date +%Y%m%d..%H:%M:%S)\"
endif

# overriding build version if it is building off of phoenix
ifneq ($(NC_SUBMISSION_NUMBER),)
BUILDVERSION := \"novacom-$(NC_SUBMISSION_NUMBER)\"
endif


# compiler flags, default libs to link against
CFLAGS := -Wall -W -Wno-multichar -Wno-unused-parameter -Wno-unused-function -g -DBUILDVERSION=$(BUILDVERSION)
CPPFLAGS := -g
ASMFLAGS :=
LDFLAGS := 
LDLIBS := 

UNAME := $(shell uname -s)
ARCH := $(shell uname -m)

# switch any platform specific stuff here
# ifeq ($(findstring CYGWIN,$(UNAME)),CYGWIN)
# ...
# endif

ifeq ($(UNAME),Linux)
LDFLAGS += -Wl,-rpath,. -lpthread # add the local path to the program's search path
endif


OBJS := \
	src/main.o \
	src/packet.o \
	src/tcprelay.o \
	src/socket.o \
	src/base64.o \
	src/sha1.o

OBJS := $(addprefix $(BUILDDIR)/,$(OBJS))

DEPS := $(OBJS:.o=.d)

$(BUILDDIR)/$(TARGET):  $(OBJS)
	@echo linking $@
	@$(CC) $(LDFLAGS) $(OBJS) -o $@ $(LDLIBS)

.PHONY: clean
clean:
	rm -f $(OBJS) $(DEPS) $(BUILDDIR)/$(TARGET)

.PHONY: spotless
spotless:
	rm -rf build-*

# makes sure the target dir exists
MKDIR = if [ ! -d $(dir $@) ]; then mkdir -p $(dir $@); fi

$(BUILDDIR)/%.o: %.c
	@$(MKDIR)
	@echo compiling $(CFLAGS) $<
	@$(CC) $(CFLAGS) -c $< -MD -MT $@ -MF $(@:%o=%d) -o $@

$(BUILDDIR)/%.o: %.cpp
	@$(MKDIR)
	@echo compiling $<
	@$(CC) $(CPPFLAGS) -c $< -MD -MT $@ -MF $(@:%o=%d) -o $@

$(BUILDDIR)/%.o: %.S
	@$(MKDIR)
	@echo compiling $<
	@$(CC) $(ASMFLAGS) -c $< -MD -MT $@ -MF $(@:%o=%d) -o $@

ifeq ($(filter $(MAKECMDGOALS), clean), )
-include $(DEPS)
endif
