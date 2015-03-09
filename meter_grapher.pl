#!/opt/local/bin/perl -w

use strict;
use Data::Dumper;
use Net::MQTT::Simple;
use RRDTool::OO;

use constant RRD_FILE => "/tmp/esp.rrd";

use constant IMG_ROOT => '/tmp';
use constant HOUR_IMG => 'hour.png';
use constant DAY_IMG => 'day.png';
use constant WEEK_IMG => 'week.png';
use constant MONTH_IMG => 'month.png';
use constant YEAR_IMG => 'year.png';

my $unix_time;
my $mqtt = Net::MQTT::Simple->new(q[loppen.christiania.org]);
my $mqtt_data = undef;
my $mqtt_count = 0;

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
		archive => {
			rows => 1440
		}
	);
}
 
sub mqtt_handler {
	my ($topic, $message) = @_;
	
	# handle mqtt
	if ($topic =~ m!/sample/(\d+)/flow_temperature!) {
		$unix_time = $1;
		$mqtt_data->{flow_temperature} = $message;
		$mqtt_count++;
	} 
	if ($topic =~ m!/sample/(\d+)/return_flow_temperature!) {
		$unix_time = $1;
		$mqtt_data->{return_flow_temperature} = $message;
		$mqtt_count++;
	}
	if ($topic =~ m!/sample/(\d+)/temperature_difference!) {
		$unix_time = $1;
		$mqtt_data->{temperature_difference} = $message;
		$mqtt_count++;
	}
	
	if ($mqtt_count == 3) {
		warn Dumper($mqtt_data);
		warn "unix_time: $unix_time last rrd unix time: ". $rrd->last . "\n";
		if (($rrd->last < $unix_time + 5) && ($unix_time < time() + 3600)) {
			# update rrd
			$rrd->update(
				time => $unix_time, 
				values => {
					flow_temp			=>  $mqtt_data->{flow_temperature},
					return_flow_temp	=>  $mqtt_data->{return_flow_temperature},
					temp_diff			=>  $mqtt_data->{temperature_difference}
				}
			);
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
					format    => '%.0lf°C %s',
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
					format    => '%.0lf°C %s',
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
					format    => '%.0lf°C %s',
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
					format    => '%.0lf°C %s',
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
					format    => '%.0lf°C %s',
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
					format    => '%.0lf°C %s',
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
					format    => '%.0lf°C %s',
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
					format    => '%.0lf°C %s',
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
					format    => '%.0lf°C %s',
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
					format    => '%.0lf°C %s',
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
					format    => '%.0lf°C %s',
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
					format    => '%.0lf°C %s',
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
					format    => '%.0lf°C %s',
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
					format    => '%.0lf°C %s',
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
					format    => '%.0lf°C %s',
				}
			);
		}
		else {
			# discard
			warn "discarded: $unix_time\n";
		}
		
		$mqtt_data = undef;
		$mqtt_count = 0;
	}
}


$mqtt->run(q[/sample/#] => \&mqtt_handler);
