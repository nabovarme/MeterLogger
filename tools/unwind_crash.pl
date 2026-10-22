#!/usr/bin/env perl
use strict;
use warnings;
use Getopt::Long;

# ============================================================================
# ESP8266 Non-OS SDK Crash Log Unwinder (Call0 ABI)
# ============================================================================

my $elf_file;
my $hex_data = "";
my $help;

GetOptions(
	"elf|e=s" => \$elf_file,
	"hex|h=s" => \$hex_data,
	"help"    => \$help
) or die "Error in command line arguments\n";

if ($help || !$elf_file || (!@ARGV && !$hex_data)) {
	print_usage();
	exit 0;
}

if (!$hex_data) {
	if (@ARGV) {
		$hex_data = join("", @ARGV);
	} else {
		local $/;
		$hex_data = <STDIN>;
	}
}

# Clean HTTP response metadata and whitespace
$hex_data =~ s/offset=\d+&data=//g;
$hex_data =~ s/[^0-9a-fA-F]//g;

my $raw_data = pack("H*", $hex_data);

if (length($raw_data) < 116) {
	die sprintf(
		"Error: Combined payload is too short (%d bytes). Minimum 116 bytes needed for crash header.\n",
		length($raw_data)
	);
}

# ============================================================================
# 1. PARSE BINARY CRASH HEADER (crash_log_header_t)
# ============================================================================

my @fields = unpack("V28", substr($raw_data, 0, 112));

my $magic          = $fields[0];
my $build_ts       = $fields[1];
my $pc             = $fields[2];
my $ps             = $fields[3];
my $sar            = $fields[4];
my $vpri           = $fields[5];
my $a0             = $fields[6];
my @a_regs         = @fields[7..20];
my $litbase        = $fields[21];
my $sr176          = $fields[22];
my $sr208          = $fields[23];
my $regs_a1        = $fields[24];
my $interrupted_sp = unpack("V", substr($raw_data, 108, 4));
my $exccause       = $fields[25];
my $excvaddr       = $fields[26];
my $stack_dump_len = unpack("V", substr($raw_data, 112, 4));

if ($regs_a1 != $interrupted_sp) {
	warn sprintf(
		"Warning: regs.a1 (0x%08X) differs from header interrupted_sp (0x%08X); using header field at offset 108.\n",
		$regs_a1, $interrupted_sp
	);
}

if ($magic != 0xDEADBEEF) {
	die sprintf(
		"Error: Invalid Crash Log Magic 0x%08X (Expected 0xDEADBEEF)\n",
		$magic
	);
}

# Validate the declared stack length against the available payload.
my $payload_len = length($raw_data) - 116;
my $available_stack_dump_len = int($payload_len / 4) * 4;

if ($stack_dump_len > 2048) {
	die "Error: stack_dump_len $stack_dump_len exceeds the expected maximum of 2048 bytes.\n";
}

if ($stack_dump_len % 4) {
	die "Error: stack_dump_len $stack_dump_len is not a multiple of four.\n";
}

if ($payload_len % 4) {
	warn "Warning: stack payload contains trailing bytes that do not form a complete 32-bit word.\n";
}

if ($stack_dump_len > $available_stack_dump_len) {
	warn sprintf(
		"Warning: header declares %u stack bytes, but only %u complete payload bytes are present; using the available bytes.\n",
		$stack_dump_len, $available_stack_dump_len
	);
	$stack_dump_len = $available_stack_dump_len;
} elsif ($stack_dump_len < $payload_len) {
	warn sprintf(
		"Warning: payload contains %u bytes beyond the declared stack dump; ignoring the extra bytes.\n",
		$payload_len - $stack_dump_len
	);
}

print "=" x 75 . "\n";
print "                ESP8266 CALL0 STACK UNWINDER REPORT\n";
print "=" x 75 . "\n";

printf "Build Timestamp : 0x%08X\n", $build_ts;
printf "Exception Cause : %d (%s)\n", $exccause, get_cause_desc($exccause);
printf "Crash PC        : 0x%08X -> %s\n", $pc, resolve_addr($elf_file, $pc);
printf "Fault Address   : 0x%08X -> %s\n", $excvaddr, resolve_addr($elf_file, $excvaddr);
printf "Interrupted SP  : 0x%08X\n", $interrupted_sp;
printf "Initial RA (A0) : 0x%08X -> %s\n", $a0, resolve_addr($elf_file, $a0 - 3);
printf "Stack Payload   : %d bytes logged (%d bytes captured in payload)\n",
	$stack_dump_len, length($raw_data) - 116;

print "-" x 75 . "\n";

print "\nREGISTER DUMP:\n";

printf "  PC : 0x%08X  PS : 0x%08X  SAR: 0x%08X  VPRI: 0x%08X\n",
	$pc, $ps, $sar, $vpri;
printf "  A0 : 0x%08X  A1 : 0x%08X  A2 : 0x%08X  A3  : 0x%08X\n",
	$a0, $interrupted_sp, $a_regs[0], $a_regs[1];
printf "  A4 : 0x%08X  A5 : 0x%08X  A6 : 0x%08X  A7  : 0x%08X\n",
	$a_regs[2], $a_regs[3], $a_regs[4], $a_regs[5];
printf "  A8 : 0x%08X  A9 : 0x%08X  A10: 0x%08X  A11 : 0x%08X\n",
	$a_regs[6], $a_regs[7], $a_regs[8], $a_regs[9];
printf "  A12: 0x%08X  A13: 0x%08X  A14: 0x%08X  A15 : 0x%08X\n",
	$a_regs[10], $a_regs[11], $a_regs[12], $a_regs[13];

print "-" x 75 . "\n";

# ============================================================================
# 2. POPULATE RAM LOOKUP MAP FOR STACK DUMP
# ============================================================================

my $stack_payload = substr($raw_data, 116, $stack_dump_len);
my %ram;
my $words = int(length($stack_payload) / 4);

for (my $i = 0; $i < $words; $i++) {
	my $addr = $interrupted_sp + ($i * 4);
	my $val  = unpack("V", substr($stack_payload, $i * 4, 4));

	$ram{$addr} = $val;
}

# ============================================================================
# 3. DETERMINISTIC CALL0 PROLOGUE UNWINDING LOOP
# ============================================================================

print "\nRECONSTRUCTED CALL STACK:\n";

my $curr_pc   = $pc;
my $curr_sp   = $interrupted_sp;
my $curr_a0   = $a0;
my $frame_num = 0;

while ($frame_num < 30) {
	my $loc = resolve_addr($elf_file, $curr_pc);

	printf "#%-2d 0x%08X in %s (SP=0x%08X)\n",
		$frame_num, $curr_pc, $loc, $curr_sp;

	if (
		($curr_pc < 0x40100000 || $curr_pc > 0x4010C000)
		&&
		($curr_pc < 0x40200000 || $curr_pc > 0x40300000)
	) {
		last;
	}

	my ($frame_size, $a0_offset) = analyze_prologue($elf_file, $curr_pc);
	my $next_a0 = undef;

	# 1. Did the prologue explicitly save A0 to the stack?
	if (defined $a0_offset) {
		my $saved_a0_addr = $curr_sp + $a0_offset;

		if (exists $ram{$saved_a0_addr}) {
			my $val = $ram{$saved_a0_addr};

			# Ignore zeroed out memory (compiler optimization bug)
			if ($val != 0 && $val != 0xFFFFFFFF) {
				$next_a0 = $val;
			}
		}
	}

	# 2. Try Register A0
	if (
		!defined $next_a0
		&& defined $curr_a0
		&& $curr_a0 != 0
		&& $curr_a0 != 0xFFFFFFFF
	) {
		if (abs($curr_a0 - 3 - $curr_pc) > 1024) {
			$next_a0 = $curr_a0;
		}
	}

	# 3. DEEP MEMORY SCAN FALLBACK: If A0 wasn't saved, scan deep into the stack
	if (!defined $next_a0) {
		# Scan up to 512 bytes up the stack
		for (my $off = 0; $off < 512; $off += 4) {
			my $check_addr = $curr_sp + $off;

			if (exists $ram{$check_addr}) {
				my $val = $ram{$check_addr};

				if (
					($val >= 0x40100000 && $val <= 0x4010C000)
					||
					($val >= 0x40200000 && $val <= 0x40300000)
				) {
					# Ignore pointers pointing slightly back into the same function
					if (abs($val - 3 - $curr_pc) > 64) {
						$next_a0 = $val;

						# Adjust frame size to jump over this corrupted block
						$frame_size = $off + 4;
						last;
					}
				}
			}
		}
	}

	if (!defined $next_a0 || $next_a0 == 0 || $next_a0 == 0xFFFFFFFF) {
		print "    [Unwinder reached top of stack or ran out of RAM payload]\n";
		last;
	}

	$curr_sp += ($frame_size > 0 ? $frame_size : 32);
	$curr_pc  = $next_a0 - 3;
	$curr_a0  = undef;

	$frame_num++;
}

print "=" x 75 . "\n";

# ============================================================================
# HELPER SUBROUTINES
# ============================================================================

sub analyze_prologue {
	my ($elf, $target_pc) = @_;

	my $symbol_addr = get_symbol_start_addr($elf, $target_pc);

	my $start_addr = $symbol_addr ? $symbol_addr : ($target_pc - 128);
	my $stop_addr  = $target_pc;

	my $cmd = sprintf(
		"xtensa-lx106-elf-objdump -d --start-address=0x%08X --stop-address=0x%08X %s 2>&1",
		$start_addr, $stop_addr, $elf
	);

	my $disasm = `$cmd`;

	my $frame_size = 0;
	my $a0_offset  = undef;

	foreach my $line (split /\n/, $disasm) {
		if ($line =~ /addi(?:\.n)?\s+(?:a1|sp),\s*(?:a1|sp),\s*(-?)(0x[0-9a-fA-F]+|\d+)/i) {
			my $sign = $1;
			my $val  = ($2 =~ /^0x/i) ? hex($2) : int($2);

			if ($sign eq '-') {
				$frame_size = $val;
				$a0_offset  = undef;
			} elsif ($val > 0x7FFFFFFF) {
				$frame_size = 4294967296 - $val;
				$a0_offset  = undef;
			}
		} elsif ($line =~ /addmi\s+(?:a1|sp),\s*(?:a1|sp),\s*(-?)(0x[0-9a-fA-F]+|\d+)/i) {
			my $sign = $1;
			my $val  = ($2 =~ /^0x/i) ? hex($2) : int($2);

			if ($sign eq '-') {
				$frame_size = $val * 256;
				$a0_offset  = undef;
			} elsif ($val > 0x7FFFFFFF) {
				$frame_size = (4294967296 - $val) * 256;
				$a0_offset  = undef;
			}
		} elsif ($line =~ /s32i(?:\.n)?\s+a0,\s*(?:a1|sp),\s*(0x[0-9a-fA-F]+|\d+)/i) {
			$a0_offset = ($1 =~ /^0x/i) ? hex($1) : int($1);
		}
	}

	return ($frame_size, $a0_offset);
}

sub get_symbol_start_addr {
	my ($elf, $addr) = @_;

	my $cmd = sprintf("xtensa-lx106-elf-nm -n %s 2>&1", $elf);
	my $output = `$cmd`;
	my $last_addr = 0;

	foreach my $line (split /\n/, $output) {
		if ($line =~ /^([0-9a-fA-F]+)\s+[tT]\s+(.+)$/) {
			my $sym_addr = hex($1);

			if ($sym_addr > $addr) {
				return $last_addr;
			}

			$last_addr = $sym_addr;
		}
	}

	return $last_addr;
}

sub resolve_addr {
	my ($elf, $addr) = @_;

	my $cmd = sprintf(
		"xtensa-lx106-elf-addr2line -pfiaC -e %s 0x%08X 2>&1",
		$elf, $addr
	);

	my $output = `$cmd`;
	chomp($output) if $output;

	return $output || "Unknown";
}

sub get_cause_desc {
	my ($cause) = @_;

	my %causes = (
		0  => "IllegalInstructionCause",
		1  => "SystemCallCause",
		2  => "InstructionFetchErrorCause",
		3  => "LoadStoreErrorCause",
		4  => "Level1InterruptCause",
		5  => "AllocaCause",
		6  => "IntegerDivideByZeroCause",
		9  => "LoadStoreAlignmentCause",
		12 => "InstrPIFDataErrorCause",
		13 => "LoadStorePIFDataErrorCause",
		14 => "InstrPIFAddrErrorCause",
		15 => "LoadStorePIFAddrErrorCause",
		16 => "InstTLBMissCause",
		24 => "InstFetchProhibitedCause",
		28 => "LoadProhibitedCause",
		29 => "StoreProhibitedCause"
	);

	return $causes{$cause} || "Unknown Exception";
}

sub print_usage {
	print "Usage: perl unwind_crash.pl -e <MeterLogger.elf> -h \"<hex_payload_data>\"\n";
}
