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
	screen \
	software-properties-common

# Java
RUN add-apt-repository "deb http://ppa.launchpad.net/webupd8team/java/ubuntu xenial main" && apt-get update
RUN echo debconf shared/accepted-oracle-license-v1-1 select true | sudo debconf-set-selections
RUN echo debconf shared/accepted-oracle-license-v1-1 seen true | sudo debconf-set-selections
RUN apt-get -y install oracle-java8-installer

# Adduser `meterlogger`
RUN perl -pi -e 's/^#?\%sudo\W+ALL=\(ALL\:ALL\)\W+ALL/\%sudo\tALL=\(ALL\:ALL\) NOPASSWD\: ALL/' /etc/sudoers
RUN adduser --disabled-password --gecos "" meterlogger && usermod -a -G dialout meterlogger
RUN usermod -a -G sudo meterlogger

# Create our main work directory
RUN mkdir /meterlogger
RUN chown -R meterlogger:meterlogger /meterlogger

# Crosstool demands non-root user for compilation
USER meterlogger

# esp-open-sdk
RUN cd /meterlogger && git clone --recursive https://github.com/nabovarme/esp-open-sdk.git && \
    cd esp-open-sd && git checkout sdk-v2.2.x
RUN rm -fr /meterlogger/esp-open-sdk/esp-open-lwip
RUN cd /meterlogger/esp-open-sdk && git clone https://github.com/martin-ger/esp-open-lwip.git
RUN cd /meterlogger/esp-open-sdk && make STANDALONE=y

# EspStackTraceDecoder.jar
RUN cd /meterlogger && wget https://github.com/littleyoda/EspStackTraceDecoder/releases/download/untagged-59a763238a6cedfe0362/EspStackTraceDecoder.jar

# meterlogger
RUN cd /meterlogger && git clone --recursive https://github.com/nabovarme/MeterLogger.git

USER root

# Export ENV
ENV PATH /meterlogger/esp-open-sdk/xtensa-lx106-elf/bin:$PATH
ENV XTENSA_TOOLS_ROOT /meterlogger/esp-open-sdk/xtensa-lx106-elf/bin
ENV SDK_BASE /meterlogger/esp-open-sdk/sdk

WORKDIR /meterlogger/MeterLogger
CMD cp /meterlogger/esp-open-sdk/xtensa-lx106-elf/bin/esptool.py /meterlogger/MeterLogger/tools/ && \
	cd /meterlogger/MeterLogger && \
	eval $BUILD_ENV make clean all
