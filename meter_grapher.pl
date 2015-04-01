#!/usr/bin/perl -w

use strict;
use Data::Dumper;
use Sys::Syslog;
use Net::MQTT::Simple;
use RRDTool::OO;
use DBI;

use lib qw( /opt/local/apache2/perl/ );
use Nabovarme::Db;

#use constant RRD_FILE => "/home/stoffer/nabovarme/nabovarme.rrd";
use constant RRD_FILE => "/tmp/nabovarme.rrd";

#use constant IMG_ROOT => '/var/www/isp.skulp.net/nabovarme';
use constant IMG_ROOT => '/tmp/';
use constant HOUR_IMG => 'hour.png';
use constant DAY_IMG => 'day.png';
use constant WEEK_IMG => 'week.png';
use constant MONTH_IMG => 'month.png';
use constant YEAR_IMG => 'year.png';

use constant HOUR_ENERGY_IMG => 'hour_energy.png';
use constant DAY_ENERGY_IMG => 'day_energy.png';
use constant WEEK_ENERGY_IMG => 'week_energy.png';
use constant MONTH_ENERGY_IMG => 'month_energy.png';
use constant YEAR_ENERGY_IMG => 'year_energy.png';

openlog($0, "ndelay,pid", "local0");
syslog('info', "starting...");

my $unix_time;
my $mqtt = Net::MQTT::Simple->new(q[loppen.christiania.org]);
my $mqtt_data = undef;
#my $mqtt_count = 0;

my $rrd = RRDTool::OO->new(file => RRD_FILE);

# create rrd file
if (! -e RRD_FILE) {
	$rrd->create(
		step => 60,
		data_source => {
			name => 'flow_temp',
			type => 'GAUGE',
			min => 0,
			max => 150
		},
		data_source => {
			name => 'return_flow_temp',
			type => 'GAUGE',
			min => 0,
			max => 150
		},
		data_source => {
			name => 'temp_diff',
			type => 'GAUGE',
			min => 0,
			max => 150
		},
		data_source => {
			name => 'flow',
			type => 'GAUGE',
			min => 0,
		},
		data_source => {
			name => 'effect',
			type => 'GAUGE',
			min => 0,
		},
		data_source => {
			name => 'hours',
			type => 'GAUGE',
			min => 0
		},
		data_source => {
			name => 'volume',
			type => 'GAUGE',
			min => 0
		},
		data_source => {
			name => 'energy',
			type => 'GAUGE',
			min => 0
		},
		archive => {
			rows => 1440
		}
	);
}

# connect to db
my $dbh;
if($dbh = Nabovarme::Db->my_connect) {
	$dbh->{'mysql_auto_reconnect'} = 1;
	syslog('info', "connected to db");
}
else {
	syslog('info', "cant't connect to db $!");
	die $!;
}

# start mqtt run loop
$mqtt->run(q[/sample/#] => \&mqtt_handler);

# end of main
sub mqtt_handler {
	my ($topic, $message) = @_;

	unless ($topic =~ m!/sample/(\d+)!) {
		return;
	}
	$unix_time = $1;
	
	# parse message
	$message =~ s/&$//;
	
	my ($key, $value, $unit);
	my @key_value_list = split(/&/, $message);
	warn Dumper @key_value_list;
	my $key_value; 
	foreach $key_value (@key_value_list) {
		if (($key, $value, $unit) = $key_value =~ /([^=]*)=(\S+)(\s+(.*))?/) {
			$mqtt_data->{$key} = $value;
		}
	}
	warn Dumper($mqtt_data);
	
	# save to db
	if ($unix_time < time() + 7200) {
		my $sth = $dbh->prepare(qq[INSERT INTO `samples` (
			`serial`,
			`flow_temp`,
			`return_flow_temp`,
			`temp_diff`,
			`flow`,
			`effect`,
			`hours`,
			`volume`,
			`energy`
			) VALUES (] . 
			$dbh->quote($mqtt_data->{serial}) . ',' . 
			$dbh->quote($mqtt_data->{t1}) . ',' . 
			$dbh->quote($mqtt_data->{t2}) . ',' . 
			$dbh->quote($mqtt_data->{tdif}) . ',' . 
			$dbh->quote($mqtt_data->{flow1}) . ',' . 
			$dbh->quote($mqtt_data->{effect1}) . ',' . 
			$dbh->quote($mqtt_data->{hr}) . ',' . 
			$dbh->quote($mqtt_data->{v1}) . ',' . 
			$dbh->quote($mqtt_data->{e1}) . qq[)]);
		$sth->execute || syslog('info', "can't log to db");
		$sth->finish;
	}
	
	# handle mqtt
	warn "unix_time: $unix_time last rrd unix time: ". $rrd->last . " diff: " . ($unix_time - ($rrd->last)) . " \n \n";
	if (($rrd->last < $unix_time + 5) && ($unix_time < time() + 7200)) {
		# update rrd
		$rrd->update(
			time => $unix_time, 
			values => {
				flow_temp			=>  $mqtt_data->{t1},
				return_flow_temp	=>  $mqtt_data->{t2},
				temp_diff			=>  $mqtt_data->{tdif},
				flow				=>  $mqtt_data->{flow1},
				effect				=>  $mqtt_data->{effect1},
				hours				=>  $mqtt_data->{hr},
				volume				=>  $mqtt_data->{v1},
				energy				=>  $mqtt_data->{e1}
			}
		);
		# graphs for all data
		$rrd->graph(
			title => 'Meter graph hourly',
			image => IMG_ROOT . '/' . HOUR_IMG,
			vertical_label => '',
			width => 650,
			height => 200,
			start => time() - 3600,
			draw => {
				dsname		=> "flow_temp",
				name		=> 'flow_temp',
				type		=> 'line',
				color		=> 'a9000c',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Fremløbstemp.'
			},
			gprint         => {
				draw      => 'flow_temp',
		        cfunc     => 'LAST',
				format    => '%.2lf°C',
			},
			
			draw => {
				dsname		=> "return_flow_temp",
				name		=> 'return_flow_temp',
				type		=> 'line',
				color		=> '011091',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Returntemp.'
			},
			gprint         => {
				draw      => 'return_flow_temp',
			        cfunc     => 'LAST',
					format    => '%.2lf°C',
				},

			draw => {
				dsname		=> "temp_diff",
				name		=> 'temp_diff',
				type		=> 'line',
				color		=> 'd7c8dd',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Temp. diff.'
			},
			gprint         => {
				draw      => 'temp_diff',
		        cfunc     => 'LAST',
				format    => '%.2lf°C',
			},

			draw => {
				dsname		=> "flow",
				name		=> 'flow',
				type		=> 'line',
				color		=> 'ad6933',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Flow'
			},
			gprint         => {
				draw      => 'flow',
		        cfunc     => 'LAST',
				format    => '%.0lf l/h',
			},

			draw => {
				dsname		=> "effect",
				name		=> 'effect',
				type		=> 'line',
				color		=> '00982f',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Effekt'
			},
			gprint         => {
				draw      => 'effect',
		        cfunc     => 'LAST',
				format    => "%.2lf kW",
			},
			
			comment        => " \n \n",

			draw => {
				dsname		=> "hours",
				name		=> 'hours',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'hours',
		        cfunc     => 'LAST',
				format    => "Hours %.0lf h",
			},

			draw => {
				dsname		=> "volume",
				name		=> 'volume',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'volume',
		        cfunc     => 'LAST',
				format    => 'Volume %.0lf m3',
			},

			draw => {
				dsname		=> "energy",
				name		=> 'energy',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'energy',
		        cfunc     => 'LAST',
				format    => 'Energy %.0lf kWh',
			}
		);
		$rrd->graph(
			title => 'Meter graph daily',
			image => IMG_ROOT . '/' . DAY_IMG,
			vertical_label => '',
			width => 650,
			height => 200,
			start => time() - 3600 * 24,
			draw => {
				dsname		=> "flow_temp",
				name		=> 'flow_temp',
				type		=> 'line',
				color		=> 'a9000c',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Fremløbstemp.'
			},
			gprint         => {
				draw      => 'flow_temp',
		        cfunc     => 'LAST',
				format    => '%.2lf°C',
			},
			
			draw => {
				dsname		=> "return_flow_temp",
				name		=> 'return_flow_temp',
				type		=> 'line',
				color		=> '011091',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Returntemp.'
			},
			gprint         => {
				draw      => 'return_flow_temp',
			        cfunc     => 'LAST',
					format    => '%.2lf°C',
				},

			draw => {
				dsname		=> "temp_diff",
				name		=> 'temp_diff',
				type		=> 'line',
				color		=> 'd7c8dd',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Temp. diff.'
			},
			gprint         => {
				draw      => 'temp_diff',
		        cfunc     => 'LAST',
				format    => '%.2lf°C',
			},

			draw => {
				dsname		=> "flow",
				name		=> 'flow',
				type		=> 'line',
				color		=> 'ad6933',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Flow'
			},
			gprint         => {
				draw      => 'flow',
		        cfunc     => 'LAST',
				format    => '%.0lf l/h',
			},

			draw => {
				dsname		=> "effect",
				name		=> 'effect',
				type		=> 'line',
				color		=> '00982f',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Effekt'
			},
			gprint         => {
				draw      => 'effect',
		        cfunc     => 'LAST',
				format    => "%.2lf kW",
			},

			comment        => " \n \n",

			draw => {
				dsname		=> "hours",
				name		=> 'hours',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'hours',
		        cfunc     => 'LAST',
				format    => "Hours %.0lf h",
			},

			draw => {
				dsname		=> "volume",
				name		=> 'volume',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'volume',
		        cfunc     => 'LAST',
				format    => 'Volume %.0lf m3',
			},

			draw => {
				dsname		=> "energy",
				name		=> 'energy',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'energy',
		        cfunc     => 'LAST',
				format    => 'Energy %.0lf kWh',
			}
		);
		$rrd->graph(
			title => 'Meter graph weekly',
			image => IMG_ROOT . '/' . WEEK_IMG,
			vertical_label => '',
			width => 650,
			height => 200,
			start => time() - 3600 * 24 * 7,
			draw => {
				dsname		=> "flow_temp",
				name		=> 'flow_temp',
				type		=> 'line',
				color		=> 'a9000c',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Fremløbstemp.'
			},
			gprint         => {
				draw      => 'flow_temp',
		        cfunc     => 'LAST',
				format    => '%.2lf°C',
			},
			
			draw => {
				dsname		=> "return_flow_temp",
				name		=> 'return_flow_temp',
				type		=> 'line',
				color		=> '011091',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Returntemp.'
			},
			gprint         => {
				draw      => 'return_flow_temp',
			        cfunc     => 'LAST',
					format    => '%.2lf°C',
				},

			draw => {
				dsname		=> "temp_diff",
				name		=> 'temp_diff',
				type		=> 'line',
				color		=> 'd7c8dd',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Temp. diff.'
			},
			gprint         => {
				draw      => 'temp_diff',
		        cfunc     => 'LAST',
				format    => '%.2lf°C',
			},

			draw => {
				dsname		=> "flow",
				name		=> 'flow',
				type		=> 'line',
				color		=> 'ad6933',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Flow'
			},
			gprint         => {
				draw      => 'flow',
		        cfunc     => 'LAST',
				format    => '%.0lf l/h',
			},

			draw => {
				dsname		=> "effect",
				name		=> 'effect',
				type		=> 'line',
				color		=> '00982f',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Effekt'
			},
			gprint         => {
				draw      => 'effect',
		        cfunc     => 'LAST',
				format    => "%.2lf kW",
			},

			comment        => " \n \n",

			draw => {
				dsname		=> "hours",
				name		=> 'hours',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'hours',
		        cfunc     => 'LAST',
				format    => "Hours %.0lf h",
			},

			draw => {
				dsname		=> "volume",
				name		=> 'volume',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'volume',
		        cfunc     => 'LAST',
				format    => 'Volume %.0lf m3',
			},

			draw => {
				dsname		=> "energy",
				name		=> 'energy',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'energy',
		        cfunc     => 'LAST',
				format    => 'Energy %.0lf kWh',
			}
		);
		$rrd->graph(
			title => 'Meter graph monthly',
			image => IMG_ROOT . '/' . MONTH_IMG,
			vertical_label => '',
			width => 650,
			height => 200,
			start => time() - 3600 * 24 * 31,
			draw => {
				dsname		=> "flow_temp",
				name		=> 'flow_temp',
				type		=> 'line',
				color		=> 'a9000c',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Fremløbstemp.'
			},
			gprint         => {
				draw      => 'flow_temp',
		        cfunc     => 'LAST',
				format    => '%.2lf°C',
			},
			
			draw => {
				dsname		=> "return_flow_temp",
				name		=> 'return_flow_temp',
				type		=> 'line',
				color		=> '011091',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Returntemp.'
			},
			gprint         => {
				draw      => 'return_flow_temp',
			        cfunc     => 'LAST',
					format    => '%.2lf°C',
				},

			draw => {
				dsname		=> "temp_diff",
				name		=> 'temp_diff',
				type		=> 'line',
				color		=> 'd7c8dd',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Temp. diff.'
			},
			gprint         => {
				draw      => 'temp_diff',
		        cfunc     => 'LAST',
				format    => '%.2lf°C',
			},

			draw => {
				dsname		=> "flow",
				name		=> 'flow',
				type		=> 'line',
				color		=> 'ad6933',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Flow'
			},
			gprint         => {
				draw      => 'flow',
		        cfunc     => 'LAST',
				format    => '%.0lf l/h',
			},

			draw => {
				dsname		=> "effect",
				name		=> 'effect',
				type		=> 'line',
				color		=> '00982f',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Effekt'
			},
			gprint         => {
				draw      => 'effect',
		        cfunc     => 'LAST',
				format    => "%.2lf kW",
			},

			comment        => " \n \n",

			draw => {
				dsname		=> "hours",
				name		=> 'hours',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'hours',
		        cfunc     => 'LAST',
				format    => "Hours %.0lf h",
			},

			draw => {
				dsname		=> "volume",
				name		=> 'volume',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'volume',
		        cfunc     => 'LAST',
				format    => 'Volume %.0lf m3',
			},

			draw => {
				dsname		=> "energy",
				name		=> 'energy',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'energy',
		        cfunc     => 'LAST',
				format    => 'Energy %.0lf kWh',
			}
		);
		$rrd->graph(
			title => 'Meter graph yearly',
			image => IMG_ROOT . '/' . YEAR_IMG,
			vertical_label => '',
			width => 650,
			height => 200,
			start => time() - 3600 * 24 * 365,
			draw => {
				dsname		=> "flow_temp",
				name		=> 'flow_temp',
				type		=> 'line',
				color		=> 'a9000c',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Fremløbstemp.'
			},
			gprint         => {
				draw      => 'flow_temp',
		        cfunc     => 'LAST',
				format    => '%.2lf°C',
			},
			
			draw => {
				dsname		=> "return_flow_temp",
				name		=> 'return_flow_temp',
				type		=> 'line',
				color		=> '011091',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Returntemp.'
			},
			gprint         => {
				draw      => 'return_flow_temp',
			        cfunc     => 'LAST',
					format    => '%.2lf°C',
				},

			draw => {
				dsname		=> "temp_diff",
				name		=> 'temp_diff',
				type		=> 'line',
				color		=> 'd7c8dd',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Temp. diff.'
			},
			gprint         => {
				draw      => 'temp_diff',
		        cfunc     => 'LAST',
				format    => '%.2lf°C',
			},

			draw => {
				dsname		=> "flow",
				name		=> 'flow',
				type		=> 'line',
				color		=> 'ad6933',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Flow'
			},
			gprint         => {
				draw      => 'flow',
		        cfunc     => 'LAST',
				format    => '%.0lf l/h',
			},

			draw => {
				dsname		=> "effect",
				name		=> 'effect',
				type		=> 'line',
				color		=> '00982f',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Effekt'
			},
			gprint         => {
				draw      => 'effect',
		        cfunc     => 'LAST',
				format    => "%.2lf kW",
			},

			comment        => " \n \n",

			draw => {
				dsname		=> "hours",
				name		=> 'hours',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'hours',
		        cfunc     => 'LAST',
				format    => "Hours %.0lf h",
			},

			draw => {
				dsname		=> "volume",
				name		=> 'volume',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'volume',
		        cfunc     => 'LAST',
				format    => 'Volume %.0lf m3',
			},

			draw => {
				dsname		=> "energy",
				name		=> 'energy',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'energy',
		        cfunc     => 'LAST',
				format    => 'Energy %.0lf kWh',
			}
		);

		# graphs for energy
		$rrd->graph(
			title => 'Meter energy graph hourly',
			image => IMG_ROOT . '/' . HOUR_ENERGY_IMG,
			vertical_label => '',
			width => 650,
			height => 200,
			start => time() - 3600,
			draw => {
				dsname		=> "effect",
				name		=> 'effect',
				type		=> 'line',
				color		=> '00982f',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Effekt'
			},
			gprint         => {
				draw      => 'effect',
		        cfunc     => 'LAST',
				format    => "%.2lf kW",
			},
			
			comment        => " \n \n",

			draw => {
				dsname		=> "hours",
				name		=> 'hours',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'hours',
		        cfunc     => 'LAST',
				format    => "Hours %.0lf h",
			},

			draw => {
				dsname		=> "volume",
				name		=> 'volume',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'volume',
		        cfunc     => 'LAST',
				format    => 'Volume %.0lf m3',
			},

			draw => {
				dsname		=> "energy",
				name		=> 'energy',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'energy',
		        cfunc     => 'LAST',
				format    => 'Energy %.0lf kWh',
			}
		);
		$rrd->graph(
			title => 'Meter energy graph daily',
			image => IMG_ROOT . '/' . DAY_ENERGY_IMG,
			vertical_label => '',
			width => 650,
			height => 200,
			start => time() - 3600 * 24,
			draw => {
				dsname		=> "effect",
				name		=> 'effect',
				type		=> 'line',
				color		=> '00982f',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Effekt'
			},
			gprint         => {
				draw      => 'effect',
		        cfunc     => 'LAST',
				format    => "%.2lf kW",
			},
			
			comment        => " \n \n",

			draw => {
				dsname		=> "hours",
				name		=> 'hours',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'hours',
		        cfunc     => 'LAST',
				format    => "Hours %.0lf h",
			},

			draw => {
				dsname		=> "volume",
				name		=> 'volume',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'volume',
		        cfunc     => 'LAST',
				format    => 'Volume %.0lf m3',
			},

			draw => {
				dsname		=> "energy",
				name		=> 'energy',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'energy',
		        cfunc     => 'LAST',
				format    => 'Energy %.0lf kWh',
			}
		);
		$rrd->graph(
			title => 'Meter energy graph weekly',
			image => IMG_ROOT . '/' . WEEK_ENERGY_IMG,
			vertical_label => '',
			width => 650,
			height => 200,
			start => time() - 3600 * 24 * 7,
			draw => {
				dsname		=> "effect",
				name		=> 'effect',
				type		=> 'line',
				color		=> '00982f',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Effekt'
			},
			gprint         => {
				draw      => 'effect',
		        cfunc     => 'LAST',
				format    => "%.2lf kW",
			},
			
			comment        => " \n \n",

			draw => {
				dsname		=> "hours",
				name		=> 'hours',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'hours',
		        cfunc     => 'LAST',
				format    => "Hours %.0lf h",
			},

			draw => {
				dsname		=> "volume",
				name		=> 'volume',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'volume',
		        cfunc     => 'LAST',
				format    => 'Volume %.0lf m3',
			},

			draw => {
				dsname		=> "energy",
				name		=> 'energy',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'energy',
		        cfunc     => 'LAST',
				format    => 'Energy %.0lf kWh',
			}
		);
		$rrd->graph(
			title => 'Meter energy graph monthly',
			image => IMG_ROOT . '/' . MONTH_ENERGY_IMG,
			vertical_label => '',
			width => 650,
			height => 200,
			start => time() - 3600 * 24 * 31,
			draw => {
				dsname		=> "effect",
				name		=> 'effect',
				type		=> 'line',
				color		=> '00982f',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Effekt'
			},
			gprint         => {
				draw      => 'effect',
		        cfunc     => 'LAST',
				format    => "%.2lf kW",
			},
			
			comment        => " \n \n",

			draw => {
				dsname		=> "hours",
				name		=> 'hours',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'hours',
		        cfunc     => 'LAST',
				format    => "Hours %.0lf h",
			},

			draw => {
				dsname		=> "volume",
				name		=> 'volume',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'volume',
		        cfunc     => 'LAST',
				format    => 'Volume %.0lf m3',
			},

			draw => {
				dsname		=> "energy",
				name		=> 'energy',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'energy',
		        cfunc     => 'LAST',
				format    => 'Energy %.0lf kWh',
			}
		);
		$rrd->graph(
			title => 'Meter energy graph yearly',
			image => IMG_ROOT . '/' . YEAR_ENERGY_IMG,
			vertical_label => '',
			width => 650,
			height => 200,
			start => time() - 3600 * 24 * 365,
			draw => {
				dsname		=> "effect",
				name		=> 'effect',
				type		=> 'line',
				color		=> '00982f',
				thickness	=> 1.5,
				cfunc		=> 'MAX',
				legend		=> 'Effekt'
			},
			gprint         => {
				draw      => 'effect',
		        cfunc     => 'LAST',
				format    => "%.2lf kW",
			},
			
			comment        => " \n \n",

			draw => {
				dsname		=> "hours",
				name		=> 'hours',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'hours',
		        cfunc     => 'LAST',
				format    => "Hours %.0lf h",
			},

			draw => {
				dsname		=> "volume",
				name		=> 'volume',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'volume',
		        cfunc     => 'LAST',
				format    => 'Volume %.0lf m3',
			},

			draw => {
				dsname		=> "energy",
				name		=> 'energy',
				type		=> 'hidden',
				cfunc		=> 'MAX',
			},
			gprint         => {
				draw      => 'energy',
		        cfunc     => 'LAST',
				format    => 'Energy %.0lf kWh',
			}
		);
	}
	else {
		# discard
		warn "discarded: $unix_time\n";
	}
	
	$mqtt_data = undef;
}

