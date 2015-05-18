#!/usr/bin/perl
#
# Bittern Cache.
#
# Copyright(c) 2013, 2014, 2015, Twitter, Inc., All rights reserved.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
#

require 5;
use strict;
use Getopt::Long;

my($help_msg) =
"
        The bc.stats.pl tool prints cache statistics every second.
        The following fields are printed:

        (bittern) device:
		The internal name of the bittern cache as it appears in
		/sys/fs/bittern/<name>. The external name be found as
		follows:
			bc_control.h --list

        (inflight) pending:
                Read/write requests which are currently inflight
                and in a pending state. These requests are currently being
                processed thru the cache state machine.
        (inflight) deferred:
                Read/write requests which are currently deferred
                and in a pending state. These requests will be processed
                as soon as resources are available.

        (completed) read:
                Completed Read requests.
        (completed) write:
                Completed Write requests.
        (completed) writebacks:
                Dirty cache blocks flushed by the background writer.
        (completed) invals:
                Invalidations peformed by either the invalidator thread,
		background writer or during write cloning.

        (hits) r-hits:
                Read hit count.
        (hits) w-hits:
                Write hit count.

        (cache-blocks) invalid:
        (cache-blocks) clean:
        (cache-blocks) dirty:
        (cache-blocks) dirty%:
                Total number of invalid, clean and dirty blocks.
                Percentage of dirty blocks.

        (cached-device) rd:
                Read requests issued to the cached device.
        (cached-device) wr:
                Write requests issued to the cached device.
        (cached-device) rdseq:
                Bypass read requests issued to the cached device.
        (cached-device) wrseq:
                Bypass write requests issued to the cached device.
";
sub do_help {
	print "$0:\n$help_msg\n";
	exit(0);
}

Getopt::Long::GetOptions("help", sub { do_help(); });

sub setup_cache_list {
	my @cache_list = ();

	# Default is to watch all bittern devices,
	# unless specified in the cmdline.
	if ($#ARGV >= 0) {
		for my $cache_name (@ARGV) {
			my($cache_stats) = "/sys/fs/bittern/$cache_name/stats";
			if (! -r $cache_stats) {
				print "warning: $cache_stats does not exist\n";
			}
			push @cache_list, $cache_name;
		}
	} else {
		for my $cache_name (glob("/sys/fs/bittern/*")) {
			$cache_name =~ s/^\/sys\/fs\/bittern\///;
			push @cache_list, $cache_name;
		}
	}

	return @cache_list;
}

sub parse_cache_stat {
	my($hash_ret) = undef;
	my($cache_name, $stat_file) = @_;
	my($file);
	if (open($file, $stat_file)) {
		my($stat_str);
		while ($stat_str = <$file>) {
			chomp($stat_str);
			my($dummy1, $dummy2, $nvpairs) = split(': ', $stat_str);
			my(@stat_strs);
			@stat_strs = split(' ', $nvpairs);
			foreach my $s (@stat_strs) {
				my($name, $value) = split('=', $s);
				# strip percentage or other trailing info
				$value =~ s/\(.*//;
				$hash_ret->{$name} = $value;
			}
		}
		close($file);
	}

	return $hash_ret;
}

sub parse_cache_stats {
	my($name) = @_;
	my($hash_ret) = undef;

	for my $file (qw(stats stats_extra bgwriter)) {
		my $full_path = "/sys/fs/bittern/$name/$file";
		$hash_ret->{$file} = parse_cache_stat($name, $full_path);
		# Make it simple and just behave all-or-nothing.
		# This allows to main code to just check one hash ref
		# instead of the whole set.
		if (!defined($hash_ret->{$file})) {
			return undef;
		}
	}

	return $hash_ret;
}

sub parse_caches_stats {
	my @caches = @_;
	my($hash_ret) = undef;

	for my $name (@caches) {
		$hash_ret->{$name} = parse_cache_stats($name);
	}

	return $hash_ret;
}

#
# do delta of (curr, prev) and return 3 hashes,
# {c} which contains current values,
# {p} which contains previous values,
# {d} which contains the deltas.
sub do_deltas {
	my($curr, $prev) = @_;
	my($hash_ret) = undef;
	if ($curr && $prev) {
		foreach my $key (keys %$curr) {
			$hash_ret->{c}->{$key} = $curr->{$key};
			$hash_ret->{p}->{$key} = $prev->{$key};
			$hash_ret->{d}->{$key} = $curr->{$key} - $prev->{$key};
		}
	}
	$hash_ret;
}

sub str_pad_symbol {
	my($str, $len, $symbol) = @_;
	while (length($str) + 1 < $len) {
		$str = $symbol . $str . $symbol;
	}
	# pad on the left if we don't fully fit
	if (length($str) < $len) {
		$str = $symbol . $str;
	}
	$str;
}

sub str_pad_dashes {
	my($str, $len) = @_;
	return str_pad_symbol($str, $len, '-');
}

sub str_pad_spaces {
	my($str, $len) = @_;
	return str_pad_symbol($str, $len, ' ');
}

sub do_stats {
	my($cache_name,
	   $print_hdr,
	   $stats,
	   $stats_extra,
	   $stats_bgwriter) = @_;

	my($s_hdr0) = "";
	my($s_hdr) = "";
	my($s_val) = "";
	my($s_spaces) = "   ";

	#
	# queued requests
	#
	$s_hdr0 .= sprintf("%10s" . $s_spaces, str_pad_dashes("bittern", 10));
	$s_hdr .= sprintf("%10s" . $s_spaces, "device");
	$s_val .= sprintf("%10s" . $s_spaces, $cache_name);
	#
	# inflight
	#
	$s_hdr0 .= sprintf("%13s" . $s_spaces, str_pad_dashes("inflight", 13));
	$s_hdr .= sprintf("%6s %6s" . $s_spaces, "defer", "pend");
	$s_val .= sprintf("%6d %6d" . $s_spaces,
			  $stats->{c}->{deferred_requests},
			  $stats->{c}->{pending_requests});
	#
	# completed
	#
	$s_hdr0 .= sprintf("%27s" . $s_spaces, str_pad_dashes("completed", 27));
	$s_hdr .= sprintf("%6s %6s %6s %6s" . $s_spaces,
			  "reads",
			  "writes",
			  "wrback",
			  "invals");
	$s_val .= sprintf("%6d %6d %6d %6d" . $s_spaces,
			  $stats_extra->{d}->{completed_read_requests},
			  $stats_extra->{d}->{completed_write_requests},
			  $stats_extra->{d}->{completed_writebacks},
			  $stats_extra->{d}->{completed_invalidations});
	#
	# hits
	#
	my($r_hits) = $stats->{d}->{clean_read_hits} +
		      $stats->{d}->{dirty_read_hits};
	my($w_hits) = $stats->{d}->{clean_write_hits} +
		      $stats->{d}->{dirty_write_hits};
	$s_hdr0 .= sprintf("%15s" . $s_spaces, str_pad_dashes("hits", 15));
	$s_hdr .= sprintf("%7s %7s" . $s_spaces, "r-hits", "w-hits");
	$s_val .= sprintf("%7d %7d" . $s_spaces,
			  $r_hits,
			  $w_hits);
	#
	# clean/dirty
	#
	my($pct_f) = ($stats->{c}->{valid_dirty_cache_entries} * 100) /
		     $stats->{c}->{total_entries};
	my($pct_s) = sprintf("%3.2f%%", $pct_f);
	$s_hdr0 .= sprintf("%34s" . $s_spaces,
			   str_pad_dashes("cache-blocks", 34));
	$s_hdr .= sprintf("%8s %8s %8s %7s" . $s_spaces,
			  "inval",
			  "clean",
			  "dirty",
			  "dirty%");
	$s_val .= sprintf("%8d %8d %8d %7s" . $s_spaces,
			  $stats->{c}->{invalid_cache_entries},
			  $stats->{c}->{valid_clean_cache_entries},
			  $stats->{c}->{valid_dirty_cache_entries},
			  $pct_s);
	#
	# disk access
	#
	$s_hdr0 .= sprintf("%27s" . $s_spaces,
			   str_pad_dashes("cached-device", 27));
	$s_hdr .= sprintf("%6s %6s %6s %6s" . $s_spaces,
			  "rd",
			  "wr",
			  "rdseq",
			  "wrseq");
	$s_val .= sprintf("%6d %6d %6d %6d" . $s_spaces,
			  $stats->{d}->{read_cached_device_requests},
			  $stats->{d}->{write_cached_device_requests},
			  $stats_extra->{d}->{read_sequential_bypass_count},
			  $stats_extra->{d}->{write_sequential_bypass_count});

	if ($print_hdr != 0) {
		printf("%s\n", $s_hdr0);
		printf("%s\n", $s_hdr);
	}
	printf("%s\n", $s_val);
}

#
# MAIN
#

my @cache_name_list = setup_cache_list();
if ($#cache_name_list < 0) {
	print "$0: no cache device found or no names were supplied\n";
	exit;
}

my($caches_stats) = parse_caches_stats(@cache_name_list);
my($loop_count) = 0;

# How many lines we get to print before printing the header? We print two lines
# for the header, then a line per cache, 5 times, then the next header(s).
my ($loop_before_printing_header) = 5;

while (1) {
	my($print_hdr) = 0;
	if ($loop_count % $loop_before_printing_header == 0) {
		$print_hdr = 1;
	}

	sleep(1);

	#
	# parse stats from /sys/fs/
	#

	my($curr_caches_stats) = parse_caches_stats(@cache_name_list);
	my($prev_caches_stats) = $caches_stats;

	foreach my $cache_name (@cache_name_list) {

		#
		# calc deltas
		#

		my($curr) = $curr_caches_stats->{$cache_name};
		my($prev) = $prev_caches_stats->{$cache_name};

		if (!defined($curr) || !defined($prev)) {
			$loop_count = 0;
			next;
		}

		my($stats) = do_deltas($curr->{'stats'},
				       $prev->{'stats'});
		my($stats_extra) = do_deltas($curr->{'stats_extra'},
					     $prev->{'stats_extra'});
		my($stats_bgwriter) = do_deltas($curr->{'bgwriter'},
						$prev->{'bgwriter'});

		do_stats($cache_name,
			 $print_hdr,
			 $stats,
			 $stats_extra,
			 $stats_bgwriter);

		$print_hdr = 0;
		$loop_count++;
	}

	$caches_stats = $curr_caches_stats;
}
