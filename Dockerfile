#
# uIota Dockerfile
#
# The resulting image will contain everything needed to build uIota FW.
#
# Setup: (only needed once per Dockerfile change)
# 1. install docker, add yourself to docker group, enable docker, relogin
# 2. # docker build -t uiota-build .
#
# Usage:
# 3. cd to riot root
# 4. # docker run -i -t -u $UID -v $(pwd):/data/riotbuild uiota-build ./dist/tools/compile_test/compile_test.py


FROM debian:jessie

MAINTAINER Kristoffer Ek <stoffer@skulp.net>

# unrar is non-free
RUN "echo" "deb http://http.us.debian.org/debian jessie non-free" >> /etc/apt/sources.list

RUN apt-get update && apt-get install -y \
	aptitude \
	autoconf \
	automake \
	bash \
	bison \
	bzip2 \
	flex \
	g++ \
	gawk \
	gcc \
	git \
	gperf \
	help2man \
	joe \
	libexpat-dev \
	libtool \
	libtool-bin \
	make \
	ncurses-dev \
	nano \
	python \
	python-dev \
	python-serial \
	sed \
	texinfo \
	unrar \
	unzip \
	vim \
	wget \
	splint \
	sudo \
	screen

# Adduser `meterlogger`
RUN perl -pi -e 's/^#?\%sudo\W+ALL=\(ALL\:ALL\)\W+ALL/\%sudo\tALL=\(ALL\:ALL\) NOPASSWD\: ALL/' /etc/sudoers
RUN adduser --disabled-password --gecos "" meterlogger && usermod -a -G dialout meterlogger
RUN usermod -a -G sudo meterlogger

# Create our main work directory
RUN mkdir /meterlogger

# My heart belongs to daddy
RUN chown -R meterlogger:meterlogger /meterlogger

# Crosstool demands non-root user for compilation
USER meterlogger

# esp-open-sdk
RUN cd /meterlogger && git clone --recursive https://github.com/nabovarme/esp-open-sdk.git
RUN rm -fr /meterlogger/esp-open-sdk/esp-open-lwip
RUN cd /meterlogger/esp-open-sdk && git clone https://github.com/martin-ger/esp-open-lwip.git
RUN cd /meterlogger/esp-open-sdk && make STANDALONE=y

# meterlogger
RUN cd /meterlogger/ && git clone --recursive https://github.com/nabovarme/MeterLogger.git

USER root

# Export ENV
ENV PATH /meterlogger/esp-open-sdk/xtensa-lx106-elf/bin:$PATH
ENV XTENSA_TOOLS_ROOT /meterlogger/esp-open-sdk/xtensa-lx106-elf/bin
ENV SDK_BASE /meterlogger/esp-open-sdk/sdk

CMD (cd /meterlogger/MeterLogger && AP=1 DEBUG=1 DEBUG_NO_METER=1 SERIAL=$BUILD_SERIAL KEY=$BUILD_KEY make clean all)
