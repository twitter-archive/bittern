#
# Copyright (c) 2009 Aconex.  All Rights Reserved.
# Copyright (c) 2015 Twitter, Inc..  All Rights Reserved.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#

use strict;
use warnings;
use PCP::PMDA;

my $pmda = PCP::PMDA->new('bittern', 286);

sub bittern_parse_stats {
	my($hash_ret) = undef;
	my($cache_name, $stats_file) = @_;
	my($file);
	if (open($file, $stats_file)) {
		my($stats_str);
		while ($stats_str = <$file>) {
			chomp($stats_str);
			# print "$stats_str\n";
			my($cache_name, $sysfs_name, $stats_nvpairs) = split(': ', $stats_str);
			# print "$stats_str\n\n";
			my(@stats_strs);
			@stats_strs = split(' ', $stats_nvpairs);
			# print "\n";
			# print "stats_strs = @stats_strs\n";
			# print "\n";
			foreach my $s (@stats_strs) {
				# print "s : '$s':    ";
				my($name, $value) = split('=', $s);
				# strip percentage or other trailing info from value
				$value =~ s/\(.*//;
				# print "name='$name', value='$value'";
				# print "\n";
				if ($name eq "completeted_requests") {
					# hack to handle misspelled metric
					$name = "completed_requests";
				}
				$hash_ret->{$name} = $value;
			}
		}
		close($file);
	}
	return $hash_ret;
}

# do delta of (curr, prev) and return 3 hashes,
# {c} which contains current values,
# {p} which contains previous values,
# {d} which contains the deltas.
sub bittern_do_deltas
{
	my($hash_ret) = undef;
	my($curr, $prev) = @_;
	if ($curr && $prev) {
		foreach my $key (keys %$curr) {
			$hash_ret->{c}->{$key} = $curr->{$key};
			$hash_ret->{p}->{$key} = $prev->{$key};
			$hash_ret->{d}->{$key} = $curr->{$key} - $prev->{$key};
		}
	}
	$hash_ret;
}

#
# this array holds the association between the instance id and the cache name
# bittern_caches[$instance_id] == $cache_name
#
my @bittern_caches = ( );
#
# this hash is in the form of
#	$bittern_cache_stats->{$cache_name}->{$class}->{$metric_name}
# for instance:
#	$bittern_cache_stats->{"bitcache0"}->{"stats"}->{"read_requests"}
# the corresponding metric is:
#	"bittern.stats.read_requests"
#
my $bittern_caches_stats = undef;

sub bittern_scan
{
	my $fd;
	my ($instance_id) = 0;
	my (@caches) = ( );
	@bittern_caches = ( );
	$bittern_caches_stats = undef;
	if (opendir($fd, "/sys/fs/bittern/")) {
		my($entry);
		while ($entry = readdir($fd)) {
			next if ($entry eq ".");
			next if ($entry eq "..");
			my($root_stats) = "/sys/fs/bittern/" . $entry . "/";
			# $pmda->log("bittern_cache_instance - $root_stats");
			$bittern_caches_stats->{$entry}->{stats} = bittern_parse_stats($entry, $root_stats . "/stats");
			$bittern_caches_stats->{$entry}->{stats_extra} = bittern_parse_stats($entry, $root_stats . "/stats_extra");
			push @caches, $instance_id, $entry;
			push @bittern_caches, $entry;
			$pmda->log("bittern_cache_instance - $entry : $root_stats : instance_id = $instance_id");
			$instance_id++;
		}
		closedir($fd);
	}
	$pmda->replace_indom(0, \@caches);
}

sub bittern_cache_fetch
{
	# $pmda->log("bittern_cache_fetch");
	bittern_scan();
}

sub bittern_cache_instance
{
	# $pmda->log("bittern_cache_instance");
	bittern_scan();
}

sub bittern_fetch_callback
{
	my($cluster, $item, $inst) = @_;
	my($metric_name) = pmda_pmid_name($cluster, $item);
	my($value) = 1;

	if (!defined($metric_name))	{ return (PM_ERR_PMID, 0); }

	my($instance_name) = $bittern_caches[$inst];
	# $pmda->log("bittern_fetch_callback metric_name=$metric_name cluster=$cluster:item=$item (inst=$inst)");
	# $pmda->log("bittern_fetch_callback instance_name=$instance_name");

	# metric triple is in the form of "bittern.stats.read_misses"
	my($m_bittern, $m_stat, $m_name) = split(/\./, $metric_name);
	# $pmda->log("bittern_fetch_callback m_stat=$m_stat, m_name=$m_name");

	if ($inst == PM_IN_NULL)	{ return (PM_ERR_INST, 0); }
	if (!defined($bittern_caches_stats)) { return (PM_ERR_INST, 0); }
	my($instance_stats) = $bittern_caches_stats->{$instance_name};
	# $pmda->log("bittern_fetch_callback instance_stats=$instance_stats");
	if (!defined($instance_stats)) { return (PM_ERR_INST, 0); }

	my($stat) = $instance_stats->{$m_stat};
	# $pmda->log("bittern_fetch_callback stats=$stat");
	if (!defined($stat)) { return (PM_ERR_INST, 0); }

	if (!defined($stat->{$m_name})) {
		$value = 1;
		$pmda->log("bittern_fetch_callback $m_stat:$m_name = undefined --- fake it till you make it");
	} else {
		$value = $stat->{$m_name};
		# $pmda->log("bittern_fetch_callback $m_stat:$m_name = $value\n");
	}

	return ($value, 1);
}

my(@counter_metrics) = ("bittern.stats.read_requests",
			"bittern.stats.write_requests",
			"bittern.stats.read+write_requests",
			"bittern.stats.read_misses",
			"bittern.stats.clean_write_misses",
			"bittern.stats.clean_write_misses_rmw",
			"bittern.stats.clean_write_hits_rmw",
			"bittern.stats.dirty_read_hits",
			"bittern.stats.dirty_write_hits",
			"bittern.stats.dirty_write_misses",
			"bittern.stats.dirty_write_misses_rmw",
			"bittern.stats.dirty_write_hits_rmw",
			"bittern.stats.writebacks",
			"bittern.stats.invalidations",
			"bittern.stats.read_cached_device_requests",
			"bittern.stats.write_cached_device_requests",
			"bittern.stats.read+write_cached_device_requests",
			"bittern.stats_extra.total_deferred_requests",
			"bittern.stats_extra.completed_requests",
			"bittern.stats_extra.total_read_misses",
			"bittern.stats_extra.total_read_hits",
			"bittern.stats_extra.total_write_misses",
			"bittern.stats_extra.total_write_hits",
			"bittern.stats_extra.read_hits_busy",
			"bittern.stats_extra.write_hits_busy",
			"bittern.stats_extra.read_misses_busy",
			"bittern.stats_extra.write_misses_busy",
			"bittern.stats_extra.writebacks",
			"bittern.stats_extra.writebacks_stalls",
			"bittern.stats_extra.writebacks_clean",
			"bittern.stats_extra.writebacks_invalid",
			"bittern.stats_extra.invalidations",
			"bittern.stats_extra.idle_invalidations",
			"bittern.stats_extra.busy_invalidations",
			"bittern.stats_extra.invalidations_map",
			"bittern.stats_extra.invalidations_invalidator",
			"bittern.stats_extra.invalidations_writeback",
			"bittern.stats_extra.no_invalidations_all_blocks_busy",
			"bittern.stats_extra.invalid_blocks_busy",
			"bittern.stats_extra.flush_requests",
			"bittern.stats_extra.pure_flush_requests",
			"bittern.stats_extra.discard_requests",
			"bittern.stats_extra.read_sequential_bypass_count",
			"bittern.stats_extra.read_sequential_io_count",
			"bittern.stats_extra.read_non_sequential_io_count",
			"bittern.stats_extra.read_sequential_bypass_hit",
			"bittern.stats_extra.write_sequential_bypass_count",
			"bittern.stats_extra.write_sequential_io_count",
			"bittern.stats_extra.write_non_sequential_io_count",
			"bittern.stats_extra.write_sequential_bypass_hit",
			"bittern.stats_extra.dirty_write_clone_alloc_ok",
			"bittern.stats_extra.dirty_write_clone_alloc_fail",
			"bittern.stats_extra.list_empty_pending");
my(@instant_metrics) = ("bittern.stats.deferred_requests",
			"bittern.stats.pending_requests",
			"bittern.stats.pending_writeback_requests",
			"bittern.stats.pending_invalidate_requests",
			"bittern.stats.total_entries",
			"bittern.stats.valid_cache_entries",
			"bittern.stats.valid_dirty_cache_entries",
			"bittern.stats.valid_clean_cache_entries",
			"bittern.stats.invalid_cache_entries",
			"bittern.stats_extra.pending_read_requests",
			"bittern.stats_extra.pending_read_bypass_requests",
			"bittern.stats_extra.pending_write_requests",
			"bittern.stats_extra.highest_pending_requests",
			"bittern.stats_extra.highest_pending_invalidate_requests",
			"bittern.stats.pending_cached_device_requests",
			"bittern.stats_extra.highest_pending_cached_device_requests",
			"bittern.stats_extra.highest_deferred_requests");
my($pmid) = 0;
foreach my $s (@counter_metrics) {
	# $pmda->log("add_metric counter pmid=$pmid, name=$s");
	$pmda->add_metric(pmda_pmid(0,$pmid),
			  PM_TYPE_U64, 0,
			  PM_SEM_COUNTER,
			  pmda_units(0,0,1,0,0,PM_COUNT_ONE),
			  $s,
			  '', '');
	$pmid++;
}
foreach my $s (@instant_metrics) {
	# $pmda->log("add_metric instant pmid=$pmid, name=$s");
	$pmda->add_metric(pmda_pmid(0,$pmid),
			  PM_TYPE_U64, 0,
			  PM_SEM_INSTANT,
			  pmda_units(0,0,1,0,0,PM_COUNT_ONE),
			  $s,
			  '', '');
	$pmid++;
}
$pmda->add_indom(0, [], '', '');
$pmda->set_fetch(\&bittern_cache_fetch);
$pmda->set_instance(\&bittern_cache_instance);
$pmda->set_fetch_callback(\&bittern_fetch_callback);
$pmda->set_user('pcp');
$pmda->run;

=pod

=head1 NAME

pmdabittern - Linux Bittern performance metrics domain agent (PMDA)

=head1 DESCRIPTION

B<pmdabittern> is a Performance Metrics Domain Agent (PMDA) which
exports metric values from Bittern caches in the Linux
kernel.

=head1 INSTALLATION

If you want access to the names and values for the bittern performance
metrics, do the following as root:

	# cd $PCP_PMDAS_DIR/bittern
	# ./Install

If you want to undo the installation, do the following as root:

	# cd $PCP_PMDAS_DIR/bittern
	# ./Remove

B<pmdabittern> is launched by pmcd(1) and should never be executed
directly.  The Install and Remove scripts notify pmcd(1) when
the agent is installed or removed.

=head1 FILES

=over

=item $PCP_PMDAS_DIR/bittern/Install

installation script for the B<pmdabittern> agent

=item $PCP_PMDAS_DIR/bittern/Remove

undo installation script for the B<pmdabittern> agent

=item $PCP_LOG_DIR/pmcd/bittern.log

default log file for error messages from B<pmdabittern>

=back

=head1 SEE ALSO

pmcd(1).
