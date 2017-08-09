#!/usr/bin/perl -w

use strict;
use Data::Dumper;

use lib qw( /etc/apache2/perl );
#use lib qw( /opt/local/apache2/perl/ );
use Nabovarme::Db;

use constant DEFAULT_BUILD_VARS => 'AP=1';
use constant BUILD_TARGETS => 'clean all flashall';

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
                
	if ($_->{sw_version} =~ /MC-B/) {
		print DEFAULT_BUILD_VARS . ' MC_66B=1' . " SERIAL=$meter_serial KEY=$key" . ' make ' . BUILD_TARGETS . "\n";
		exec DEFAULT_BUILD_VARS . ' MC_66B=1' . " SERIAL=$meter_serial KEY=$key" . ' make ' . BUILD_TARGETS . "\n";
	}
	elsif ($_->{sw_version} =~ /MC/) {
		print DEFAULT_BUILD_VARS . ' EN61107=1' . " SERIAL=$meter_serial KEY=$key" . ' make ' . BUILD_TARGETS . "\n";
		exec DEFAULT_BUILD_VARS . ' EN61107=1' . " SERIAL=$meter_serial KEY=$key" . ' make ' . BUILD_TARGETS . "\n";
	}
	elsif ($_->{sw_version} =~ /IMPULSE/) {
		print DEFAULT_BUILD_VARS . ' IMPULSE=1' . " SERIAL=$meter_serial KEY=$key" . ' make ' . BUILD_TARGETS . "\n";
		exec DEFAULT_BUILD_VARS . ' IMPULSE=1' . " SERIAL=$meter_serial KEY=$key" . ' make ' . BUILD_TARGETS . "\n";
	}
	elsif ($_->{sw_version} =~ /NO_METER/) {
		print DEFAULT_BUILD_VARS . ' DEBUG=1 DEBUG_NO_METER=1' . " SERIAL=$meter_serial KEY=$key" . ' make ' . BUILD_TARGETS . ' screen' . "\n";
		exec DEFAULT_BUILD_VARS . ' DEBUG=1 DEBUG_NO_METER=1' . " SERIAL=$meter_serial KEY=$key" . ' make ' . BUILD_TARGETS . ' screen' . "\n";
	}
	else {
		print DEFAULT_BUILD_VARS . " SERIAL=$meter_serial KEY=$key" . ' make ' . BUILD_TARGETS . "\n";
		exec DEFAULT_BUILD_VARS . " SERIAL=$meter_serial KEY=$key" . ' make ' . BUILD_TARGETS . "\n";
	}
}

# end of main


__END__
