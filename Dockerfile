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


FROM debian:stretch

MAINTAINER Kristoffer Ek <stoffer@skulp.net>

# unrar is non-free
RUN echo "deb http://http.us.debian.org/debian stretch non-free" >> /etc/apt/sources.list

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
	gnupg \
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
RUN apt-key adv --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys EA8CACC073C3DB2A
#RUN echo "deb http://ppa.launchpad.net/linuxuprising/java/ubuntu bionic main" > /etc/apt/sources.list.d/linuxuprising-java.list && apt-get update
#RUN echo oracle-java15-installer shared/accepted-oracle-license-v1-2 select true | sudo /usr/bin/debconf-set-selections
#RUN echo oracle-java15-installer shared/accepted-oracle-licence-v1-2 boolean true | sudo /usr/bin/debconf-set-selections
#RUN apt-get install -y --allow-unauthenticated oracle-java16-set-default

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
    cd /meterlogger/esp-open-sdk && git checkout sdk-v3.1.0-dev
RUN rm -fr /meterlogger/esp-open-sdk/esp-open-lwip
RUN cd /meterlogger/esp-open-sdk && git clone https://github.com/nabovarme/esp-open-lwip.git && \
    cd /meterlogger/esp-open-sdk/esp-open-lwip && git checkout no_igmp_mdns
RUN cd /meterlogger/esp-open-sdk && \
    perl -pi -e 's/2\.1\.0/2\.4\.8/' crosstool-NG/config/companion_libs/expat.in && \
    perl -pi -e 's/2_1_0/2_4_8/' crosstool-NG/config/companion_libs/expat.in && \
    perl -pi -e 's/http:\/\/isl\.gforge\.inria\.fr/https:\/\/master\.dl\.sourceforge\.net\/project\/libisl/' crosstool-NG/scripts/build/companion_libs/121-isl.sh && \
    make STANDALONE=y

# EspStackTraceDecoder.jar
RUN cd /meterlogger && wget https://github.com/littleyoda/EspStackTraceDecoder/releases/download/untagged-59a763238a6cedfe0362/EspStackTraceDecoder.jar

# meterlogger
RUN cd /meterlogger && git clone --recursive https://github.com/nabovarme/MeterLogger.git && \
    cd /meterlogger/MeterLogger && git checkout master
RUN cd /tmp && git clone https://github.com/espressif/esptool.git

USER root

# Export ENV
ENV PATH /meterlogger/esp-open-sdk/xtensa-lx106-elf/bin:$PATH
ENV XTENSA_TOOLS_ROOT /meterlogger/esp-open-sdk/xtensa-lx106-elf/bin
ENV SDK_BASE /meterlogger/esp-open-sdk/sdk

WORKDIR /meterlogger/MeterLogger
CMD cp /tmp/esptool/esptool.py /meterlogger/MeterLogger/tools/ && \
	cd /meterlogger/MeterLogger && \
	eval $BUILD_ENV make clean all
