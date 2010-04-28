#!/usr/bin/perl -w
# vim:ts=4
#
# Check RAID status. Look for any known types of RAID configurations, and check them all.
# Return CRITICAL if in a DEGRADED state, since if the whole array has failed you'll have already noticed it!
# Return UNKNOWN if there are no RAID configs that can be found.
# Return WARNING if rebuilding or initialising
#
# 2004-2006 Steve Shipway, university of auckland,
# http://www.steveshipway.org/forum/viewtopic.php?f=20&t=417&p=3211
# Steve Shipway Thanks M Carmier for megaraid section.
# 2009-2010 Elan Ruusamäe <glen@delfi.ee>

# Requires: perl 5.8 for the open(my $fh , '-|', @CMD) syntax
# you may workaround for earlier perl it as:
# open(my $fh , join(" ", @CMD, '|') or return;
# http://perldoc.perl.org/perl58delta.html#PerlIO-is-Now-The-Default
#
# License: GPL v2
#
# Supports:
# - Adaptec AAC RAID via aaccli or afacli or arcconf
# - AIX software RAID via lsvg
# - HP/Compaq Smart Array via cciss_vol_status
# - HP SmartArray (MSA1500) via serial line
# - Linux 3ware SATA RAID via tw_cli
# - Linux DPT/I2O hardware RAID controllers via /proc/scsi/dpt_i2o
# - Linux GDTH hardware RAID controllers via /proc/scsi/gdth
# - Linux LSI MegaRaid hardware RAID via CmdTool2
# - Linux LSI MegaRaid hardware RAID via megarc
# - Linux LSI MegaRaid hardware RAID via /proc/megaraid
# - Linux MegaIDE hardware RAID controllers via /proc/megaide
# - Linux MPT hardware RAID via mpt-status
# - Linux software RAID (md) via /proc/mdstat
# - LSI Logic MegaRAID SAS series via MegaCli
# - LSI MegaRaid via lsraid
# - Serveraid IPS via ipssend
# - Solaris software RAID via metastat
#
# Changes:
# Version 1.1 : IPS; Solaris, AIX, Linux software RAID; megaide
# Version 2.0 : Added megaraid, mpt (serveraid), aaccli (serveraid)
# Version 2.1 :
# - Made script more generic and secure
# - Added gdth
# - Added dpt_i2o
# - Added 3ware SATA RAID
# - Added Adaptec AAC-RAID via arcconf
# - Added LSI MegaRaid via megarc
# - Added LSI MegaRaid via CmdTool2
# - Added HP/Compaq Smart Array via cciss_vol_status
# - Added HP MSA1500 check via serial line

use strict;
use Getopt::Long;
use vars qw($opt_v $opt_d $opt_h $opt_W $opt_S);
my(%ERRORS) = (OK => 0, WARNING => 1, CRITICAL => 2, UNKNOWN => 3);
my($VERSION) = "2.1";
my($message, $status);
my(@ignore);

my $sudo = which('sudo');
my $cat = which('cat');

# various RAID tools
my $mpt_status = which('mpt-status');
my $aaccli = which('aaccli');
my $afacli = which('afacli');
my $lsraid = which('lsraid');
my $megacli = which('MegaCli64') || which('MegaCli');
my $metastat = which('metastat');
my $lsvg = which('lsvg');
my $ipssend = which('ipssend');
my $tw_cli = which('tw_cli-9xxx') || which('tw_cli');
my $arcconf = which('arcconf');
my $megarc = which('megarc');
my $cmdtool2 = which('CmdTool2');
my $cciss_vol_status = which('cciss_vol_status');

#####################################################################
sub print_usage () {
	print "Usage: check_raid [list of devices to ignore]\n";
	print "       check_raid -v\n";
	print "       check_raid -h\n";
}

sub print_help () {
	print "check_raid, v$VERSION\n";
	print "Copyright (c) 2004-2006 Steve Shipway, Copyright (c) 2009-2010, Elan Ruusamäe <glen\@delfi.ee>

This plugin reports the current server's RAID status
";
	print_usage();
}

#####################################################################
# return true if parameter is not in ignore list
sub valid($) {
	my($v) = lc $_[0];
	foreach (@ignore) {
		return 0 if lc $_ eq $v;
	}
	return 1;
}

# Solaris, software RAID
sub check_metastat {
	my($d,$sd);

	my @CMD = $metastat;
	unshift(@CMD, $sudo) if $> and $sudo;

	open(my $fh , '-|', @CMD) or return;
	while (<$fh>) {
		if (/^(\S+):/) { $d = $1; $sd = ''; next; }
		if (/Submirror \d+:\s+(\S+)/) { $sd = $1; next; }
		if (my($s) = /State: (\S.+)/) {
			if ($sd and valid($sd) and valid($d)) {
				if ($s =~ /Okay/i) {
					# no worries...
				} elsif ($s =~ /Resync/i) {
					$status = $ERRORS{WARNING} unless $status;
				} else {
					$status = $ERRORS{CRITICAL};
				}
				$message .= "$d:$sd:$s ";
			}
		}
	}
	close $fh;
}

# MegaIDE RAID controller
sub check_megaide {
	my $fh;
	foreach my $f (</proc/megaide/*/status>) {
		if (-r $f) {
			open $fh, '<', $f or next;
		} else {
			my @CMD = ($cat, $f);
			unshift(@CMD, $sudo) if $> and $sudo;
			open($fh , '-|', @CMD) or next;
		}
		while (<$fh>) {
			next unless (my($s, $n) = /Status\s*:\s*(\S+).*Logical Drive.*:\s*(\d+)/i);
			next unless valid($n);
			if ($s ne 'ONLINE') {
				$status = $ERRORS{CRITICAL};
				$message .= "Megaide:$n:$s ";
			} else {
				$message .= "Megaide:$n:$s ";
			}
			last;
		}
		close $fh;
	}
}

# Linux Multi-Device (md)
# TODO: check linerar devices
sub check_mdstat {
	open my $fh, '<', '/proc/mdstat' or return;

	my ($md, $md_pers, $md_status, $resync_status);
	my (@status, @failed_disks);

	while (<$fh>) {
		chomp;

		if (my($s, $p) = /^(\S+)\s+:\s*(?:\S+)\s+(\S+)/) {
			$md = $s;
			$md_pers = $p;
			@failed_disks = $_ =~ m/(\S+)\[\d+\]\(F\)/g;
			undef $resync_status;
			next;
		}

		# linux-2.6.33/drivers/md/dm-raid1.c, device_status_char
		# A => Alive - No failures
		# D => Dead - A write failure occurred leaving mirror out-of-sync
		# S => Sync - A sychronization failure occurred, mirror out-of-sync
		# R => Read - A read failure occurred, mirror data unaffected
		# U => for the rest
		if (my($s) = /^\s+.*\[([U_]+)\]/) {
			$md_status = $s;
			next;
		}

		# linux-2.6.33/drivers/md/md.c, md_seq_show
		if (my($action) = m{(resync=(?:PENDING|DELAYED))}) {
			$resync_status = $action;
			next;
		}
		# linux-2.6.33/drivers/md/md.c, status_resync
		# [==>..................]  resync = 13.0% (95900032/732515712) finish=175.4min speed=60459K/sec
		if (my($action, $perc, $eta, $speed) = m{(resync|recovery|check|reshape) = ([\d.]+%) \(\d+/\d+\) finish=([\d.]+min) speed=(\d+K/sec)}) {
			$resync_status = "$action:$perc $speed ETA: $eta";
			next;
		}

		# we need empty line denoting end of one md
		next unless /^\s+$/;

		next unless valid($md);

		if ($md_status =~ /_/) {
			$status = $ERRORS{CRITICAL};
			push(@status, "$md($md_pers):@failed_disks:$md_status");

		} elsif (scalar @failed_disks > 0) {
			$status = $ERRORS{WARNING} unless $status;
			push(@status, "$md($md_pers):hot-spare failure: @failed_disks:$md_status");

		} elsif ($resync_status) {
			$status = $ERRORS{WARNING} unless $status;
			push(@status, "$md($md_pers):$md_status ($resync_status)");
			undef $resync_status;

		} else {
			push(@status, "$md($md_pers):$md_status");
		}
	}
	close $fh;

	$message .= "md:[".join(', ', @status)."] " if @status;
}


# Linux, software RAID
sub check_lsraid {
	my @CMD = ($lsraid, '-A', '-p');
	unshift(@CMD, $sudo) if $> and $sudo;

	open(my $fh , '-|', @CMD) or return;
	while (<$fh>) {
		next unless (my($n, $s) = m{/dev/(\S+) \S+ (\S+)});
		next unless valid($n);
		if ($s =~ /good|online/) {
			# no worries
		} elsif ($s =~ /sync/) {
			$status = $ERRORS{WARNING} unless $status;
		} else {
			$status = $ERRORS{CRITICAL};
		}
		$message .= "md:$n:$s ";
	}
	close $fh;
}

# Linux, software RAID
# MegaRAID SAS 8xxx controllers
# based on info from here:
# http://www.bxtra.net/Articles/2008-09-16/Dell-Perc6i-RAID-Monitoring-Script-using-MegaCli-LSI-CentOS-52-64-bits
# TODO: http://www.techno-obscura.com/~delgado/code/check_megaraid_sas
sub check_megacli {
	my @CMD = ($megacli, '-PDList', '-aALL', '-NoLog');
	unshift(@CMD, $sudo) if $> and $sudo;

	open(my $fh , '-|', @CMD) or return;
	my (@status, @devs, %cur);
	while (<$fh>) {
		if (my($s) = /Device Id: (\S+)/) {
			push(@devs, { %cur }) if %cur;
			%cur = ( dev => $s, state => undef, name => undef );
			next;
		}
		if (my($s) = /Firmware state: (\S+)/) {
			$cur{state} = $s;
			next;
		}
		if (my($s) = /Inquiry Data: (.+)/) {
			# trim some spaces
			$s =~ s/\s+/ /g; $s =~ s/^\s+|\s+$//g;
			$cur{name} = $s;
			next;
		}
	}
	close $fh;
	push(@devs, { %cur }) if %cur;

	foreach my $dev (@devs) {
		push(@status, sprintf "Dev%02d (%s): %s", $dev->{dev}, $dev->{name}, $dev->{state});
		if ($dev->{state} ne 'Online') {
			# TODO: process other statuses
			$status = $ERRORS{CRITICAL};
		}
	}

	$message .= "MegaCli:".join(', ', @status) if @status;
}

# AIX LVM
sub check_lsvg {
	my @CMD = $lsvg;
	unshift(@CMD, $sudo) if $> and $sudo;

	my @vg;
	open(my $fh , '-|', @CMD) or return;
	while (<$fh>) {
		chomp;
		push @vg, $_;
	}
	close $fh;

	foreach my $vg (@vg) {
		next unless valid($vg); # skip entire VG

		open(my $fh , '-|', @CMD, '-l', $vg) or next;

		while (<$fh>) {
			my @f = split /\s/;
			my ($n, $s) = ($f[0],$f[5]);
			next if (!valid($n) or !$s);
			next if ($f[3] eq $f[2]); # not a mirrored LV

			if ($s =~ /open\/(\S+)/i) {
				$s = $1;
				if ($s ne 'syncd') {
					$status = $ERRORS{CRITICAL};
				}
				$message .= "lvm:$n:$s ";
			}
		}
		close $fh;
	}
}

# Serveraid IPS
sub check_ipssend {
	my @CMD = $ipssend;
	unshift(@CMD, $sudo) if $> and $sudo;

	open(my $fh , '-|', @CMD) or return;
	while (<$fh>) {
		next if (my($n) = /drive number (\d+)/i);
		next unless valid($n);
		next unless (my($s, $c) = /Status .*: (\S+)\s+(\S+)/);

		if ($c =~ /SYN|RBL/i ) { # resynching
			$status = $ERRORS{WARNING} unless $status;
		} elsif ($c !~ /OKY/i) { # not OK
			$status = $ERRORS{CRITICAL};
		}
		$message .= "ips:$n:$s ";
	}
	close $fh;
}

# Adaptec ServeRAID
sub check_aaccli {
	unless ($aaccli) {
		$message .= "aac:aaccli program not found ";
		$status = $ERRORS{CRITICAL};
		return;
	}

	my @CMD = $aaccli;
	unshift(@CMD, $sudo) if $> and $sudo;

	use IPC::Open2;

	my ($read, $write);
	my $pid = open2($read, $write, @CMD) or return;
	print $write "open aac0\n";
	print $write "container list /full\n";
	print $write "exit\n";
	close $write;
#File foo receiving all output.
#
#AAC0>
#COMMAND: container list /full=TRUE
#Executing: container list /full=TRUE
#Num          Total  Oth Stripe          Scsi   Partition                                       Creation
#Label Type   Size   Ctr Size   Usage   C:ID:L Offset:Size   State   RO Lk Task    Done%  Ent Date   Time
#----- ------ ------ --- ------ ------- ------ ------------- ------- -- -- ------- ------ --- ------ --------
# 0    Mirror 74.5GB            Open    0:02:0 64.0KB:74.5GB Normal                        0  051006 13:48:54
# /dev/sda             Auth             0:03:0 64.0KB:74.5GB Normal                        1  051006 13:48:54
#
#
#AAC0>
#COMMAND: logfile end
#Executing: logfile end
	while (<$read>) {
		if (my ($dsk, $stat) = /(\d:\d\d?:\d+)\s+\S+:\S+\s+(\S+)/) {
			next unless valid($dsk);
			$dsk =~ s/:/\//g;
			next unless valid($dsk);
			$message .= "aac:$dsk:$stat ";
			$status = $ERRORS{CRITICAL} if ($stat eq "Broken");
			$status = $ERRORS{WARNING} if (!$status and $stat eq "Rebuild");
			$status = $ERRORS{WARNING} if (!$status and $stat eq "Bld/Vfy");
			$status = $ERRORS{CRITICAL} if ($stat eq "Missing");
			$status = $ERRORS{WARNING} if (!$status and $stat eq "Verify");
			$status = $ERRORS{WARNING} if (!$status and $stat eq "VfyRepl");
		}
	}
	close $read;
}

# Adaptec AACRAID
sub check_afacli {
	my @CMD = $afacli;
	unshift(@CMD, $sudo) if $> and $sudo;

	use IPC::Open2;

	my ($read, $write);
	my $pid = open2($read, $write, @CMD) or return;
	print $write "open afa0\n";
	print $write "container list /full\n";
	print $write "exit\n";
	close $write;

	while (<$read>) {
		if (my($dsk, $stat) = /(\d:\d\d?:\d+)\s+\S+:\S+\s+(\S+)/) {
			next unless valid($dsk);
			$dsk =~ s/:/\//g;
			next unless valid($dsk);
			$message .= "aac:$dsk:$stat ";
			$status = $ERRORS{CRITICAL} if ($stat eq "Broken");
			$status = $ERRORS{WARNING} if (!$status and $stat eq "Rebuild");
			$status = $ERRORS{WARNING} if (!$status and $stat eq "Bld/Vfy");
			$status = $ERRORS{CRITICAL} if ($stat eq "Missing");
			$status = $ERRORS{WARNING} if (!$status and $stat eq "Verify");
			$status = $ERRORS{WARNING} if (!$status and $stat eq "VfyRepl");
		}
	}
	close $read;
}

# LSILogic MPT ServeRAID
sub check_mpt {
	unless ($mpt_status) {
		$message .= "mpt:mpt-status program not found ";
		$status = $ERRORS{CRITICAL};
		return;
	}

	# status messages pushed here
	my @status;

	my @CMD = $mpt_status;
	unshift(@CMD, $sudo) if $> and $sudo;

	open(my $fh, '-|', @CMD, '-s') or return;
	while (<$fh>) {
		if (my($d, $s) = /^log_id\s*(\d+)\s+(\S+)/) {
			next unless valid($d);
			if (!$status and $s =~ /INITIAL|INACTIVE|RESYNC/) {
				$status = $ERRORS{WARNING} unless $status;
			} elsif ($s =~ /DEGRADED/) {
				$status = $ERRORS{CRITICAL};
			} elsif (!$status and $s !~ /ONLINE|OPTIMAL/) {
				$status = $ERRORS{UNKNOWN} unless $status;
			}
			push(@status, "Logical Volume $d:$s");
			next;
		}

		if (my($d, $s) = /^phys_id\s*(\d+)\s+(\S+)/) {
			next if ($s eq "ONLINE");

			# TODO: process other statuses
			$status = $ERRORS{CRITICAL};

			push(@status, "Physical Disk $d:$s");
			next;
		}
	}
	close $fh;

	$message .= "mpt:".join(', ', @status) if @status;
}

# MegaRAID
sub check_megaraid {

	# status messages pushed here
	my @status;

	foreach my $f (</proc/megaraid/*/raiddrives*>) {
		my $fh;
		if (-r $f) {
			open $fh, '<', $f or next;
		} else {
			my @CMD = ($cat, $f);
			unshift(@CMD, $sudo) if $> and $sudo;
			open($fh , '-|', @CMD) or next;
		}
		my ($n) = $f =~ m{/proc/megaraid/([^/]+)};
		while (<$fh>) {
			if (my($s) = /logical drive\s*:\s*\d+.*, state\s*:\s*(\S+)/i) {
				if ($s ne 'optimal') {
					$status = $ERRORS{CRITICAL};
				}
				push(@status, "$n: $s");
				last;
			}
		}
		close $fh;
	}

	$message .= "MegaRAID: ".join(', ', @status) if @status;
}

# Linux Gdth RAID
# based on check_gdth by Petter Reinholdtsen
# http://homepages.uni-paderborn.de/odenbach/projects/check_gdth/
sub check_gdth {
# Looking for this text block:
# Logical Drives:
#  Number:        0               Status:         ok
#  Capacity [MB]: 17333           Type:           RAID-1
#  Slave Number:  15              Status:         ok
#  Missing Drv.:  0               Invalid Drv.:   0
#  To Array Drv.: --

	# status messages pushed here
	my @status;
	for my $file (</proc/scsi/gdth/*>) {
		open my $fh, '<', $file or return;
		my ($controller) = $file =~ m{([^/]+$)};
		my @ld;
		while (<$fh>) {
			last if (/Array Drives:/); # Stop after the Logical Drive block

			# Match items from Logical Drivers
			if (my($num, $s) = m/^\s+Number:\s+(\d+)\s+Status:\s+(\S+)/) {
				if ($s ne "ok") {
					$status = $ERRORS{CRITICAL};
				}
				push(@ld, "$controller,$num:$s");
			}
		}
		close($fh);
		push(@status, "Logical Drive ".join(', ', @ld)) if @ld;
	}

	$message .= "gdth:".join(', ', @status) if @status;
}

sub check_dpt_i2o {
	my @status;
	for my $file (</proc/scsi/dpt_i2o/*>) {
		open my $fh, '<', $file or return;
		my ($controller) = $file =~ m{([^/]+$)};

		while (<$fh>) {
			if (my ($c, $t, $l, $s) = m/TID=\d+,\s+\(Channel=(\d+),\s+Target=(\d+),\s+Lun=(\d+)\)\s+\((\S+)\)/) {
				if ($s ne "online") {
					$status = $ERRORS{CRITICAL};
				}
				push(@status, "$c,$t,$l:$s");
			}
		}
		close($fh);
	}

	$message .= "dpt_i2o:[".join(', ', @status)."] " if @status;
}

# 3ware SATA RAID
# check designed from check_3ware.sh by:
# Sander Klein <sander [AT] pictura [dash] dp [DOT] nl>
# http://www.pictura-dp.nl/
# Version 20070706
sub check_tw_cli {
	my @CMD = $tw_cli;
	unshift(@CMD, $sudo) if $> and $sudo;

	# status messages pushed here
	my @status;

	my (@c, $fh);
	# scan controllers
	open($fh , '-|', @CMD, 'info') or return;
	while (<$fh>) {
		if (my($c, $model) = /^(c\d+)\s+(\S+)/) {
			push(@c, [$c, $model]);
		}
	}
	close $fh;

	unless (@c) {
		$status = $ERRORS{WARNING} unless $status;
		$message .= "3ware: No Adapters were found on this machine";
		return;
	}

	for my $i (@c) {
		my ($c, $model) = @$i;
		# check each unit on controllers
		open($fh , '-|', @CMD, 'info', $c, 'unitstatus') or next;
		my @cstatus;
		while (<$fh>) {
			next unless (my($u, $s, $p) = /^(u\d+)\s+\S+\s+(\S+)\s+(\S+)/);

			if ($s eq 'OK') {
				push(@cstatus, "$u:$s");

			} elsif ($s eq 'REBUILDING') {
				$status = $ERRORS{WARNING} unless $status;
				push(@cstatus, "$u:$s $p% ");

			} elsif ($s eq 'DEGRADED') {
				open(my $fh , '-|', @CMD, 'info', $c, 'drivestatus');
				my @ds;
				if ($fh) {
					my @p;
					while (<$fh>) {
						next unless (my($p, $s) = /^(p\d+)\s+(\S+)\s+\Q$u\E/);
						push(@ds, "$p:$s");
					}
					close $fh;
				}
				push(@cstatus, "$u:$s (disks: ".join(' ', @ds). ")");
				$status = $ERRORS{CRITICAL};
			} else {
				push(@cstatus, "$u:$_");
				$status = $ERRORS{UNKNOWN} unless $status;
			}
		}
		push(@status, "$c($model): ". join(',', @cstatus));
		close $fh;
	}

	$message .= "3ware: ".join(', ', @status) if @status;
}

# Adaptec AAC-RAID
# check designed from check-aacraid.py, Anchor System - <http://www.anchor.com.au>
# Oliver Hookins, Paul De Audney, Barney Desmond.
# Perl port (check_raid) by Elan Ruusamäe.
sub check_arcconf {
	my @CMD = $arcconf;
	unshift(@CMD, $sudo) if $> and $sudo;

	# status messages pushed here
	my @status;

	# we chdir to /var/log, as tool is creating 'UcliEvt.log'
	chdir('/var/log') || chdir('/');

	my ($fh, $d);
	open($fh , '-|', @CMD, 'GETCONFIG', '1', 'LD') or return;
	while (<$fh>) {
		$d = $1, next if /^Logical device number (\d+)/;
		next unless (my ($s) = /^\s*Status of logical device\s+:\s+(.+)/);
		push(@status, "Logical Device $d:$s");
	}
	close $fh;

	open($fh , '-|', @CMD, 'GETCONFIG', '1', 'AD') or return;
	while (<$fh>) {
		if (my($s) = /Controller Status\s*:\s*(.*)/) {
			if ($s ne 'Optimal') {
				$status = $ERRORS{CRITICAL};
			}
			push(@status, "Controller:$s");
			next;
		}

		if (my($s) = /Defunct disk drive count\s:\s*(\d+)/) {
			$status = $ERRORS{CRITICAL};
			push(@status, "Defunct drives:$s");
			next;
		}

		if (my($td, $fd, $dd) = m{Logical devices/Failed/Degraded\s*:\s*(\d+)/(\d+)/(\d+)}) {
			if (int($fd) > 0) {
				$status = $ERRORS{CRITICAL};
				push(@status, "Failed drives: $fd");
			}
			if (int($dd) > 0) {
				$status = $ERRORS{CRITICAL};
				push(@status, "Degraded drives: $dd");
			}
			next;
		}

		if (my($s) = /^\s*Status\s*:\s*(.*)$/) {
			next if $s eq "Not Installed";

			push(@status, "Battery Status: $s");
			next;
		}

		if (my($s) = /^\s*Over temperature\s*:\s*(.*)$/) {
			if ($s ne "No") {
				$status = $ERRORS{CRITICAL};
				push(@status, "Battery Overtemp: $s");
			}
			next;
		}

		if (my($s) = /\s*Capacity remaining\s*:\s*(\d+)\s*percent.*$/) {
			push(@status, "Battery Capacity: $s%");
			if (int($s) < 50) {
				$status = $ERRORS{WARNING} unless $status;
			}
			if (int($s) < 25) {
				$status = $ERRORS{CRITICAL};
			}
			next;
		}

		if (my($d, $h, $m) = /\s*Time remaining \(at current draw\)\s*:\s*(\d+) days, (\d+) hours, (\d+) minutes/) {
			my $mins = int($d) * 1440 + int($h) * 60 + int($m);
			if ($mins < 1440) {
				$status = $ERRORS{WARNING} unless $status;
			}
			if ($mins < 720) {
				$status = $ERRORS{CRITICAL};
			}

			if ($mins < 60) {
				push(@status, "Battery Time: ${m}m");
			} else {
				push(@status, "Battery Time: ${d}d, ${h}h, ${m}m");
			}
			next;
		}
	}
	close $fh;

	$message .= "arcconf:".join(', ', @status) if @status;
}

# LSI MegaRaid or Dell Perc arrays
# Check the status of all arrays on all Lsi MegaRaid controllers on the local
# machine. Uses the megarc program written by Lsi to get the status of all
# arrays on all local Lsi MegaRaid controllers.
#
# check designed from check_lsi_megaraid:
# http://www.monitoringexchange.org/cgi-bin/page.cgi?g=Detailed/2416.html;d=1
# Perl port (check_raid) by Elan Ruusamäe.
sub check_megarc {
	my @CMD = $megarc;
	unshift(@CMD, $sudo) if $> and $sudo;

	# status messages pushed here
	my @status;

	# get controllers
	open(my $fh , '-|', @CMD, '-AllAdpInfo', '-nolog') or return;
	my @lines = <$fh>;
	close $fh;

	if ($lines[11] =~ /No Adapters Found/) {
		$status = $ERRORS{WARNING} unless $status;
		$message .= "megarc: No LSI adapters were found on this machine";
		return;
	}
	my @c;
	foreach (@lines[12..$#lines]) {
		if (my ($id) = /^\s*(\d+)/) {
			push(@c, int($id));
		}
	}
	unless (@c) {
		$status = $ERRORS{WARNING} unless $status;
		$message .= "megarc: No LSI adapters were found on this machine";
		return;
	}

	foreach my $c (@c) {
		open($fh , '-|', @CMD, '-dispCfg', "-a$c", '-nolog') or return;
		my (%d, %s, $ld);
		while (<$fh>) {
			# Logical Drive : 0( Adapter: 0 ):  Status: OPTIMAL
			if (my($d, $s) = /Logical Drive\s+:\s+(\d+).+Status:\s+(\S+)/) {
				$ld = $d;
				$s{$ld} = $s;
				next;
			}
			# SpanDepth :01     RaidLevel: 5  RdAhead : Adaptive  Cache: DirectIo
			if (my($s) = /RaidLevel:\s+(\S+)/) {
				$d{$ld} = $s if defined $ld;
				next;
			}
		}
		close $fh;

		# now process the details
		unless (keys %d) {
			push(@status, "No arrays found on controller $c");
			$status = $ERRORS{WARNING} unless $status;
			return;
		}

		while (my($d, $s) = each %s) {
			if ($s ne 'OPTIMAL') {
				# The Array number here is incremented by one because of the
				# inconsistent way that the LSI tools count arrays.
				# This brings it back in line with the view in the bios
				# and from megamgr.bin where the array counting starts at
				# 1 instead of 0
				push(@status, "Array ".(int($d) + 1)." status is ".$s{$d}." (Raid-$s on adapter $c)");
				$status = $ERRORS{CRITICAL};
				next;
			}

			push(@status, "Logical Drive $d: $s");
		}
	}

	$message .= "megarc: ".join(', ', @status) if @status;
}

sub check_cmdtool2 {
	my @CMD = $cmdtool2;
	unshift(@CMD, $sudo) if $> and $sudo;

	# status messages pushed here
	my @status;

	# get adapters
	open(my $fh , '-|', @CMD, '-AdpAllInfo', '-aALL', '-nolog') or return;
	my @c;
	while (<$fh>) {
		if (my($c) = /^Adapter #(\d+)/) {
			push(@c, $c);
		}
	}
	close $fh;

	unless (@c) {
		$status = $ERRORS{WARNING} unless $status;
		$message .= "CmdTool2: No LSI adapters were found on this machine";
		return;
	}

	foreach my $c (@c) {
		open($fh , '-|', @CMD, '-CfgDsply', "-a$c", '-nolog') or return;
		my ($d);
		while (<$fh>) {
			# DISK GROUPS: 0
			if (my($s) = /^DISK GROUPS: (\d+)/) {
				$d = int($s);
				next;
			}

			# State: Optimal
			if (my($s) = /^State: (\S+)$/) {
				if ($s ne 'Optimal') {
					$status = $ERRORS{CRITICAL};
				}
				push(@status, "Logical Drive $c,$d: $s");
			}
		}
	}

	$message .= "CmdTool2: ".join(', ', @status) if @status;
}

sub check_cciss {
	unless ($cciss_vol_status) {
		$message .= "cciss:cciss_vol_status program not found";
		$status = $ERRORS{CRITICAL};
		return;
	}

	my @CMD = $cciss_vol_status;
	unshift(@CMD, $sudo) if $> and $sudo;

	# status messages pushed here
	my @status;

	# find controllers
#	cciss0: HP Smart Array P400i Controller
#	Board ID: 0x3235103c
#	Firmware Version: 4.06
#	IRQ: 98
#	Logical drives: 1
#	Current Q depth: 0
#	Current # commands on controller: 0
#	Max Q depth since init: 249
#	Max # commands on controller since init: 275
#	Max SG entries since init: 31
#	Sequential access devices: 0
#
#	cciss/c0d0:      220.12GB       RAID 1(1+0)

	my @c;
	foreach my $f (</proc/driver/cciss/*>) {
		open my $fh, '<', $f or next;
		while (<$fh>) {
			next unless (my($s) = m{^(cciss/[^:]+):});
			push(@c, "/dev/$s");
		}
		close $fh;
	}

	unless (@c) {
		$status = $ERRORS{WARNING} unless $status;
		$message .= "cciss: No Smart Array Adapters were found on this machine";
		return;
	}

	foreach my $c (@c) {
		open(my $fh , '-|', @CMD, $c) or die $!;
		while (<$fh>) {
			chomp;
			# strip for better pattern matching
			s/\.\s*$//;

			# /dev/cciss/c0d0: (Smart Array P400i) RAID 1 Volume 0 status: OK
			if (my($s) = /status: (.*?)$/) {
				if ($s ne 'OK') {
					$status = $ERRORS{CRITICAL};
				}
				push(@status, $_);
			}
		}
		close($fh);
	}

	$message .= "cciss: ".join(', ', @status) if @status;
}

# check from /sys if there are any MSA VOLUME's present.
sub sys_have_msa {
	for my $file (</sys/block/*/device/model>) {
		open my $fh, '<', $file or next;
		my $model = <$fh>;
		close($fh);
		return 1 if ($model =~ /^MSA.+VOLUME/);
	}
	return 0;
}

sub check_hp_msa {
	# TODO: unhardcode out modem dev
	my $ctldevice = "/dev/ttyS0";

	# status messages pushed here
	my @status;

	my $modem = new SerialLine($ctldevice);
	my $fh = $modem->open();
	unless ($fh) {
		$status = $ERRORS{WARNING} unless $status;
		$message .= "hp_msa: Can't open $ctldevice";
		return;
	}

	# check first controller
	print $fh "\r";
	print $fh "show globals\r";
	print $fh "show this_controller\r";
	print $fh "show other_controller\r";
	# this will issue termination match, ie. invalid command
	print $fh "exit\r";

	my ($c, %c, %t);
	while (<$fh>) {
		chomp;
		s/[\n\r]$//;
		last if /Invalid CLI command/;

		# Temperature:
		# EMU: 23 Celsius,  73 Fahrenheit
		# PS1: 22 Celsius,  71 Fahrenheit
		# PS2: 22 Celsius,  71 Fahrenheit
		if (my($s, $c) = /(\S+): (\d+) Celsius,\s+\d+ Fahrenheit/) {
			$t{$s} = int($c);
			next;
		}

		# Controller 1 (right controller):
		if (my($s) = /^(Controller \d+)/) {
			$c = $s;
			$c{$c} = [];
			next;
		}
		# Surface Scan:   Running, LUN 10 (68% Complete)
		if (my($s, $m) = /Surface Scan:\s+(\S+)[,.]\s*(.*)/) {
			if ($s eq 'Running') {
				my ($l, $p) = $m =~ m{(LUN \d+) \((\d+)% Complete\)};
				push(@{$c{$c}}, "Surface: $l ($p%)");
				$status = $ERRORS{WARNING} unless $status;
			} elsif ($s ne 'Complete') {
				push(@{$c{$c}}, "Surface: $s, $m");
				$status = $ERRORS{WARNING} unless $status;
			}
			next;
		}
		# Rebuild Status: Running, LUN 0 (67% Complete)
		if (my($s, $m) = /Rebuild Status:\s+(\S+)[,.]\s*(.*)/) {
			if ($s eq 'Running') {
				my ($l, $p) = $m =~ m{(LUN \d+) \((\d+)% Complete\)};
				push(@{$c{$c}}, "Rebuild: $l ($p%)");
				$status = $ERRORS{WARNING} unless $status;
			} elsif ($s ne 'Complete') {
				push(@{$c{$c}}, "Rebuild: $s, $m");
				$status = $ERRORS{WARNING} unless $status;
			}
			next;
		}
		# Expansion:      Complete.
		if (my($s, $m) = /Expansion:\s+(\S+)[.,]\s*(.*)/) {
			if ($s eq 'Running') {
				my ($l, $p) = $m =~ m{(LUN \d+) \((\d+)% Complete\)};
				push(@{$c{$c}}, "Expansion: $l ($p%)");
				$status = $ERRORS{WARNING} unless $status;
			} elsif ($s ne 'Complete') {
				push(@{$c{$c}}, "Expansion: $s, $m");
				$status = $ERRORS{WARNING} unless $status;
			}
			next;
		}
	}
	$modem->close();

	foreach $c (sort { $a cmp $b } keys %c) {
		my $s = $c{$c};
		$s = join(', ', @$s);
		$s = 'OK' unless $s;
		push(@status, "$c: $s");
	}

	# check that no temp is over the treshold
	my $warn = 28;
	my $crit = 33;
	while (my($t, $c) = each %t) {
		if ($c > $crit) {
			push(@status, "$t: ${c}C");
			$status = $ERRORS{CRITICAL};
		} elsif ($c > $warn) {
			push(@status, "$t: ${c}C");
			$status = $ERRORS{WARNING} unless $status;
		}
	}

	$message .= "hp_msa: ".join(', ', @status) if @status;
}

sub which {
	my $prog = shift;

	my @paths = split /:/, $ENV{'PATH'};
	unshift(@paths, qw(/usr/local/nrpe /usr/local/bin /sbin /usr/sbin /bin /usr/sbin));

	for my $path (@paths) {
		return "$path/$prog" if -x "$path/$prog";
	}
	return undef;
}

sub find_file {
	for my $file (@_) {
		return $file if -f $file;
	}
	return undef;
}

###########################################################################
sub sudoers {
	# build values to be added
	my @sudo;
	push(@sudo, "CHECK_RAID ALL=(root) NOPASSWD: $ipssend getconfig 1 LD\n") if $ipssend;
	push(@sudo, "CHECK_RAID ALL=(root) NOPASSWD: $aaccli container list /full\n") if $aaccli;
	push(@sudo, "CHECK_RAID ALL=(root) NOPASSWD: $tw_cli info*\n") if $tw_cli;
	push(@sudo, "CHECK_RAID ALL=(root) NOPASSWD: $mpt_status -s\n") if $mpt_status and -d "/proc/mpt";
	push(@sudo, "CHECK_RAID ALL=(root) NOPASSWD: $cat /proc/megaide/0/status\n") if -d "/proc/megaide/0";
	push(@sudo, "CHECK_RAID ALL=(root) NOPASSWD: $cat /proc/megaide/1/status\n") if -d "/proc/megaide/1";
	push(@sudo, "CHECK_RAID ALL=(root) NOPASSWD: $arcconf GETCONFIG 1 *\n") if $arcconf;
	push(@sudo, "CHECK_RAID ALL=(root) NOPASSWD: $megarc -AllAdpInfo -nolog\n") if $megarc;
	push(@sudo, "CHECK_RAID ALL=(root) NOPASSWD: $megarc -dispCfg -a* -nolog\n") if $megarc;
	push(@sudo, "CHECK_RAID ALL=(root) NOPASSWD: $cmdtool2 -AdpAllInfo -aALL -nolog\n") if $cmdtool2;
	push(@sudo, "CHECK_RAID ALL=(root) NOPASSWD: $cmdtool2 -CfgDsply -a* -nolog\n") if $cmdtool2;
	push(@sudo, "CHECK_RAID ALL=(root) NOPASSWD: $megacli -PDList -aALL -NoLog\n") if $megacli;
	foreach my $mr (</proc/mega*/*/raiddrives*>) {
		push(@sudo, "CHECK_RAID ALL=(root) NOPASSWD: $cat $mr\n") if -d $mr;
	}

	if ($cciss_vol_status) {
		my @c;
		foreach my $f (</proc/driver/cciss/*>) {
			open my $fh, '<', $f or next;
			while (<$fh>) {
				if (my($s) = m{^(cciss/[^:]+):}) {
					push(@c, "/dev/$s");
				}
			}
			close $fh;
		}
		foreach my $c (@c) {
			push(@sudo, "CHECK_RAID ALL=(root) NOPASSWD: $cciss_vol_status $c\n");
		}
	}

	unless (@sudo) {
		print "Your configuration does not need to use sudo, sudoers not updated\n";
		return;
	}

	my $sudoers = find_file('/usr/local/etc/sudoers', '/etc/sudoers');
	my $visudo = which('visudo');

	die "Unable to find sudoers file.\n" unless -f $sudoers;
	die "Unable to write to sudoers file.\n" unless -w $sudoers;
	die "visudo program not found\n" unless -x $visudo;
	die "cat program not found\n" unless -x $cat;

	print "Updating file $sudoers\n";

	# NOTE: secure as visudo itself: /etc is root owned
	my $new = $sudoers.".new.".$$;

	# setup to have sane perm for new sudoers file
	umask(0227);

	# insert old sudoers
	open my $old, '<', $sudoers or die $!;
	open my $fh, '>', $new or die $!;
	while (<$old>) {
		print $fh $_;
	}
	close $old or die $!;

	# setup alias, so we could easily remove these later by matching lines with 'CHECK_RAID'
	# also this avoids installing ourselves twice.
	print $fh "\n";
	print $fh "# Lines matching CHECK_RAID added by $0 -S on ", scalar localtime, "\n";
	print $fh "User_Alias CHECK_RAID=nagios\n";
	print $fh @sudo;

	close $fh;

	# validate sudoers
	system($visudo, '-c', '-f', $new) == 0 or unlink($new),exit $? >> 8;

	# use the new file
	rename($new, $sudoers) or die $!;

	print "$sudoers file updated.\n";
}

#####################################################################
$ENV{'BASH_ENV'}='';
$ENV{'ENV'}='';

Getopt::Long::Configure('bundling');
GetOptions("v" => \$opt_v, "version" => \$opt_v,
	 "h" => \$opt_h, "help" => \$opt_h,
	 "d" => \$opt_d, "debug" => \$opt_d,
	 "S" => \$opt_S, "sudoers" => \$opt_S,
	 "W" => \$opt_W, "warnonly" => \$opt_W
);

if ($opt_S) {
	sudoers;
	exit 0;
}

@ignore = @ARGV if @ARGV;

if ($opt_v) {
	print "check_raid Version $VERSION\n";
	exit $ERRORS{'OK'};
}
if ($opt_h) {
	print_help();
	exit $ERRORS{'OK'};
}
if ($opt_W) {
	$ERRORS{CRITICAL} = $ERRORS{WARNING};
}

$status = $ERRORS{OK};
$message = '';

check_gdth if -d "/proc/scsi/gdth" and valid("/proc/scsi/gdth");
check_megaide if -d "/proc/megaide" and valid("/proc/megaide");
check_mdstat if -f "/proc/mdstat" and valid("/proc/mdstat");
check_mpt if -d "/proc/mpt" and valid("/proc/mpt");
check_dpt_i2o if -d "/proc/scsi/dpt_i2o" and valid("/proc/scsi/dpt_i2o");
check_megaraid if -d "/proc/megaraid" and valid("/proc/megaraidc");
check_aaccli if -d "/proc/scsi/aacraid" and valid("/proc/scsi/aacraid");
check_afacli if $afacli;
check_lsraid if $lsraid;
check_megacli if $megacli;
check_metastat if $metastat;
check_lsvg if $lsvg;
check_ipssend if $ipssend;
check_tw_cli if $tw_cli;
check_arcconf if $arcconf;
check_megarc if $megarc;
check_cmdtool2 if $cmdtool2;
check_cciss if -d "/proc/driver/cciss" and valid("/proc/driver/cciss");
check_hp_msa if sys_have_msa;


if ($message) {
	if ($status == $ERRORS{OK}) {
		print "OK: ";
	} elsif ($status == $ERRORS{WARNING}) {
		print "WARNING: ";
	} elsif ($status == $ERRORS{CRITICAL}) {
		print "CRITICAL: ";
	} else {
		print "UNKNOWN: ";
	}
	print "$message\n";
} else {
	$status = $ERRORS{UNKNOWN};
	print "No RAID configuration found.\n";
}
exit $status;

package SerialLine;
# Package dealing with connecting to serial line and handling UUCP style locks.
use strict;
use Carp;

sub new {
	my $self = shift;
	my $class = ref($self) || $self;
	my $device = shift;

	my $this = {
		lockdir => "/var/lock",
		lockfile => undef,
		device => $device,
		fh => undef,
	};

	bless($this, $class);
}

sub lock {
	my $self = shift;
	# create lock in style: /var/lock/LCK..ttyS0
	my $device = shift;
	my ($lockfile) = $self->{device} =~ m#/dev/(.+)#;
	$lockfile = "$self->{lockdir}/LCK..$lockfile";
	if (-e $lockfile) {
		return 0;
	}
	open(my $fh, '>', $lockfile) || croak "Can't create lock: $lockfile\n";
	print $fh $$;
	close($fh);

	$self->{lockfile} = $lockfile;
}

sub open {
	my $self = shift;

	$self->lock or return;

	# open the device
	open(my $fh, "+>$self->{device}") || croak "Couldn't open $self->{device}, $!\n";

	$self->{fh} = $fh;
}

sub close {
	my $self = shift;
	if ($self->{fh}) {
		close($self->{fh});
		undef($self->{fh});
		unlink $self->{lockfile} or carp $!;
	}
}

sub DESTORY {
	my $self = shift;
	$self->close();
}
