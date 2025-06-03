package Nabovarme::Db;

use strict;
use warnings;
use DBI;
use Sys::Syslog;
use Config::Simple;

use constant CONFIG_FILE => qw (/etc/Nabovarme.conf );

my $config = new Config::Simple(CONFIG_FILE) || die $!;

my $dbi = 'DBI:mysql:database=' . $config->param('db_name') . ';host=' . $config->param('db_host') . ';port=' . $config->param('db_port');

sub my_connect {
  return DBI->connect($dbi, $config->param('db_username'), $config->param('db_password'), { mysql_auto_reconnect => 1, mysql_enable_utf8 => 1 }) || warn $!;
}
        
1;
