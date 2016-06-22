#!/usr/bin/perl -w

use strict;
use Data::Dumper;
use Net::MQTT::Simple;
use Crypt::Mode::CBC;
use Digest::SHA qw( sha256 hmac_sha256 );

#use lib qw( /etc/apache2/perl );
use lib qw( /opt/local/apache2/perl/ );
use Nabovarme::Db;

my $m = Crypt::Mode::CBC->new('AES');

my $protocol_version;
my $unix_time;
my $meter_serial = $ARGV[0] || '9999999';
my $mtqq_cmd = $ARGV[1] || "version\0\0\0";
my $message = $ARGV[2] || '';
my $hmac_sha256_hash;

my $topic = '/config/v2/' . $meter_serial . '/' . $mtqq_cmd;

my $iv = pack('H*', 'd560acc9e7f41d0980018556678c8b74');

#my $mqtt = Net::MQTT::Simple->new(q[127.0.0.1]);
my $mqtt = Net::MQTT::Simple->new(q[10.8.0.84]);

# connect to db
my $dbh;
if ($dbh = Nabovarme::Db->my_connect) {
	$dbh->{'mysql_auto_reconnect'} = 1;
}

my $sth = $dbh->prepare(qq[SELECT `key` FROM meters WHERE serial = ] . $dbh->quote($meter_serial) . qq[ LIMIT 1]);
$sth->execute;
if ($sth->rows) {
	my $key = $sth->fetchrow_hashref->{key} || warn "no aes key found";
	my $sha256 = sha256(pack('H*', $key));
	my $aes_key = substr($sha256, 0, 16);
	my $hmac_sha256_key = substr($sha256, 16, 16);

	print Dumper $topic . $message;
	$message = $m->encrypt($message, $aes_key, $iv);
	$message = $iv . $message;
	
	$hmac_sha256_hash = hmac_sha256($topic . $message, $hmac_sha256_key);
	warn Dumper({	"meter serial" => $meter_serial,
					"message" => unpack('H*', $message),
					"aes key" => unpack('H*', $aes_key),
					"iv" => unpack('H*', $iv),
					"hmac sha256 key" => unpack('H*', $hmac_sha256_key),
					"hmac_sha256_hash" => unpack('H*', $hmac_sha256_hash)
				});
	$mqtt->publish($topic => $hmac_sha256_hash . $message);
}

# end of main


__END__
