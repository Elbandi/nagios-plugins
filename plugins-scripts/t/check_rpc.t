#! /usr/bin/perl -w -I ..
#
# Remote Procedure Call (RPC) Tests via check_rpc
#
# $Id: check_rpc.t 1207 2005-07-25 01:47:15Z illumino $
#

use strict;
use Test;
use NPTest;

use vars qw($tests);
BEGIN {$tests = 2; plan tests => $tests}

my $successOutput = '/^check_rpc/';

my $t;

$t += checkCmd( "./check_rpc -V",   0, $successOutput );

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
