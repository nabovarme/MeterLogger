#! /opt/local/bin/perl -w

use strict;
use Data::Dumper;
use Data::Hexdumper;
use Device::SerialPort;
#use Digest::CRC;
use Time::HiRes  qw( usleep );

my $port_obj;
my $res;
my ($c, $s);

$port_obj = new Device::SerialPort('/dev/tty.usbserial-A6YNEE07') || die "$!\n";
$port_obj->databits(7);
$port_obj->stopbits(2);
$port_obj->parity('even');

while (1) {
	get_serial();
	get_data();
	sleep 10;
}

sub get_serial {
	my $serial = 0;

	$port_obj->baudrate(300);
	$port_obj->write("/#2\r");
	usleep(150_000);	# wait for transmit
	$port_obj->baudrate(1200);
	$res = '';
	do {
		($c, $s) = $port_obj->read(1);
		if ($c) {
			$res .= $s;
		}
	} while (ord($s) != 0x0d);
	#warn "\n";
	#warn hexdump(data => $res, suppress_warnings => 1);
		
	$port_obj->baudrate(300);
	$port_obj->write("/MP1\r");
	usleep(200_000);	# wait for transmit
	$port_obj->baudrate(300);
	$res = '';
	do {
		($c, $s) = $port_obj->read(1);
		if ($c) {
			$res .= $s;
		}
	} while (ord($s) != 0x0d);
	#warn "\n";
	#warn hexdump(data => $res, suppress_warnings => 1);
	
	$port_obj->baudrate(1200);
	$port_obj->write("M32\r");
	usleep(200_000);
	$port_obj->baudrate(1200);
	$res = '';
	do {
		($c, $s) = $port_obj->read(1);
		if ($c) {
			$res .= $s;
		}
	} while (ord($s) != 0x0d);
	#warn "\n";
	#warn hexdump(data => $res, suppress_warnings => 1);
	
	# get serial
	$port_obj->baudrate(2400);
	$port_obj->write("M200654\r");
	usleep(50_000);
	$port_obj->baudrate(2400);
	$res = '';
	do {
		($c, $s) = $port_obj->read(1);
		if ($c) {
			$res .= $s;
		}
	} while (ord($s) != 0x0d);
	$res =~ s/^\@//;
	$serial = $res;
	#warn "\n";
	#warn hexdump(data => $serial, suppress_warnings => 1);
	
	$port_obj->baudrate(2400);
	$port_obj->write("M200655\r");
	usleep(50_000);
	$port_obj->baudrate(2400);
	$res = '';
	do {
		($c, $s) = $port_obj->read(1);
		if ($c) {
			$res .= $s;
		}
	} while (ord($s) != 0x0d);
	$res =~ s/^\@//;
	$serial += $res << 8;
	#warn "\n";
	#warn hexdump(data => $serial, suppress_warnings => 1);
	
	$port_obj->baudrate(2400);
	$port_obj->write("M200656\r");
	usleep(50_000);
	$port_obj->baudrate(2400);
	$res = '';
	do {
		($c, $s) = $port_obj->read(1);
		if ($c) {
			$res .= $s;
		}
	} while (ord($s) != 0x0d);
	$res =~ s/^\@//;
	$serial += $res << 16;
	#warn "\n";
	#warn hexdump(data => $serial, suppress_warnings => 1);
	
	$port_obj->baudrate(2400);
	$port_obj->write("M200657\r");
	usleep(50_000);
	$port_obj->baudrate(2400);
	$res = '';
	do {
		($c, $s) = $port_obj->read(1);
		if ($c) {
			$res .= $s;
		}
	} while (ord($s) != 0x0d);
	$res =~ s/^\@//;
	$serial += $res << 24;
	#warn "\n";
	#warn hexdump(data => $serial, suppress_warnings => 1);
	
	$port_obj->baudrate(2400);
	$port_obj->write("M200658\r");
	usleep(50_000);
	$port_obj->baudrate(2400);
	$res = '';
	do {
		($c, $s) = $port_obj->read(1);
		if ($c) {
			$res .= $s;
		}
	} while (ord($s) != 0x0d);
	$res =~ s/^\@//;
	$serial += $res << 32;
	#warn "\n";
	#warn hexdump(data => $serial, suppress_warnings => 1);
	
	$port_obj->baudrate(2400);
	$port_obj->write("M200659\r");
	usleep(50_000);
	$port_obj->baudrate(2400);
	$res = '';
	do {
		($c, $s) = $port_obj->read(1);
		if ($c) {
			$res .= $s;
		}
	} while (ord($s) != 0x0d);
	$res =~ s/^\@//;
	$serial += $res << 40;
	warn "\n";
	#warn hexdump(data => $serial, suppress_warnings => 1);
	
	print "$serial\n";

	$port_obj->baudrate(2400);
	$port_obj->write("M906540659\r");
	usleep(50_000);
	$port_obj->baudrate(2400);
	$res = '';
	do {
		($c, $s) = $port_obj->read(1);
		if ($c) {
			$res .= $s;
		}
	} while (ord($s) != 0x0d);

	$port_obj->baudrate(2400);
	$port_obj->write("*\r");
	usleep(50_000);
	$port_obj->baudrate(2400);
	$res = '';
	do {
		($c, $s) = $port_obj->read(1);
		if ($c) {
			$res .= $s;
		}
	} while (ord($s) != 0x0d);
}

sub get_data {
	# send EN 61 107 request
	$port_obj->baudrate(300);
	$port_obj->write("/?!\r\n");
	usleep(200_000);
	
	# receive
	$res = '';
	do {
		($c, $s) = $port_obj->read(1);
		if ($c) {
			$res .= $s;
	#		warn hexdump(data => $res, suppress_warnings => 1);
		}
	} while (ord($s) != 0x03);
	#} while (! ($res =~ /!\r\n..$/));
	#warn Dumper($res);
	warn hexdump(data => $res, suppress_warnings => 1);
	
	# get T3
	usleep(300_000);
	
	# send
	$port_obj->write('/#C');
	usleep(200_000);
	
	# receive
	$port_obj->baudrate(1200);
	$res = '';
	do {
		($c, $s) = $port_obj->read(1);
		if ($c) {
			$res .= $s;
		}
	#} while (1);
	} while (ord($s) != 0x0d);
	
	warn "\n";
	warn hexdump(data => $res, suppress_warnings => 1);
	$res =~ /\d+ (\d+)/;
	print $1/100 . "\n";
}

1;