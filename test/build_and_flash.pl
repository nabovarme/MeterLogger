#!/usr/bin/perl -w

use strict;
use Data::Dumper;

use lib qw( /etc/apache2/perl );
#use lib qw( /opt/local/apache2/perl/ );
use Nabovarme::Db;

use constant SERIAL_PORT => $ENV{SERIAL_PORT} || '/dev/ttyUSB0';
use constant DOCKER_IMAGE => 'meterlogger:latest';
use constant BUILD_COMMAND => 'docker run -t -i -v ~/esp8266/MeterLogger:/meterlogger/MeterLogger';
use constant FLASH_COMMAND => 'python ./tools/esptool.py -p ' . SERIAL_PORT . ' -b 1500000 write_flash --flash_size 8m 0xFE000 firmware/blank.bin 0xFC000 firmware/esp_init_data_default_112th_byte_0x03.bin 0x00000 firmware/0x00000.bin 0x10000 firmware/0x10000.bin 0x60000 webpages.espfs';
my $DEFAULT_BUILD_VARS = 'AP=1';

my $meter_serial = $ARGV[0] || '9999999';

# connect to db
my $dbh;
if ($dbh = Nabovarme::Db->my_connect) {
	$dbh->{'mysql_auto_reconnect'} = 1;
}

my $sth = $dbh->prepare(qq[SELECT `key`, `sw_version` FROM meters WHERE serial = ] . $dbh->quote($meter_serial) . qq[ LIMIT 1]);
$sth->execute;
if ($sth->rows) {
	$_ = $sth->fetchrow_hashref;
	my $key = $_->{key} || warn "no aes key found\n";
	my $sw_version = $_->{sw_version} || warn "no sw_version found\n";

	# parse options
	if ($_->{sw_version} =~ /NO_AUTO_CLOSE/) {
		$DEFAULT_BUILD_VARS .= ' AUTO_CLOSE=0';
	}

	if ($_->{sw_version} =~ /NO_CRON/) {
		$DEFAULT_BUILD_VARS .= ' NO_CRON=1';
	}

	if ($_->{sw_version} =~ /DEBUG_STACK_TRACE/) {
		$DEFAULT_BUILD_VARS .= ' DEBUG_STACK_TRACE=1';
	}

	if ($_->{sw_version} =~ /THERMO_ON_AC_2/) {
		$DEFAULT_BUILD_VARS .= ' THERMO_ON_AC_2=1';
	}

	if ($_->{sw_version} =~ /FLOW/) {
		$DEFAULT_BUILD_VARS .= ' FLOW_METER=1';
	}

	# parse hw models
	if ($_->{sw_version} =~ /MC-B/) {
		print BUILD_COMMAND . ' -e BUILD_ENV="' . $DEFAULT_BUILD_VARS . ' MC_66B=1' . qq[ SERIAL=$meter_serial KEY=$key] . '" ' . DOCKER_IMAGE . "\n";
		system BUILD_COMMAND . ' -e BUILD_ENV="' . $DEFAULT_BUILD_VARS . ' MC_66B=1' . qq[ SERIAL=$meter_serial KEY=$key] . '" ' . DOCKER_IMAGE;
	}
	elsif ($_->{sw_version} =~ /MC/) {
		print BUILD_COMMAND . ' -e BUILD_ENV="' . $DEFAULT_BUILD_VARS . ' EN61107=1' . qq[ SERIAL=$meter_serial KEY=$key] . '" ' . DOCKER_IMAGE . "\n";
		system BUILD_COMMAND . ' -e BUILD_ENV="' . $DEFAULT_BUILD_VARS . ' EN61107=1' . qq[ SERIAL=$meter_serial KEY=$key] . '" ' . DOCKER_IMAGE;
	}
	elsif ($_->{sw_version} =~ /IMPULSE/) {
		print BUILD_COMMAND . ' -e BUILD_ENV="' . $DEFAULT_BUILD_VARS . ' IMPULSE=1' . qq[ SERIAL=$meter_serial KEY=$key] . '" ' . DOCKER_IMAGE . "\n";
		system BUILD_COMMAND . ' -e BUILD_ENV="' . $DEFAULT_BUILD_VARS . ' IMPULSE=1' . qq[ SERIAL=$meter_serial KEY=$key] . '" ' . DOCKER_IMAGE ;
	}
	elsif ($_->{sw_version} =~ /NO_METER/) {
		print BUILD_COMMAND . ' -e BUILD_ENV="' . $DEFAULT_BUILD_VARS . ' DEBUG=1 DEBUG_NO_METER=1' . qq[ SERIAL=$meter_serial KEY=$key] . '" ' . DOCKER_IMAGE . "\n";
		system BUILD_COMMAND . ' -e BUILD_ENV="' . $DEFAULT_BUILD_VARS . ' DEBUG=1 DEBUG_NO_METER=1' . qq[ SERIAL=$meter_serial KEY=$key] . '" ' . DOCKER_IMAGE;
	}
	else {
		print BUILD_COMMAND . ' -e BUILD_ENV="' . $DEFAULT_BUILD_VARS . qq[ SERIAL=$meter_serial KEY=$key] . '" ' . DOCKER_IMAGE . "\n";
		system BUILD_COMMAND . ' -e BUILD_ENV="' . $DEFAULT_BUILD_VARS . qq[ SERIAL=$meter_serial KEY=$key] . '" ' . DOCKER_IMAGE;
	}
	print FLASH_COMMAND . "\n";
	system 'echo ' . FLASH_COMMAND . ' | pbcopy';	# copy command to clipboard for repeated use
	system FLASH_COMMAND;
}

# end of main


__END__
